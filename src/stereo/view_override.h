#pragma once

namespace hw2vr::stereo {

// Phase 0 spike: a fixed camera transform E applied wherever the camera enters a shader, so we can check the 3D
// scene moves as one while the 2D HUD stays put. Ctrl+Shift+F9 cycles off -> yaw -> offset -> yaw+offset.
//
// Where the camera enters (docs/FINDINGS.md, "Camera"):
enum class CameraUniform {
    InMatV,   // camera view                                        -> E * V
    InMatC,   // camera view (P*C*MV shaders)                       -> E * C
    InMatMV,  // model-view, in programs with no inMatV/inMatC      -> E * MV
    InMatP,   // projection of view-space input (CPU-transformed vertices, e.g. light glows) -> P * E
};

void configure(float yaw_degrees, float offset_x, int start_mode);  // start_mode: 0 off, 1 yaw, 2 offset, 3 both
void on_frame();  // once per wglSwapBuffers: hotkey + per-frame stats
bool override_active();

// out = E * in (column-major). Rotation-only inputs (skybox) get only E's rotation, so the sky never translates.
void apply_view_override(const float in[16], float out[16], CameraUniform source);
// out = proj * E, for programs whose vertices are already in view space.
void apply_projection_override(const float proj[16], float out[16]);

// Colour of the on-screen mode square (top-left), or false when the override is off.
bool mode_indicator_color(float rgb[3]);

}  // namespace hw2vr::stereo
