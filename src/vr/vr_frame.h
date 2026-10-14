#pragma once

#include <windows.h>

#include <string>

// The VR frame loop, v1 alternating-frame rendering (AFR): every game frame renders one eye with that eye's camera
// (see stereo/view_override). At wglSwapBuffers we copy the finished frame into that eye's texture and submit both
// eyes, each with the head pose it was rendered from; after the swap we wait for the compositor's next poses and
// switch eye. The HUD goes to an OpenVR overlay fixed in the game camera's frame (docs/FINDINGS.md).
namespace hw2vr::vrframe {

struct Settings {
    bool autostart = false;
    float world_scale = 100.0f;    // game units per real metre
    float hud_distance = 3.0f;     // metres from the recentre point
    float hud_scale = 1.0f;        // 1 = HUD spans the game's own field of view, so screen-space markers sit on ships
    bool hud_premultiplied = true;
    bool hud_follow_head = true;   // HUD sits at the head's position (orientation stays with the game camera)
    int key_toggle = VK_F8, key_recentre = VK_F10, key_scale_up = VK_PRIOR, key_scale_down = VK_NEXT;  // Ctrl+Shift+
};

void configure(const Settings& settings, const std::wstring& dll_dir);
void before_swap(HDC dc);  // hotkeys, eye capture + submit, HUD overlay texture
void after_swap();         // WaitGetPoses -> camera for the next frame's eye
// No shutdown hook on purpose: at process exit OpenVR notices on its own, and VR_Shutdown under the loader lock
// (DllMain) risks a deadlock.

}  // namespace hw2vr::vrframe
