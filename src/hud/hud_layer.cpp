#include "hud/hud_layer.h"

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "common/log.h"
#include "proxy/engine_hooks.h"
#include "proxy/gl_api.h"
#include "stereo/depth_probe.h"

namespace hw2vr::hud {
namespace {

constexpr unsigned kMinOrthoUploads = 3;  // fewer than this in a frame: no HUD framebuffer this frame
constexpr unsigned kStableFrames = 10;    // same winner this many frames in a row before we trust it
constexpr unsigned kLoseAfterFrames = 120;

std::unordered_map<unsigned, unsigned> g_ortho_counts;  // draw fbo -> ortho inMatP uploads this frame
unsigned g_hud_fbo = 0;                                 // 0 = not identified
unsigned g_candidate = 0, g_candidate_streak = 0, g_frames_without = 0;

bool g_capture = false, g_premultiplied = true;
bool g_ready = false;  // overlay target cleared for this frame (the game has entered its HUD pass)
bool g_replaying = false;
int g_width = 0, g_height = 0;
gl::GLuint g_tex = 0, g_fbo = 0;
int g_tex_width = 0, g_tex_height = 0;
std::unordered_set<unsigned> g_ui_programs;  // drew 2D into the HUD framebuffer this frame
unsigned g_overlay_frames = 0, g_redirected = 0, g_cursor_draws = 0;
gl::GLint g_saved_blend[4];
gl::GLint g_saved_viewport[4];
gl::GLboolean g_saved_scissor = 0;

// World-space markers (2D draws before the game's PHASE_UIIn): placed at their ship's depth in each eye. The
// classification and depth of every 2D HUD draw are kept in order, so the second-eye replay repeats them exactly.
bool g_world_markers = true;
std::vector<bool> g_kinds;    // per 2D HUD draw this frame: true = world marker
std::vector<float> g_depths;  // per world marker this frame: view depth (game units)
size_t g_kind_replay = 0, g_depth_replay = 0;
float g_frame_depth = 0.0f, g_last_depth = 0.0f;  // last anchored depth this frame / ever
constexpr float kDefaultMarkerDepth = 2000.0f;    // game units (20 m at the default world scale)
unsigned g_world_draws = 0, g_world_anchored = 0;

HWND g_window = nullptr;
int g_cursor_override_x = -1, g_cursor_override_y = -1;
gl::GLuint g_cursor_tex = 0, g_cursor_fbo = 0;
Cursor g_cursor, g_last_cursor;

// Panel filter: the overlay's mipmaps sampled 2x2 per output pixel one level above the output size (a box filter).
gl::GLuint g_panel_tex = 0, g_panel_fbo = 0, g_panel_program = 0, g_panel_vao = 0;
int g_panel_width = 0, g_panel_height = 0;
gl::GLint g_loc_src = -1, g_loc_texel = -1, g_loc_lod = -1, g_loc_backing = -1;
bool g_panel_failed = false;

// UI coverage mask: alpha of an overlay mip level, read into a ring of pixel-pack buffers and mapped a frame later.
constexpr int kMaskLevel = 3;  // 1/8 per side
gl::GLuint g_mask_fbo = 0, g_mask_pbo[2] = {};
int g_mask_pbo_w[2] = {}, g_mask_pbo_h[2] = {};
bool g_mask_pending[2] = {};
int g_mask_next = 0;
std::vector<unsigned char> g_mask;
int g_mask_w = 0, g_mask_h = 0;

constexpr const char* kPanelVs = R"(#version 330 core
out vec2 v_uv;
void main() {
    vec2 p = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));
    v_uv = p;
    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
}
)";
constexpr const char* kPanelFs = R"(#version 330 core
in vec2 v_uv;
uniform sampler2D u_src;
uniform vec2 u_texel;  // one output pixel in uv
uniform float u_lod;
uniform vec4 u_backing;  // premultiplied colour behind the UI (console)
out vec4 o_color;
void main() {
    vec2 d = u_texel * 0.25;
    vec4 ui = 0.25 * (textureLod(u_src, v_uv + vec2(-d.x, -d.y), u_lod) + textureLod(u_src, v_uv + vec2(d.x, -d.y), u_lod) +
                      textureLod(u_src, v_uv + vec2(-d.x, d.y), u_lod) + textureLod(u_src, v_uv + vec2(d.x, d.y), u_lod));
    o_color = ui + (1.0 - ui.a) * u_backing;
}
)";

// A colour texture + framebuffer pair, (re)allocated at the given size.
bool ensure_rgba_target(gl::GLuint* tex, gl::GLuint* fbo, int w, int h, const char* what) {
    const gl::Api& a = gl::api();
    if (!*tex) a.GenTextures(1, tex);
    a.BindTexture(gl::TEXTURE_2D, *tex);
    a.TexImage2D(gl::TEXTURE_2D, 0, gl::RGBA8, w, h, 0, gl::RGBA, gl::UNSIGNED_BYTE, nullptr);
    a.TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_MIN_FILTER, gl::LINEAR);
    a.TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_MAG_FILTER, gl::LINEAR);
    a.TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_WRAP_S, gl::CLAMP_TO_EDGE);
    a.TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_WRAP_T, gl::CLAMP_TO_EDGE);
    if (!*fbo) a.GenFramebuffers(1, fbo);
    a.BindFramebuffer(gl::DRAW_FRAMEBUFFER, *fbo);
    a.FramebufferTexture2D(gl::DRAW_FRAMEBUFFER, gl::COLOR_ATTACHMENT0, gl::TEXTURE_2D, *tex, 0);
    const bool ok = a.CheckFramebufferStatus(gl::DRAW_FRAMEBUFFER) == gl::FRAMEBUFFER_COMPLETE;
    HW2VR_LOG("hud: %s target %dx%d%s", what, w, h, ok ? "" : " is incomplete");
    return ok;
}

void clear_target(gl::GLuint fbo) {
    const gl::Api& a = gl::api();
    a.BindFramebuffer(gl::DRAW_FRAMEBUFFER, fbo);
    a.ClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    a.Clear(gl::COLOR_BUFFER_BIT);
}

bool ensure_target() {
    if (g_tex && g_tex_width == g_width && g_tex_height == g_height) return true;
    if (g_width <= 0 || g_height <= 0) return false;
    const bool ok = ensure_rgba_target(&g_tex, &g_fbo, g_width, g_height, "overlay");
    gl::api().TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_MIN_FILTER, gl::LINEAR_MIPMAP_LINEAR);  // panel filter
    g_tex_width = g_width;
    g_tex_height = g_height;
    return ok;
}

// The hotspot for this frame's cursor texture: where the cursor is now, in client pixels.
void locate_cursor() {
    if (g_cursor_override_x >= 0) {
        g_cursor.x = g_cursor_override_x;
        g_cursor.y = g_cursor_override_y;
        return;
    }
    POINT p{};
    if (g_window && GetCursorPos(&p) && ScreenToClient(g_window, &p)) g_cursor.x = p.x, g_cursor.y = p.y;
}

// Start of this frame's HUD pass: an empty overlay to redirect the 2D draws into.
void begin_overlay_frame() {
    gl::StateGuard guard;
    gl::StateGuard::neutral();
    if (!ensure_target()) return;
    const gl::Api& a = gl::api();
    a.BindFramebuffer(gl::DRAW_FRAMEBUFFER, g_fbo);
    a.ClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    a.Clear(gl::COLOR_BUFFER_BIT);
    g_ready = true;
    ++g_overlay_frames;
}

void update_ui_mask();

}  // namespace

void note_ortho_projection(unsigned draw_fbo) {
    if (!g_replaying) ++g_ortho_counts[draw_fbo];
}

bool is_hud_fbo(unsigned fbo) { return fbo != 0 && fbo == g_hud_fbo; }

void set_capture(bool enabled, bool premultiplied) {
    g_capture = enabled;
    g_premultiplied = premultiplied;
}

void set_target_size(int width, int height) {
    g_width = width;
    g_height = height;
}

void set_window(void* hwnd) { g_window = static_cast<HWND>(hwnd); }
void set_cursor_override(int x, int y) { g_cursor_override_x = x, g_cursor_override_y = y; }
const Cursor& current_cursor() { return g_cursor; }
const Cursor& last_cursor() { return g_last_cursor; }

void on_draw_framebuffer_change(unsigned /*from*/, unsigned to) {
    if (g_capture && g_premultiplied && !g_ready && !g_replaying && is_hud_fbo(to)) begin_overlay_frame();
}

void end_frame() {
    unsigned winner = 0, best = 0;
    for (const auto& [fbo, count] : g_ortho_counts)
        if (fbo != 0 && count > best) winner = fbo, best = count;
    g_ortho_counts.clear();
    g_ui_programs.clear();
    g_ready = false;
    g_last_cursor = g_cursor;
    g_cursor.drawn = false;
    g_kinds.clear();
    g_depths.clear();
    g_kind_replay = g_depth_replay = 0;
    g_frame_depth = 0.0f;

    if (best < kMinOrthoUploads) winner = 0;
    if (winner == 0) {
        if (g_hud_fbo && ++g_frames_without >= kLoseAfterFrames) {
            HW2VR_LOG("hud: lost the HUD framebuffer (%u)", g_hud_fbo);
            g_hud_fbo = 0;
        }
        return;
    }
    g_frames_without = 0;
    g_candidate_streak = (winner == g_candidate) ? g_candidate_streak + 1 : 1;
    g_candidate = winner;
    if (winner != g_hud_fbo && g_candidate_streak >= kStableFrames) {
        HW2VR_LOG("hud: HUD framebuffer is %u (%u ortho uploads per frame)", winner, best);
        g_hud_fbo = winner;
    }
}

bool overlay_ready() { return g_ready; }
unsigned overlay_texture() { return g_tex; }
int overlay_width() { return g_tex ? g_tex_width : 0; }
int overlay_height() { return g_tex ? g_tex_height : 0; }

DrawAction draw_action(unsigned draw_fbo, unsigned program, bool is_2d) {
    if (!g_ready) return DrawAction::Draw;
    if (is_hud_fbo(draw_fbo)) {
        if (!is_2d) return DrawAction::Draw;  // 3D overlay stays in the game's HUD layer, drawn per eye
        if (g_replaying) {
            const bool world = g_kind_replay < g_kinds.size() && g_kinds[g_kind_replay];
            ++g_kind_replay;
            return world ? DrawAction::PlaceAtDepth : DrawAction::Skip;
        }
        g_ui_programs.insert(program);
        // World-space HUD layers come before the game runs PHASE_UIIn (proxy/engine_hooks.h).
        const bool world = g_world_markers && engine::installed() && !engine::screen_ui_phase();
        g_kinds.push_back(world);
        return world ? DrawAction::PlaceAtDepth : DrawAction::Redirect;
    }
    if (draw_fbo == 0 && g_ui_programs.count(program) != 0)  // UI drawn after the composite: the cursor
        return g_replaying ? DrawAction::Skip : DrawAction::RedirectCursor;
    return DrawAction::Draw;
}

float world_marker_depth(const float* bounds) {
    if (g_replaying) return g_depth_replay < g_depths.size() ? g_depths[g_depth_replay++] : g_last_depth;
    float z = 0.0f;
    ++g_world_draws;
    if (bounds && depth::anchor_view_depth(bounds, &z)) {
        g_frame_depth = g_last_depth = z;
        ++g_world_anchored;
    } else {
        // No surface behind it (a health bar above its ship, a label beside it): the marker drawn just before it
        // belongs to the same ship.
        z = g_frame_depth > 0.0f ? g_frame_depth : g_last_depth > 0.0f ? g_last_depth : kDefaultMarkerDepth;
    }
    g_depths.push_back(z);
    return z;
}

void set_world_markers(bool on) { g_world_markers = on; }
bool replaying() { return g_replaying; }

void set_replaying(bool on) {
    g_replaying = on;
    g_kind_replay = g_depth_replay = 0;
}

void begin_redirect(unsigned /*draw_fbo*/) {
    const gl::Api& a = gl::api();
    a.GetIntegerv(gl::BLEND_SRC_RGB, &g_saved_blend[0]);
    a.GetIntegerv(gl::BLEND_DST_RGB, &g_saved_blend[1]);
    a.GetIntegerv(gl::BLEND_SRC_ALPHA, &g_saved_blend[2]);
    a.GetIntegerv(gl::BLEND_DST_ALPHA, &g_saved_blend[3]);
    a.BindFramebuffer(gl::DRAW_FRAMEBUFFER, g_fbo);
    // The game's alpha factors write transmittance; the overlay holds coverage. Colour factors stay the game's, so
    // the overlay accumulates premultiplied colour.
    a.BlendFuncSeparate(static_cast<gl::GLenum>(g_saved_blend[0]), static_cast<gl::GLenum>(g_saved_blend[1]), gl::ONE,
                        gl::ONE_MINUS_SRC_ALPHA);
    ++g_redirected;
}

void end_redirect(unsigned draw_fbo) {
    const gl::Api& a = gl::api();
    a.BlendFuncSeparate(static_cast<gl::GLenum>(g_saved_blend[0]), static_cast<gl::GLenum>(g_saved_blend[1]),
                        static_cast<gl::GLenum>(g_saved_blend[2]), static_cast<gl::GLenum>(g_saved_blend[3]));
    a.BindFramebuffer(gl::DRAW_FRAMEBUFFER, draw_fbo);
}

bool begin_cursor_redirect() {
    const gl::Api& a = gl::api();
    if (!g_cursor.drawn) {  // first cursor draw this frame: an empty target round the hotspot
        gl::StateGuard guard;
        gl::StateGuard::neutral();
        if (!g_cursor_fbo && !ensure_rgba_target(&g_cursor_tex, &g_cursor_fbo, kCursorSize, kCursorSize, "cursor")) {
            a.DeleteFramebuffers(1, &g_cursor_fbo);  // retry next frame
            g_cursor_fbo = 0;
            return false;
        }
        clear_target(g_cursor_fbo);
        locate_cursor();
        g_cursor.drawn = true;
        g_cursor.texture = g_cursor_tex;
    }
    a.GetIntegerv(gl::BLEND_SRC_RGB, &g_saved_blend[0]);
    a.GetIntegerv(gl::BLEND_DST_RGB, &g_saved_blend[1]);
    a.GetIntegerv(gl::BLEND_SRC_ALPHA, &g_saved_blend[2]);
    a.GetIntegerv(gl::BLEND_DST_ALPHA, &g_saved_blend[3]);
    a.GetIntegerv(gl::VIEWPORT, g_saved_viewport);
    g_saved_scissor = a.IsEnabled(gl::SCISSOR_TEST);
    a.BindFramebuffer(gl::DRAW_FRAMEBUFFER, g_cursor_fbo);
    a.Disable(gl::SCISSOR_TEST);
    // Same full-window viewport, moved so the hotspot (GL y counts up from the bottom) lands on the centre.
    const int half = kCursorSize / 2;
    a.Viewport(g_saved_viewport[0] + half - g_cursor.x, g_saved_viewport[1] + half - (g_saved_viewport[3] - g_cursor.y),
               g_saved_viewport[2], g_saved_viewport[3]);
    a.BlendFuncSeparate(static_cast<gl::GLenum>(g_saved_blend[0]), static_cast<gl::GLenum>(g_saved_blend[1]), gl::ONE,
                        gl::ONE_MINUS_SRC_ALPHA);
    ++g_cursor_draws;
    return true;
}

void end_cursor_redirect(unsigned draw_fbo) {
    const gl::Api& a = gl::api();
    a.BlendFuncSeparate(static_cast<gl::GLenum>(g_saved_blend[0]), static_cast<gl::GLenum>(g_saved_blend[1]),
                        static_cast<gl::GLenum>(g_saved_blend[2]), static_cast<gl::GLenum>(g_saved_blend[3]));
    a.Viewport(g_saved_viewport[0], g_saved_viewport[1], g_saved_viewport[2], g_saved_viewport[3]);
    if (g_saved_scissor) a.Enable(gl::SCISSOR_TEST);
    a.BindFramebuffer(gl::DRAW_FRAMEBUFFER, draw_fbo);
}

unsigned build_panel_texture(int width, int height, float backing) {
    if (!g_tex || g_panel_failed || width <= 0 || height <= 0) return 0;
    const gl::Api& a = gl::api();
    gl::StateGuard guard;
    gl::StateGuard::neutral();
    if (!g_panel_program) {
        g_panel_program = gl::build_program(kPanelVs, kPanelFs, "panel filter");
        if (!g_panel_program) {
            g_panel_failed = true;  // logged; the panel then shows nothing rather than retrying every frame
            return 0;
        }
        g_loc_src = a.GetUniformLocation(g_panel_program, "u_src");
        g_loc_texel = a.GetUniformLocation(g_panel_program, "u_texel");
        g_loc_lod = a.GetUniformLocation(g_panel_program, "u_lod");
        g_loc_backing = a.GetUniformLocation(g_panel_program, "u_backing");
        a.GenVertexArrays(1, &g_panel_vao);  // empty: the vertices come from gl_VertexID
    }
    if (width != g_panel_width || height != g_panel_height) {
        if (!ensure_rgba_target(&g_panel_tex, &g_panel_fbo, width, height, "panel")) return 0;
        g_panel_width = width;
        g_panel_height = height;
    }
    a.ActiveTexture(gl::TEXTURE0);
    a.BindTexture(gl::TEXTURE_2D, g_tex);
    a.BindSampler(0, 0);
    a.GenerateMipmap(gl::TEXTURE_2D);
    a.BindFramebuffer(gl::DRAW_FRAMEBUFFER, g_panel_fbo);
    a.Viewport(0, 0, width, height);
    a.UseProgram(g_panel_program);
    a.BindVertexArray(g_panel_vao);
    a.Uniform1i(g_loc_src, 0);
    a.Uniform2f(g_loc_texel, 1.0f / static_cast<float>(width), 1.0f / static_cast<float>(height));
    const float ratio = static_cast<float>(g_tex_width) / static_cast<float>(width);
    a.Uniform1f(g_loc_lod, ratio > 2.0f ? std::log2(ratio) - 1.0f : 0.0f);
    const float b = std::clamp(backing, 0.0f, 1.0f);
    a.Uniform4f(g_loc_backing, 0.02f * b, 0.03f * b, 0.05f * b, b);  // dark blue-grey
    a.DrawArrays(gl::TRIANGLES, 0, 3);
    update_ui_mask();
    return g_panel_tex;
}

int ui_at(float u, float v) {
    if (g_mask.empty() || g_mask_w <= 0 || g_mask_h <= 0 || u < 0.0f || u > 1.0f || v < 0.0f || v > 1.0f) return -1;
    const int x = std::min(static_cast<int>(u * g_mask_w), g_mask_w - 1);
    const int y = std::min(static_cast<int>((1.0f - v) * g_mask_h), g_mask_h - 1);  // mask rows are bottom-up
    return g_mask[static_cast<size_t>(y) * g_mask_w + x] > 2 ? 1 : 0;  // any coverage in the cell (mip-averaged)
}

namespace {

// Reads back last frame's mask (if any) and starts this frame's. Needs the overlay's mipmaps (build_panel_texture).
void update_ui_mask() {
    const gl::Api& a = gl::api();
    const int w = std::max(1, g_tex_width >> kMaskLevel), h = std::max(1, g_tex_height >> kMaskLevel);
    gl::GLint pack_buffer = 0, pack_align = 4, pack_row = 0;
    a.GetIntegerv(gl::PIXEL_PACK_BUFFER_BINDING, &pack_buffer);
    a.GetIntegerv(gl::PACK_ALIGNMENT, &pack_align);
    a.GetIntegerv(gl::PACK_ROW_LENGTH, &pack_row);
    if (!g_mask_pbo[0]) a.GenBuffers(2, g_mask_pbo);
    if (!g_mask_fbo) a.GenFramebuffers(1, &g_mask_fbo);

    const int previous = g_mask_next ^ 1, slot = g_mask_next;  // last frame's readback has finished by now
    if (g_mask_pending[previous]) {
        a.BindBuffer(gl::PIXEL_PACK_BUFFER, g_mask_pbo[previous]);
        const int size = g_mask_pbo_w[previous] * g_mask_pbo_h[previous];
        if (const void* p = a.MapBufferRange(gl::PIXEL_PACK_BUFFER, 0, size, gl::MAP_READ_BIT)) {
            g_mask.assign(static_cast<const unsigned char*>(p), static_cast<const unsigned char*>(p) + size);
            g_mask_w = g_mask_pbo_w[previous];
            g_mask_h = g_mask_pbo_h[previous];
            a.UnmapBuffer(gl::PIXEL_PACK_BUFFER);
        }
        g_mask_pending[previous] = false;
    }

    a.BindFramebuffer(gl::READ_FRAMEBUFFER, g_mask_fbo);
    a.FramebufferTexture2D(gl::READ_FRAMEBUFFER, gl::COLOR_ATTACHMENT0, gl::TEXTURE_2D, g_tex, kMaskLevel);
    a.ReadBuffer(gl::COLOR_ATTACHMENT0);
    a.BindBuffer(gl::PIXEL_PACK_BUFFER, g_mask_pbo[slot]);
    if (g_mask_pbo_w[slot] != w || g_mask_pbo_h[slot] != h) {
        a.BufferData(gl::PIXEL_PACK_BUFFER, static_cast<ptrdiff_t>(w) * h, nullptr, gl::STREAM_READ);
        g_mask_pbo_w[slot] = w;
        g_mask_pbo_h[slot] = h;
    }
    a.PixelStorei(gl::PACK_ALIGNMENT, 1);
    a.PixelStorei(gl::PACK_ROW_LENGTH, 0);
    a.ReadPixels(0, 0, w, h, gl::ALPHA, gl::UNSIGNED_BYTE, nullptr);  // rows bottom-up
    g_mask_pending[slot] = true;
    g_mask_next = slot ^ 1;

    a.PixelStorei(gl::PACK_ALIGNMENT, pack_align);
    a.PixelStorei(gl::PACK_ROW_LENGTH, pack_row);
    a.BindBuffer(gl::PIXEL_PACK_BUFFER, static_cast<gl::GLuint>(pack_buffer));
}

}  // namespace

TestState test_state() {
    return {g_hud_fbo, g_tex, g_overlay_frames, g_redirected, g_cursor_tex, g_cursor_draws, g_world_draws, g_world_anchored};
}

float last_world_marker_depth() { return g_last_depth; }

void release() {
    const gl::Api& a = gl::api();
    if (!a.DeleteTextures) return;
    for (gl::GLuint* t : {&g_tex, &g_cursor_tex, &g_panel_tex})
        if (*t) a.DeleteTextures(1, t), *t = 0;
    for (gl::GLuint* f : {&g_fbo, &g_cursor_fbo, &g_panel_fbo, &g_mask_fbo})
        if (*f) a.DeleteFramebuffers(1, f), *f = 0;
    if (g_mask_pbo[0]) a.DeleteBuffers(2, g_mask_pbo);
    g_mask_pbo[0] = g_mask_pbo[1] = 0;
    g_mask_pbo_w[0] = g_mask_pbo_w[1] = g_mask_pbo_h[0] = g_mask_pbo_h[1] = 0;
    g_mask_pending[0] = g_mask_pending[1] = false;
    g_mask.clear();
    g_mask_w = g_mask_h = 0;
    g_tex_width = g_tex_height = 0;
    g_panel_width = g_panel_height = 0;
    g_cursor = g_last_cursor = Cursor{};
}

}  // namespace hw2vr::hud
