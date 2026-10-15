#pragma once

#include "stereo/eye_math.h"

// The camera transform applied wherever the camera enters a shader. Two sources:
//   - VR: the current eye's view correction E and frustum, set every frame by vr/vr_frame.cpp;
//   - the Phase 0 spike (flat screen only): a fixed yaw/offset, Ctrl+Shift+F9 cycles off/yaw/offset/both.
namespace hw2vr::stereo {

// Where the camera enters (docs/FINDINGS.md, "Camera"):
enum class CameraUniform {
    InMatV,   // camera view                                        -> E * V
    InMatC,   // camera view (P*C*MV shaders)                       -> E * C
    InMatMV,  // model-view, in programs with no inMatV/inMatC      -> E * MV
    InMatP,   // projection of view-space input (CPU-transformed vertices, e.g. light glows) -> P * E
};

// start_mode: 0 off, 1 yaw, 2 offset, 3 both; hotkey_vk: Ctrl+Shift+<vk> cycles the mode.
void configure_spike(float yaw_degrees, float offset_x, int start_mode, int hotkey_vk);
void on_frame();  // once per wglSwapBuffers: spike hotkey + per-frame stats

void set_vr_eye(const float eye_from_game_camera[16], const Frustum& frustum);
void clear_vr_eye();
bool vr_eye_active();
// A view-only transform for tests (no projection change); nullptr clears it.
void set_debug_eye(const float* eye_from_game_camera);
// Bumped whenever the camera transform changes (twice per frame with synchronized stereo): anything cached per
// camera (gl_hooks' group-B inMatMV) must be refreshed when it moves on.
unsigned camera_generation();

bool active();  // anything to apply

// out = E * in (column-major). Rotation-only inputs (skybox) get only E's rotation, so the sky never translates.
void apply_view_override(const float in[16], float out[16], CameraUniform source);
// Perspective projection: VR replaces its x/y rows with the eye frustum; view-space-input programs also get * E.
// Returns false when the projection should be uploaded unchanged.
bool apply_projection_override(const float proj[16], float out[16], bool view_space_input);

// A 2D HUD draw (ortho projection over the game's screen) placed in 3D: on the plane `view_depth` game units in
// front of the game camera, where the game camera sees it, then through the current eye's camera and projection.
// The marker keeps its place and size as seen from the game camera; each eye sees it at that depth. False when
// there is no eye (flat) or no game projection yet.
bool place_at_depth(const float ortho[16], float view_depth, float out[16]);

// The game's own main-camera projection (infinite far plane), remembered for sizing the HUD overlay.
// draw_framebuffer: where the main 3D scene is being drawn (its depth buffer is what the laser is tested against).
void note_game_projection(const float proj[16], unsigned draw_framebuffer);
unsigned scene_framebuffer();  // 0 = not seen yet (or the default framebuffer)
float game_tan_half_fov_x();
const float* game_projection();  // the last one seen (column-major), nullptr before the first

// Colour of the on-screen mode square (top-left), or false when nothing is active.
bool mode_indicator_color(float rgb[3]);

}  // namespace hw2vr::stereo
