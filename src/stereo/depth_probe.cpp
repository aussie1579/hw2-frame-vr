#include "stereo/depth_probe.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>

#include "common/log.h"
#include "common/mat4.h"
#include "proxy/gl_api.h"
#include "stereo/eye_math.h"
#include "stereo/view_override.h"

namespace hw2vr::depth {
namespace {

constexpr int kSlots = 3;      // readbacks in flight, per kind
constexpr int kCellPixels = 20;  // marker grid cell size in game-screen pixels
constexpr gl::GLenum kR32F = 0x822E, kRed = 0x1903;

struct Slot {
    gl::GLuint pbo = 0;
    void* fence = nullptr;
    unsigned sequence = 0;
    unsigned epoch = 0;
    int width = 0, height = 0;  // grid slots: the grid's size
    float game_proj[4] = {};    // grid slots: P0, P5, P8, P9 the rays were built from
    float turn[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};  // grid slots: the game camera's turn then
};

// A render target + the readbacks of what's drawn into it.
struct Target {
    gl::GLuint tex = 0, fbo = 0;
    int width = 0, height = 0;
    Slot slots[kSlots];
    int next = 0;
};
Target g_laser, g_grid_target;

unsigned g_sequence = 0, g_latest_sequence = 0, g_grid_sequence = 0;
unsigned g_epoch = 0;  // bumped by clear_ray: results for an earlier ray are dropped

float g_eye[16], g_proj[16];
bool g_have_camera = false;
Vec3 g_origin, g_dir;
bool g_have_ray = false;

gl::GLuint g_depth_tex = 0, g_depth_fbo = 0, g_vao = 0;
gl::GLuint g_laser_program = 0, g_grid_program = 0;
gl::GLenum g_depth_format = 0;
int g_width = 0, g_height = 0;
bool g_failed = false;
unsigned g_logged_scene = 0;

Result g_latest = Result::None;
float g_latest_distance = 0.0f;
unsigned g_probes = 0, g_results = 0, g_hits = 0, g_grids = 0;

std::vector<float> g_grid;  // distances along each cell's ray (game units), < 0 = nothing; row 0 = screen bottom
int g_grid_w = 0, g_grid_h = 0;
float g_grid_proj[4] = {};
// The game camera turns with the head (engine hook), so the camera the grid was traced from (a frame or two ago) and
// the one this frame's markers were placed with differ by the head's turn in between.
float g_turn[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
float g_grid_turn[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};

constexpr const char* kVs = R"(#version 330 core
void main() {
    vec2 p = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));
    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
}
)";
// Log-spaced steps from u_range.x to u_range.y along a ray; a step counts as a hit when the point is behind the
// visible surface but by no more than a quarter of that surface's depth (so passing behind a ship isn't a hit),
// then the crossing is refined by bisection. Depths are eye-space (-z); the eye projection's depth row converts.
constexpr const char* kMarch = R"(#version 330 core
uniform sampler2D u_depth;
uniform mat4 u_eye;
uniform mat4 u_proj;
uniform vec2 u_range;
out vec4 o_result;

bool probe(vec3 point, out float point_depth, out float surface_depth) {
    vec4 view = u_eye * vec4(point, 1.0);
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

float march(vec3 origin, vec3 dir, int steps) {
    const float kThickness = 0.25;
    float ratio = u_range.y / u_range.x;
    float previous = u_range.x;
    for (int i = 0; i < steps; ++i) {
        float t = u_range.x * pow(ratio, float(i) / float(steps - 1));
        float pd, sd;
        if (probe(origin + dir * t, pd, sd) && pd >= sd && pd - sd <= kThickness * sd) {
            float a = previous, b = t;
            for (int k = 0; k < 14; ++k) {
                float m = 0.5 * (a + b);
                float mpd, msd;
                if (probe(origin + dir * m, mpd, msd) && mpd >= msd) b = m; else a = m;
            }
            return b;
        }
        previous = t;
    }
    return -1.0;
}
)";
constexpr const char* kLaserMain = R"(
uniform vec3 u_origin;
uniform vec3 u_dir;
void main() {
    float t = march(u_origin, u_dir, 192);
    o_result = vec4(t, t >= 0.0 ? 1.0 : 0.0, 0.0, 1.0);
}
)";
// One cell per fragment: the game camera's ray through the cell's centre (u_game = P0, P5, P8, P9).
constexpr const char* kGridMain = R"(
uniform vec4 u_game;
uniform vec2 u_cells;
void main() {
    vec2 ndc = gl_FragCoord.xy / u_cells * 2.0 - 1.0;
    vec3 dir = normalize(vec3((ndc.x + u_game.z) / u_game.x, (ndc.y + u_game.w) / u_game.y, -1.0));
    o_result = vec4(march(vec3(0.0), dir, 128), 0.0, 0.0, 1.0);
}
)";

bool make_target(Target& t, int width, int height, gl::GLenum internal_format, gl::GLenum format, int bytes_per_texel) {
    const gl::Api& a = gl::api();
    if (!t.tex) a.GenTextures(1, &t.tex);
    a.BindTexture(gl::TEXTURE_2D, t.tex);
    a.TexImage2D(gl::TEXTURE_2D, 0, static_cast<gl::GLint>(internal_format), width, height, 0, format, gl::FLOAT, nullptr);
    a.TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_MIN_FILTER, gl::NEAREST);
    a.TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_MAG_FILTER, gl::NEAREST);
    if (!t.fbo) a.GenFramebuffers(1, &t.fbo);
    a.BindFramebuffer(gl::DRAW_FRAMEBUFFER, t.fbo);
    a.FramebufferTexture2D(gl::DRAW_FRAMEBUFFER, gl::COLOR_ATTACHMENT0, gl::TEXTURE_2D, t.tex, 0);
    if (a.CheckFramebufferStatus(gl::DRAW_FRAMEBUFFER) != gl::FRAMEBUFFER_COMPLETE) return false;
    for (Slot& s : t.slots) {
        if (s.fence) a.DeleteSync(s.fence), s.fence = nullptr;
        if (!s.pbo) a.GenBuffers(1, &s.pbo);
        a.BindBuffer(gl::PIXEL_PACK_BUFFER, s.pbo);
        a.BufferData(gl::PIXEL_PACK_BUFFER, static_cast<std::ptrdiff_t>(width) * height * bytes_per_texel, nullptr,
                     gl::STREAM_READ);
    }
    t.width = width;
    t.height = height;
    t.next = 0;
    return true;
}

bool make_programs() {
    const std::string laser = std::string(kMarch) + kLaserMain, grid = std::string(kMarch) + kGridMain;
    g_laser_program = gl::build_program(kVs, laser.c_str(), "laser depth");
    g_grid_program = gl::build_program(kVs, grid.c_str(), "marker depth grid");
    if (!g_laser_program || !g_grid_program) return false;
    gl::api().GenVertexArrays(1, &g_vao);
    return make_target(g_laser, 1, 1, gl::RGBA32F, gl::RGBA, 16);
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

// Runs `program` into `t` and starts reading the result back into its next slot. False if every slot is in flight.
Slot* draw_and_read(Target& t, gl::GLuint program, gl::GLenum format, gl::GLenum type) {
    Slot& slot = t.slots[t.next];
    if (slot.fence) return nullptr;
    const gl::Api& a = gl::api();
    a.BindFramebuffer(gl::DRAW_FRAMEBUFFER, t.fbo);
    a.Viewport(0, 0, t.width, t.height);
    a.UseProgram(program);
    a.Uniform1i(a.GetUniformLocation(program, "u_depth"), 0);
    a.UniformMatrix4fv(a.GetUniformLocation(program, "u_eye"), 1, 0, g_eye);
    a.UniformMatrix4fv(a.GetUniformLocation(program, "u_proj"), 1, 0, g_proj);
    a.Uniform2f(a.GetUniformLocation(program, "u_range"), 1.0f, 400000.0f);  // game units, past the far clip
    a.DrawArrays(gl::TRIANGLES, 0, 3);
    a.BindFramebuffer(gl::READ_FRAMEBUFFER, t.fbo);
    a.ReadBuffer(gl::COLOR_ATTACHMENT0);
    a.BindBuffer(gl::PIXEL_PACK_BUFFER, slot.pbo);
    a.ReadPixels(0, 0, t.width, t.height, format, type, nullptr);
    slot.fence = a.FenceSync(gl::SYNC_GPU_COMMANDS_COMPLETE, 0);
    slot.sequence = ++g_sequence;
    slot.width = t.width;
    slot.height = t.height;
    t.next = (t.next + 1) % kSlots;
    return &slot;
}

// Maps a finished slot (fence signalled, never waits) and hands its bytes to `use`.
template <class Use>
void collect(Target& t, std::ptrdiff_t bytes_per_texel, Use use) {
    const gl::Api& a = gl::api();
    gl::GLint pack_buffer = -1;
    for (Slot& s : t.slots) {
        if (!s.fence) continue;
        const gl::GLenum st = a.ClientWaitSync(s.fence, 0, 0);
        if (st != gl::ALREADY_SIGNALED && st != gl::CONDITION_SATISFIED) continue;
        if (pack_buffer < 0) a.GetIntegerv(gl::PIXEL_PACK_BUFFER_BINDING, &pack_buffer);
        a.BindBuffer(gl::PIXEL_PACK_BUFFER, s.pbo);
        const std::ptrdiff_t size = static_cast<std::ptrdiff_t>(s.width) * s.height * bytes_per_texel;
        if (const void* p = a.MapBufferRange(gl::PIXEL_PACK_BUFFER, 0, size, gl::MAP_READ_BIT)) {
            use(s, static_cast<const float*>(p));
            a.UnmapBuffer(gl::PIXEL_PACK_BUFFER);
        }
        a.DeleteSync(s.fence);
        s.fence = nullptr;
    }
    if (pack_buffer >= 0) a.BindBuffer(gl::PIXEL_PACK_BUFFER, static_cast<gl::GLuint>(pack_buffer));
}

}  // namespace

void set_camera(const float eye_from_game[16], const float eye_projection[16]) {
    std::memcpy(g_eye, eye_from_game, sizeof g_eye);
    std::memcpy(g_proj, eye_projection, sizeof g_proj);
    g_have_camera = true;
}

void set_game_turn(const float turn[16]) { std::memcpy(g_turn, turn, sizeof g_turn); }
const float* game_turn() { return g_turn; }

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
    const float* game_proj = stereo::game_projection();
    if (g_failed || !g_have_camera || scene == 0 || width <= 0 || height <= 0) return;

    const gl::Api& a = gl::api();
    gl::StateGuard guard;
    gl::StateGuard::neutral();
    gl::GLint pack_buffer = 0, pack_align = 4, pack_row = 0;
    a.GetIntegerv(gl::PIXEL_PACK_BUFFER_BINDING, &pack_buffer);
    a.GetIntegerv(gl::PACK_ALIGNMENT, &pack_align);
    a.GetIntegerv(gl::PACK_ROW_LENGTH, &pack_row);
    auto restore_pack = [&] {
        a.PixelStorei(gl::PACK_ALIGNMENT, pack_align);
        a.PixelStorei(gl::PACK_ROW_LENGTH, pack_row);
        a.BindBuffer(gl::PIXEL_PACK_BUFFER, static_cast<gl::GLuint>(pack_buffer));
    };

    if (!g_laser_program && !make_programs()) {
        HW2VR_LOG("depth: setup failed - no laser depth or marker depth");
        g_failed = true;
        restore_pack();
        return;
    }
    a.BindFramebuffer(gl::READ_FRAMEBUFFER, scene);
    if (!ensure_depth_copy(width, height)) {
        if (g_logged_scene != scene) HW2VR_LOG("depth: framebuffer %u has no usable depth - no laser depth", scene);
        g_logged_scene = scene;
        restore_pack();
        return;
    }
    if (g_logged_scene != scene) HW2VR_LOG("depth: laser and markers tested against framebuffer %u", scene);
    g_logged_scene = scene;
    a.BindFramebuffer(gl::DRAW_FRAMEBUFFER, g_depth_fbo);
    a.BlitFramebuffer(0, 0, width, height, 0, 0, width, height, gl::DEPTH_BUFFER_BIT, gl::NEAREST);  // resolves MSAA

    a.BindVertexArray(g_vao);
    a.ActiveTexture(gl::TEXTURE0);
    a.BindTexture(gl::TEXTURE_2D, g_depth_tex);
    a.BindSampler(0, 0);
    a.PixelStorei(gl::PACK_ALIGNMENT, 4);
    a.PixelStorei(gl::PACK_ROW_LENGTH, 0);

    if (g_have_ray) {
        a.UseProgram(g_laser_program);
        a.Uniform3f(a.GetUniformLocation(g_laser_program, "u_origin"), g_origin.x, g_origin.y, g_origin.z);
        a.Uniform3f(a.GetUniformLocation(g_laser_program, "u_dir"), g_dir.x, g_dir.y, g_dir.z);
        if (Slot* s = draw_and_read(g_laser, g_laser_program, gl::RGBA, gl::FLOAT)) {
            s->epoch = g_epoch;
            ++g_probes;
        }
    }
    if (game_proj) {
        const int cw = (width + kCellPixels - 1) / kCellPixels, ch = (height + kCellPixels - 1) / kCellPixels;
        if (cw != g_grid_target.width || ch != g_grid_target.height) {
            if (!make_target(g_grid_target, cw, ch, kR32F, kRed, 4)) HW2VR_LOG("depth: marker grid target incomplete");
            else HW2VR_LOG("depth: marker grid %dx%d cells", cw, ch);
        }
        a.UseProgram(g_grid_program);
        const float game[4] = {game_proj[0], game_proj[5], game_proj[8], game_proj[9]};
        a.Uniform4f(a.GetUniformLocation(g_grid_program, "u_game"), game[0], game[1], game[2], game[3]);
        a.Uniform2f(a.GetUniformLocation(g_grid_program, "u_cells"), static_cast<float>(cw), static_cast<float>(ch));
        if (Slot* s = draw_and_read(g_grid_target, g_grid_program, kRed, gl::FLOAT)) {
            std::memcpy(s->game_proj, game, sizeof game);
            std::memcpy(s->turn, g_turn, sizeof g_turn);
        }
    }
    restore_pack();
}

Result latest(float* distance) {
    collect(g_laser, 16, [](const Slot& s, const float* p) {
        if (s.epoch == g_epoch && s.sequence > g_latest_sequence) {
            g_latest_sequence = s.sequence;
            g_latest = p[1] > 0.5f ? Result::Hit : Result::Miss;
            g_latest_distance = p[0];
        }
        if (p[1] > 0.5f) ++g_hits;
        ++g_results;
    });
    *distance = g_latest_distance;
    return g_latest;
}

void poll_grid() {
    collect(g_grid_target, 4, [](const Slot& s, const float* p) {
        if (s.sequence <= g_grid_sequence) return;
        g_grid_sequence = s.sequence;
        g_grid.assign(p, p + static_cast<size_t>(s.width) * s.height);
        g_grid_w = s.width;
        g_grid_h = s.height;
        std::memcpy(g_grid_proj, s.game_proj, sizeof g_grid_proj);
        std::memcpy(g_grid_turn, s.turn, sizeof g_grid_turn);
        ++g_grids;
    });
}

bool anchor_view_depth(const float rect[4], float* view_depth) {
    if (g_grid.empty() || g_grid_w <= 0 || g_grid_h <= 0) return false;
    // The rectangle as the grid's camera saw it: turned by grid_turn^-1 * turn (identity when the camera didn't turn).
    float rot[16], grid_inverse[16], b[4];
    mat4::rigid_inverse(g_grid_turn, grid_inverse);
    mat4::multiply(grid_inverse, g_turn, rot);
    if (!stereo::turn_screen_rect(rect, g_grid_proj, rot, b)) return false;
    // Nearest surface in a cell rectangle; view depth from the distance along that cell's ray.
    auto nearest = [](float x0, float y0, float x1, float y1, float* z) {
        const int i0 = std::clamp(static_cast<int>(std::floor(x0 * g_grid_w)), 0, g_grid_w - 1);
        const int i1 = std::clamp(static_cast<int>(std::floor(x1 * g_grid_w)), 0, g_grid_w - 1);
        const int j0 = std::clamp(static_cast<int>(std::floor(y0 * g_grid_h)), 0, g_grid_h - 1);
        const int j1 = std::clamp(static_cast<int>(std::floor(y1 * g_grid_h)), 0, g_grid_h - 1);
        float best = 1e30f;
        for (int j = j0; j <= j1; ++j)
            for (int i = i0; i <= i1; ++i) {
                const float d = g_grid[static_cast<size_t>(j) * g_grid_w + i];
                if (d <= 0.0f) continue;
                const float nx = (i + 0.5f) / g_grid_w * 2.0f - 1.0f, ny = (j + 0.5f) / g_grid_h * 2.0f - 1.0f;
                const float vx = (nx + g_grid_proj[2]) / g_grid_proj[0], vy = (ny + g_grid_proj[3]) / g_grid_proj[1];
                best = std::min(best, d / std::sqrt(vx * vx + vy * vy + 1.0f));
            }
        if (best >= 1e30f) return false;
        *z = best;
        return true;
    };
    const float cx = 0.5f * (b[0] + b[2]), cy = 0.5f * (b[1] + b[3]);
    const float hw = 0.25f * (b[2] - b[0]), hh = 0.25f * (b[3] - b[1]);
    if (nearest(cx - hw, cy - hh, cx + hw, cy + hh, view_depth)) return true;
    const float mx = std::max(0.1f * (b[2] - b[0]), 1.0f / g_grid_w), my = std::max(0.1f * (b[3] - b[1]), 1.0f / g_grid_h);
    return nearest(b[0] - mx, b[1] - my, b[2] + mx, b[3] + my, view_depth);
}

void release() {
    // The full-size depth copy goes; the small targets, shaders and readback buffers stay for the next start.
    const gl::Api& a = gl::api();
    if (!a.DeleteTextures) return;
    for (Target* t : {&g_laser, &g_grid_target})
        for (Slot& s : t->slots)
            if (s.fence) a.DeleteSync(s.fence), s.fence = nullptr;
    if (g_depth_tex) a.DeleteTextures(1, &g_depth_tex), g_depth_tex = 0;
    if (g_depth_fbo) a.DeleteFramebuffers(1, &g_depth_fbo), g_depth_fbo = 0;
    g_depth_format = 0;
    g_width = g_height = 0;
    g_have_ray = g_have_camera = false;
    g_latest = Result::None;
    g_grid.clear();
    g_grid_w = g_grid_h = 0;
}

TestState test_state() { return {g_probes, g_results, g_hits, g_grids}; }

}  // namespace hw2vr::depth
