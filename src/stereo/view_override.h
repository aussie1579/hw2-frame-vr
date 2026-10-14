#pragma once

namespace hw2vr::stereo {

// Phase 0 spike: a fixed camera transform applied to every camera-view uniform, so we can check the 3D
// scene moves as one while the 2D HUD stays put. Ctrl+Shift+F9 cycles off -> yaw -> offset -> yaw+offset.
enum class ViewSource { InMatV, InMatC, InMatMV };  // which uniform carried the camera (see docs/FINDINGS.md)

void configure(float yaw_degrees, float offset_x, int start_mode);  // start_mode: 0 off, 1 yaw, 2 offset, 3 both
void on_frame();  // once per wglSwapBuffers: hotkey + per-frame stats
bool override_active();

// out = E * in (column-major). Rotation-only inputs (skybox) get only E's rotation, so the sky never translates.
void apply_view_override(const float in[16], float out[16], ViewSource source);

}  // namespace hw2vr::stereo
