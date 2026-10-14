#pragma once

// The game draws its HUD into its own framebuffer (fbo 35 on build 12653478) with ortho projections, cleared to
// (0,0,0,1) and blended so alpha holds *transmittance*; the final composite then lays it over the scene
// (docs/FINDINGS.md "Render passes"). In VR we lift it out: when the game leaves that framebuffer, copy it,
// convert it to premultiplied colour + coverage alpha for the overlay, and clear the game's copy so the eye images
// carry no HUD. Draws into the HUD framebuffer keep the game's own camera (see gl_hooks.cpp).
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

bool captured_this_frame();
unsigned overlay_texture();  // GL texture ready for the VR overlay
void release();              // delete GL objects (VR off)

}  // namespace hw2vr::hud
