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
    ButtonState left_click, right_click;
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

}  // namespace hw2vr::vrinput
