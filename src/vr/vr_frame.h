#pragma once

#include <windows.h>

#include <string>

// The VR frame loop. After each swap we wait for the compositor's poses and set the camera for the next frame.
//   Synchronized stereo (default): the game renders the left eye; at wglSwapBuffers the frame is copied out and
//     replayed with the right eye's camera (stereo/frame_replay.h), so both eyes show the same moment every frame.
//   Alternating-frame rendering (stereo = afr): each frame renders one eye; both eyes are submitted with the pose
//     they were rendered from. Cheaper, but each eye updates at half rate and moving things judder.
// The HUD goes to an OpenVR overlay fixed in the game camera's frame (docs/FINDINGS.md).
namespace hw2vr::vrframe {

struct Settings {
    bool autostart = false;
    float world_scale = 100.0f;    // game units per real metre
    float hud_distance = 3.0f;     // metres from the recentre point
    float hud_scale = 1.0f;        // 1 = HUD spans the game's own field of view, so screen-space markers sit on ships
    bool hud_premultiplied = true;
    bool hud_follow_head = true;   // HUD sits at the head's position (orientation stays with the game camera)
    bool sync_stereo = true;       // both eyes every frame (replay); false = alternating-frame rendering
    int key_toggle = VK_F8, key_recentre = VK_F10, key_scale_up = VK_PRIOR, key_scale_down = VK_NEXT;  // Ctrl+Shift+
};

void configure(const Settings& settings, const std::wstring& dll_dir);
bool active();           // VR running
bool wants_recording();  // synchronized stereo: record this frame for the second-eye replay
void before_swap(HDC dc);  // hotkeys, eye capture + submit, HUD overlay texture
void after_swap();         // WaitGetPoses -> camera for the next frame's eye
// No shutdown hook on purpose: at process exit OpenVR notices on its own, and VR_Shutdown under the loader lock
// (DllMain) risks a deadlock.

}  // namespace hw2vr::vrframe
