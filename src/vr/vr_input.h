#pragma once

#include <string>

#include "input/buttons.h"

// SteamVR Input (IVRInput) for the controllers. As the scene app the proxy gets every controller input; SteamVR's
// own laser ("quickmouse", on grip) is never involved. Actions and the Steam Frame default binding are in
// hw2vr_bindings\ next to the DLL (bindings/generate_bindings.py); everything is rebindable in SteamVR.
namespace hw2vr::vrinput {

struct ButtonState {
    bool down = false, pressed = false, released = false;
};

struct State {
    bool pointer_valid = false;
    float pointer_pose[16];  // right-hand laser ("tip" pose), seated space, column-major; aims down -Z
    bool left_valid = false;
    float left_pose[16];     // left hand, OpenXR grip convention: +X out of the palm, -Z along the fingers
    ButtonState trigger, bumper;  // right hand: trigger = its pull past trigger_press (the right mouse button),
                                  // bumper = the left mouse button
    float trigger_pull = 0.0f;    // the right trigger, 0..1
    float camera[2] = {}, zoom[2] = {};  // left and right stick, -1..1
    ButtonState buttons[input::kButtonCount];
};

// After vr::start(). manifest_path: absolute path of action_manifest.json. False (logged) = no controls.
bool init(const std::wstring& manifest_path);
void shutdown();
bool ready();
// Once per frame after WaitGetPoses: the pointer pose is predicted for the frame being rendered next.
const State& update();
void pulse(float seconds, float amplitude);  // right-hand haptic tick
// Trigger pulls (right = right mouse button, left = its hotkey) count as pressed from `press` (0..1) until back below
// `release`.
void set_trigger_thresholds(float press, float release);

}  // namespace hw2vr::vrinput
