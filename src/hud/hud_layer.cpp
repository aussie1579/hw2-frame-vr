#include "hud/hud_layer.h"

#include <unordered_map>
#include <unordered_set>

#include "common/log.h"
#include "proxy/gl_api.h"

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
unsigned g_overlay_frames = 0, g_redirected = 0;
gl::GLint g_saved_blend[4];

constexpr gl::GLenum kSrcRgb = 0x80C9, kDstRgb = 0x80C8, kSrcAlpha = 0x80CB, kDstAlpha = 0x80CA, kOne = 1,
                     kOneMinusSrcAlpha = 0x0303;

bool ensure_target() {
    if (g_tex && g_tex_width == g_width && g_tex_height == g_height) return true;
    if (g_width <= 0 || g_height <= 0) return false;
    const gl::Api& a = gl::api();
    if (!g_tex) a.GenTextures(1, &g_tex);
    a.BindTexture(gl::TEXTURE_2D, g_tex);
    a.TexImage2D(gl::TEXTURE_2D, 0, gl::RGBA8, g_width, g_height, 0, gl::RGBA, gl::UNSIGNED_BYTE, nullptr);
    a.TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_MIN_FILTER, gl::LINEAR);
    a.TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_MAG_FILTER, gl::LINEAR);
    a.TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_WRAP_S, gl::CLAMP_TO_EDGE);
    a.TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_WRAP_T, gl::CLAMP_TO_EDGE);
    if (!g_fbo) a.GenFramebuffers(1, &g_fbo);
    a.BindFramebuffer(gl::DRAW_FRAMEBUFFER, g_fbo);
    a.FramebufferTexture2D(gl::DRAW_FRAMEBUFFER, gl::COLOR_ATTACHMENT0, gl::TEXTURE_2D, g_tex, 0);
    const bool ok = a.CheckFramebufferStatus(gl::DRAW_FRAMEBUFFER) == gl::FRAMEBUFFER_COMPLETE;
    if (!ok) HW2VR_LOG("hud: overlay target %dx%d is incomplete", g_width, g_height);
    else HW2VR_LOG("hud: overlay target %dx%d", g_width, g_height);
    g_tex_width = g_width;
    g_tex_height = g_height;
    return ok;
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

DrawAction draw_action(unsigned draw_fbo, unsigned program, bool is_2d) {
    if (!g_ready) return DrawAction::Draw;
    bool to_overlay = false;
    if (is_hud_fbo(draw_fbo)) {
        if (is_2d && !g_replaying) g_ui_programs.insert(program);
        to_overlay = is_2d;  // 3D overlay stays in the game's HUD layer, drawn per eye
    } else {
        to_overlay = draw_fbo == 0 && g_ui_programs.count(program) != 0;  // UI drawn after the composite (cursor)
    }
    if (!to_overlay) return DrawAction::Draw;
    return g_replaying ? DrawAction::Skip : DrawAction::Redirect;
}

void set_replaying(bool on) { g_replaying = on; }

void begin_redirect(unsigned /*draw_fbo*/) {
    const gl::Api& a = gl::api();
    a.GetIntegerv(kSrcRgb, &g_saved_blend[0]);
    a.GetIntegerv(kDstRgb, &g_saved_blend[1]);
    a.GetIntegerv(kSrcAlpha, &g_saved_blend[2]);
    a.GetIntegerv(kDstAlpha, &g_saved_blend[3]);
    a.BindFramebuffer(gl::DRAW_FRAMEBUFFER, g_fbo);
    // The game's alpha factors write transmittance; the overlay holds coverage. Colour factors stay the game's, so
    // the overlay accumulates premultiplied colour.
    a.BlendFuncSeparate(static_cast<gl::GLenum>(g_saved_blend[0]), static_cast<gl::GLenum>(g_saved_blend[1]), kOne,
                        kOneMinusSrcAlpha);
    ++g_redirected;
}

void end_redirect(unsigned draw_fbo) {
    const gl::Api& a = gl::api();
    a.BlendFuncSeparate(static_cast<gl::GLenum>(g_saved_blend[0]), static_cast<gl::GLenum>(g_saved_blend[1]),
                        static_cast<gl::GLenum>(g_saved_blend[2]), static_cast<gl::GLenum>(g_saved_blend[3]));
    a.BindFramebuffer(gl::DRAW_FRAMEBUFFER, draw_fbo);
}

TestState test_state() { return {g_hud_fbo, g_tex, g_overlay_frames, g_redirected}; }

void release() {
    const gl::Api& a = gl::api();
    if (!a.DeleteTextures || !g_tex) return;
    a.DeleteTextures(1, &g_tex);
    a.DeleteFramebuffers(1, &g_fbo);
    g_tex = g_fbo = 0;
    g_tex_width = g_tex_height = 0;
}

}  // namespace hw2vr::hud
