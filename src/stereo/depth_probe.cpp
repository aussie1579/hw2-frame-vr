#include "stereo/depth_probe.h"

#include <cstring>

#include "common/log.h"
#include "proxy/gl_api.h"
#include "stereo/view_override.h"

namespace hw2vr::depth {
namespace {

constexpr int kSlots = 3;  // readbacks in flight
struct Slot {
    gl::GLuint pbo = 0;
    void* fence = nullptr;
    unsigned sequence = 0;
    unsigned epoch = 0;
};
Slot g_slots[kSlots];
int g_next_slot = 0;
unsigned g_sequence = 0, g_latest_sequence = 0;
unsigned g_epoch = 0;  // bumped by clear_ray: results for an earlier ray are dropped

float g_eye[16], g_proj[16];
bool g_have_camera = false;
Vec3 g_origin, g_dir;
bool g_have_ray = false;

gl::GLuint g_depth_tex = 0, g_depth_fbo = 0, g_result_tex = 0, g_result_fbo = 0, g_program = 0, g_vao = 0;
gl::GLenum g_depth_format = 0;
int g_width = 0, g_height = 0;
gl::GLint g_loc_depth = -1, g_loc_origin = -1, g_loc_dir = -1, g_loc_eye = -1, g_loc_proj = -1, g_loc_range = -1;
bool g_failed = false;
unsigned g_logged_scene = 0;

Result g_latest = Result::None;
float g_latest_distance = 0.0f;
unsigned g_probes = 0, g_results = 0, g_hits = 0;

constexpr const char* kVs = R"(#version 330 core
void main() {
    vec2 p = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));
    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
}
)";
// Log-spaced steps from u_range.x to u_range.y along the ray; a step counts as a hit when the point is behind the
// visible surface but by no more than a quarter of that surface's depth (so passing behind a ship isn't a hit),
// then the crossing is refined by bisection. Depths are eye-space (-z); the eye projection's depth row converts.
constexpr const char* kFs = R"(#version 330 core
uniform sampler2D u_depth;
uniform vec3 u_origin;
uniform vec3 u_dir;
uniform mat4 u_eye;
uniform mat4 u_proj;
uniform vec2 u_range;
out vec4 o_result;

bool probe(float t, out float point_depth, out float surface_depth) {
    vec4 view = u_eye * vec4(u_origin + u_dir * t, 1.0);
    point_depth = -view.z;
    surface_depth = 1e30;
    vec4 clip = u_proj * view;
    if (clip.w <= 0.0) return false;
    vec3 ndc = clip.xyz / clip.w;
    if (any(greaterThan(abs(ndc.xy), vec2(1.0)))) return false;
    float d = texture(u_depth, ndc.xy * 0.5 + 0.5).r;
    if (d < 1.0) surface_depth = u_proj[3][2] / ((d * 2.0 - 1.0) + u_proj[2][2]);
    return true;
}

void main() {
    const int kSteps = 192;
    const float kThickness = 0.25;
    float ratio = u_range.y / u_range.x;
    float previous = u_range.x;
    float result = -1.0;
    for (int i = 0; i < kSteps; ++i) {
        float t = u_range.x * pow(ratio, float(i) / float(kSteps - 1));
        float pd, sd;
        if (probe(t, pd, sd) && pd >= sd && pd - sd <= kThickness * sd) {
            float a = previous, b = t;
            for (int k = 0; k < 14; ++k) {
                float m = 0.5 * (a + b);
                float mpd, msd;
                if (probe(m, mpd, msd) && mpd >= msd) b = m; else a = m;
            }
            result = b;
            break;
        }
        previous = t;
    }
    o_result = vec4(result, result >= 0.0 ? 1.0 : 0.0, 0.0, 1.0);
}
)";

bool make_result_target() {
    const gl::Api& a = gl::api();
    a.GenTextures(1, &g_result_tex);
    a.BindTexture(gl::TEXTURE_2D, g_result_tex);
    a.TexImage2D(gl::TEXTURE_2D, 0, gl::RGBA32F, 1, 1, 0, gl::RGBA, gl::FLOAT, nullptr);
    a.TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_MIN_FILTER, gl::NEAREST);
    a.TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_MAG_FILTER, gl::NEAREST);
    a.GenFramebuffers(1, &g_result_fbo);
    a.BindFramebuffer(gl::DRAW_FRAMEBUFFER, g_result_fbo);
    a.FramebufferTexture2D(gl::DRAW_FRAMEBUFFER, gl::COLOR_ATTACHMENT0, gl::TEXTURE_2D, g_result_tex, 0);
    if (a.CheckFramebufferStatus(gl::DRAW_FRAMEBUFFER) != gl::FRAMEBUFFER_COMPLETE) {
        HW2VR_LOG("depth: result target incomplete - laser depth off");
        return false;
    }
    g_program = gl::build_program(kVs, kFs, "laser depth");
    if (!g_program) return false;
    g_loc_depth = a.GetUniformLocation(g_program, "u_depth");
    g_loc_origin = a.GetUniformLocation(g_program, "u_origin");
    g_loc_dir = a.GetUniformLocation(g_program, "u_dir");
    g_loc_eye = a.GetUniformLocation(g_program, "u_eye");
    g_loc_proj = a.GetUniformLocation(g_program, "u_proj");
    g_loc_range = a.GetUniformLocation(g_program, "u_range");
    a.GenVertexArrays(1, &g_vao);
    for (Slot& s : g_slots) {
        a.GenBuffers(1, &s.pbo);
        a.BindBuffer(gl::PIXEL_PACK_BUFFER, s.pbo);
        a.BufferData(gl::PIXEL_PACK_BUFFER, 4 * sizeof(float), nullptr, gl::STREAM_READ);
    }
    return true;
}

// Our single-sample copy of the scene depth, in the scene's exact depth format (depth blits need matching formats).
// The scene framebuffer must be bound for reading.
bool ensure_depth_copy(int width, int height) {
    const gl::Api& a = gl::api();
    gl::GLint type = 0, bits = 0, component = 0, stencil = 0;
    a.GetFramebufferAttachmentParameteriv(gl::READ_FRAMEBUFFER, gl::DEPTH_ATTACHMENT, gl::FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE, &type);
    if (type == static_cast<gl::GLint>(gl::NONE)) return false;
    a.GetFramebufferAttachmentParameteriv(gl::READ_FRAMEBUFFER, gl::DEPTH_ATTACHMENT, gl::FRAMEBUFFER_ATTACHMENT_DEPTH_SIZE, &bits);
    a.GetFramebufferAttachmentParameteriv(gl::READ_FRAMEBUFFER, gl::DEPTH_ATTACHMENT,
                                          gl::FRAMEBUFFER_ATTACHMENT_COMPONENT_TYPE, &component);
    a.GetFramebufferAttachmentParameteriv(gl::READ_FRAMEBUFFER, gl::DEPTH_ATTACHMENT,
                                          gl::FRAMEBUFFER_ATTACHMENT_STENCIL_SIZE, &stencil);
    const bool is_float = component == static_cast<gl::GLint>(gl::FLOAT);
    gl::GLenum format, pixel_format = gl::DEPTH_COMPONENT, pixel_type = gl::UNSIGNED_INT;
    if (is_float) format = stencil ? gl::DEPTH32F_STENCIL8 : gl::DEPTH_COMPONENT32F, pixel_type = gl::FLOAT;
    else if (stencil) format = gl::DEPTH24_STENCIL8, pixel_type = gl::UNSIGNED_INT_24_8;
    else format = bits >= 32 ? gl::DEPTH_COMPONENT32 : gl::DEPTH_COMPONENT24;
    if (stencil) pixel_format = gl::DEPTH_STENCIL;
    if (is_float && stencil) pixel_type = 0x8DAD;  // GL_FLOAT_32_UNSIGNED_INT_24_8_REV
    if (format == g_depth_format && width == g_width && height == g_height && g_depth_tex) return true;

    if (!g_depth_tex) a.GenTextures(1, &g_depth_tex);
    a.BindTexture(gl::TEXTURE_2D, g_depth_tex);
    a.TexImage2D(gl::TEXTURE_2D, 0, static_cast<gl::GLint>(format), width, height, 0, pixel_format, pixel_type, nullptr);
    a.TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_MIN_FILTER, gl::NEAREST);
    a.TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_MAG_FILTER, gl::NEAREST);
    a.TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_WRAP_S, gl::CLAMP_TO_EDGE);
    a.TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_WRAP_T, gl::CLAMP_TO_EDGE);
    a.TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_COMPARE_MODE, static_cast<gl::GLint>(gl::NONE));
    if (!g_depth_fbo) a.GenFramebuffers(1, &g_depth_fbo);
    a.BindFramebuffer(gl::DRAW_FRAMEBUFFER, g_depth_fbo);
    a.FramebufferTexture2D(gl::DRAW_FRAMEBUFFER, stencil ? gl::DEPTH_STENCIL_ATTACHMENT : gl::DEPTH_ATTACHMENT,
                           gl::TEXTURE_2D, g_depth_tex, 0);
    a.DrawBuffer(gl::NONE);
    const bool ok = a.CheckFramebufferStatus(gl::DRAW_FRAMEBUFFER) == gl::FRAMEBUFFER_COMPLETE;
    HW2VR_LOG("depth: scene depth %d bits%s%s, copy %dx%d%s", bits, is_float ? " float" : "", stencil ? " + stencil" : "",
              width, height, ok ? "" : " INCOMPLETE - laser depth off");
    g_depth_format = format;
    g_width = width;
    g_height = height;
    return ok;
}

}  // namespace

void set_camera(const float eye_from_game[16], const float eye_projection[16]) {
    std::memcpy(g_eye, eye_from_game, sizeof g_eye);
    std::memcpy(g_proj, eye_projection, sizeof g_proj);
    g_have_camera = true;
}

void set_ray(const Vec3& origin, const Vec3& dir) {
    g_origin = origin;
    g_dir = dir;
    g_have_ray = true;
}

void clear_ray() {
    if (g_have_ray) ++g_epoch;
    g_have_ray = false;
    g_latest = Result::None;
}

void run(int width, int height) {
    const unsigned scene = stereo::scene_framebuffer();
    if (g_failed || !g_have_ray || !g_have_camera || scene == 0 || width <= 0 || height <= 0) return;
    Slot& slot = g_slots[g_next_slot];
    if (slot.fence) return;  // every readback still in flight: the GPU is behind, skip this frame

    const gl::Api& a = gl::api();
    gl::StateGuard guard;
    gl::StateGuard::neutral();
    gl::GLint pack_buffer = 0, pack_align = 4, pack_row = 0;
    a.GetIntegerv(gl::PIXEL_PACK_BUFFER_BINDING, &pack_buffer);
    a.GetIntegerv(gl::PACK_ALIGNMENT, &pack_align);
    a.GetIntegerv(gl::PACK_ROW_LENGTH, &pack_row);

    if (!g_program && !make_result_target()) {
        g_failed = true;
        a.BindBuffer(gl::PIXEL_PACK_BUFFER, static_cast<gl::GLuint>(pack_buffer));
        return;
    }
    a.BindFramebuffer(gl::READ_FRAMEBUFFER, scene);
    if (!ensure_depth_copy(width, height)) {
        if (g_logged_scene != scene) HW2VR_LOG("depth: framebuffer %u has no usable depth - no laser depth", scene);
        g_logged_scene = scene;
        a.BindBuffer(gl::PIXEL_PACK_BUFFER, static_cast<gl::GLuint>(pack_buffer));
        return;
    }
    if (g_logged_scene != scene) HW2VR_LOG("depth: laser tested against framebuffer %u", scene);
    g_logged_scene = scene;
    a.BindFramebuffer(gl::DRAW_FRAMEBUFFER, g_depth_fbo);
    a.BlitFramebuffer(0, 0, width, height, 0, 0, width, height, gl::DEPTH_BUFFER_BIT, gl::NEAREST);  // resolves MSAA

    a.BindFramebuffer(gl::DRAW_FRAMEBUFFER, g_result_fbo);
    a.Viewport(0, 0, 1, 1);
    a.UseProgram(g_program);
    a.BindVertexArray(g_vao);
    a.ActiveTexture(gl::TEXTURE0);
    a.BindTexture(gl::TEXTURE_2D, g_depth_tex);
    a.BindSampler(0, 0);
    a.Uniform1i(g_loc_depth, 0);
    a.Uniform3f(g_loc_origin, g_origin.x, g_origin.y, g_origin.z);
    a.Uniform3f(g_loc_dir, g_dir.x, g_dir.y, g_dir.z);
    a.UniformMatrix4fv(g_loc_eye, 1, 0, g_eye);
    a.UniformMatrix4fv(g_loc_proj, 1, 0, g_proj);
    a.Uniform2f(g_loc_range, 1.0f, 400000.0f);  // game units: from 1 unit off the hand to beyond the far clip
    a.DrawArrays(gl::TRIANGLES, 0, 3);

    a.BindFramebuffer(gl::READ_FRAMEBUFFER, g_result_fbo);
    a.ReadBuffer(gl::COLOR_ATTACHMENT0);
    a.BindBuffer(gl::PIXEL_PACK_BUFFER, slot.pbo);
    a.PixelStorei(gl::PACK_ALIGNMENT, 4);
    a.PixelStorei(gl::PACK_ROW_LENGTH, 0);
    a.ReadPixels(0, 0, 1, 1, gl::RGBA, gl::FLOAT, nullptr);
    slot.fence = a.FenceSync(gl::SYNC_GPU_COMMANDS_COMPLETE, 0);
    slot.sequence = ++g_sequence;
    slot.epoch = g_epoch;
    g_next_slot = (g_next_slot + 1) % kSlots;
    ++g_probes;

    a.PixelStorei(gl::PACK_ALIGNMENT, pack_align);
    a.PixelStorei(gl::PACK_ROW_LENGTH, pack_row);
    a.BindBuffer(gl::PIXEL_PACK_BUFFER, static_cast<gl::GLuint>(pack_buffer));
}

Result latest(float* distance) {
    const gl::Api& a = gl::api();
    gl::GLint pack_buffer = -1;
    for (Slot& s : g_slots) {
        if (!s.fence) continue;
        const gl::GLenum st = a.ClientWaitSync(s.fence, 0, 0);  // never waits
        if (st != gl::ALREADY_SIGNALED && st != gl::CONDITION_SATISFIED) continue;
        if (pack_buffer < 0) a.GetIntegerv(gl::PIXEL_PACK_BUFFER_BINDING, &pack_buffer);
        a.BindBuffer(gl::PIXEL_PACK_BUFFER, s.pbo);
        if (const auto* p = static_cast<const float*>(a.MapBufferRange(gl::PIXEL_PACK_BUFFER, 0, 4 * sizeof(float), gl::MAP_READ_BIT))) {
            if (s.epoch == g_epoch && s.sequence > g_latest_sequence) {
                g_latest_sequence = s.sequence;
                g_latest = p[1] > 0.5f ? Result::Hit : Result::Miss;
                g_latest_distance = p[0];
            }
            if (p[1] > 0.5f) ++g_hits;
            a.UnmapBuffer(gl::PIXEL_PACK_BUFFER);
            ++g_results;
        }
        a.DeleteSync(s.fence);
        s.fence = nullptr;
    }
    if (pack_buffer >= 0) a.BindBuffer(gl::PIXEL_PACK_BUFFER, static_cast<gl::GLuint>(pack_buffer));
    *distance = g_latest_distance;
    return g_latest;
}

void release() {
    // The full-size depth copy goes; the small result target, shader and readback buffers stay for the next start.
    const gl::Api& a = gl::api();
    if (!a.DeleteTextures) return;
    for (Slot& s : g_slots)
        if (s.fence) a.DeleteSync(s.fence), s.fence = nullptr;
    if (g_depth_tex) a.DeleteTextures(1, &g_depth_tex), g_depth_tex = 0;
    if (g_depth_fbo) a.DeleteFramebuffers(1, &g_depth_fbo), g_depth_fbo = 0;
    g_depth_format = 0;
    g_width = g_height = 0;
    g_have_ray = g_have_camera = false;
    g_latest = Result::None;
}

TestState test_state() { return {g_probes, g_results, g_hits}; }

}  // namespace hw2vr::depth
