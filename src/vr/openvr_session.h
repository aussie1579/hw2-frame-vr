#pragma once

#include <string>

// Thin OpenVR wrapper. openvr_api.dll (32-bit, shipped next to the proxy) is loaded at runtime, so a missing DLL
// or SteamVR just means "no VR" instead of a game that won't start. All calls on the render thread with the
// game's GL context current.
namespace hw2vr::vr {

struct EyeFrustum {
    float left, right, down, up;  // tangents of the half-angles (left/down negative)
};

bool start(const std::wstring& dll_dir);  // VR_Init (scene app, seated); logs and returns false on any failure
void stop();
bool running();

// Blocks until the compositor wants the next frame; returns the HMD pose (column-major, seated tracking space,
// metres) or false if tracking is lost.
bool wait_poses(float hmd_pose[16]);
const EyeFrustum& frustum(int eye);    // 0 = left, 1 = right
const float* eye_to_head(int eye);     // column-major, metres

// Submits a GL texture for one eye with the HMD pose it was rendered from (Submit_TextureWithPose), so the
// compositor reprojects the stale eye correctly under alternating-frame rendering.
bool submit(int eye, unsigned gl_texture, const float hmd_pose_used[16]);

// The HUD overlay quad.
bool hud_create(bool premultiplied);
void hud_set_texture(unsigned gl_texture);
void hud_place(const float tracking_from_hud[16], float width_m);
void hud_visible(bool on);

}  // namespace hw2vr::vr
