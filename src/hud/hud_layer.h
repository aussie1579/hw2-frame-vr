#pragma once

// The game draws its HUD into its own framebuffer (fbo 35 on build 12653478), cleared to (0,0,0,1) and blended so
// alpha holds *transmittance*; the final composite lays it over the scene (docs/FINDINGS.md "Render passes").
// That layer mixes two kinds of draw:
//   - 2D UI with an ortho inMatP (taskbar, menus, text, CPU-positioned markers) -> belongs on the flat VR overlay;
//   - world-anchored 3D overlay with a perspective inMatP (selection circles, move lines, ship icons with pixel
//     offsets, the Sensors Manager disc) -> must be stereo, at the depth of what it marks.
// In VR the 2D draws are redirected into our overlay texture (with coverage alpha) and the 3D draws stay in the
// game's HUD framebuffer with the per-eye camera, so the game's own composite puts them in each eye image. UI the
// game draws straight to the screen after the composite (the mouse cursor) is redirected onto the overlay too.
namespace hw2vr::hud {

// Identification: gl_hooks reports every ortho inMatP upload with the draw framebuffer it happened in; the HUD is the
// framebuffer that collects the most of them, consistently over several frames.
void note_ortho_projection(unsigned draw_fbo);
bool is_hud_fbo(unsigned fbo);

void set_capture(bool enabled, bool premultiplied);
void set_target_size(int width, int height);  // the game's HUD is window-sized
// Called by the glBindFramebuffer hook before the game switches draw framebuffer.
void on_draw_framebuffer_change(unsigned from, unsigned to);
void end_frame();

bool overlay_ready();        // the overlay texture was rebuilt this frame
unsigned overlay_texture();  // GL texture for the VR overlay (premultiplied colour, coverage alpha)
void release();              // delete GL objects (VR off)

// Every game draw, before it runs. is_2d: the current program's projection is ortho (or untracked).
bool should_redirect(unsigned draw_fbo, unsigned program, bool is_2d);
void begin_redirect(unsigned draw_fbo);  // bind the overlay target, keep the game's colour blend, coverage alpha
void end_redirect(unsigned draw_fbo);    // restore the game's blend and framebuffer

// For tests/proxy_smoke.cpp (exported from the DLL as hw2vr_test_state).
struct TestState {
    unsigned hud_fbo;
    unsigned overlay_texture;
    unsigned overlay_frames;
    unsigned redirected_draws;
};
TestState test_state();

}  // namespace hw2vr::hud
