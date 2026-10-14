#include "vr/vr_input.h"

#include <windows.h>

#include <string>

#include "common/log.h"
#include "common/mat4.h"
#include "vr/openvr_internal.h"

namespace hw2vr::vrinput {
namespace {

bool g_ready = false;
::vr::VRActionSetHandle_t g_set = ::vr::k_ulInvalidActionSetHandle;
::vr::VRActionHandle_t g_pointer = 0, g_left_click = 0, g_right_click = 0, g_camera = 0, g_zoom = 0, g_haptic = 0;
::vr::VRActionHandle_t g_buttons[input::kButtonCount] = {};
State g_state;
bool g_logged_inactive = false;

std::string utf8(const std::wstring& w) {
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string s(n > 0 ? n - 1 : 0, '\0');
    if (n > 1) WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, s.data(), n, nullptr, nullptr);
    return s;
}

bool action(const char* path, ::vr::VRActionHandle_t* out) {
    const auto err = vr::input_api()->GetActionHandle(path, out);
    if (err != ::vr::VRInputError_None) HW2VR_LOG("vrinput: GetActionHandle(%s) failed: %d", path, static_cast<int>(err));
    return err == ::vr::VRInputError_None;
}

ButtonState read_button(::vr::VRActionHandle_t a) {
    ::vr::InputDigitalActionData_t d{};
    vr::input_api()->GetDigitalActionData(a, &d, sizeof d, ::vr::k_ulInvalidInputValueHandle);
    ButtonState b;
    b.down = d.bActive && d.bState;
    const bool changed = d.bActive && d.bChanged;
    b.pressed = changed && b.down;
    b.released = changed && !b.down;
    return b;
}

// A button that stops being active (SteamVR dashboard opened) while held reports a release, so nothing sticks.
ButtonState track(const ButtonState& now, const ButtonState& before) {
    ButtonState b = now;
    if (before.down && !now.down) b.released = true;
    return b;
}

void read_vector(::vr::VRActionHandle_t a, float out[2]) {
    ::vr::InputAnalogActionData_t d{};
    vr::input_api()->GetAnalogActionData(a, &d, sizeof d, ::vr::k_ulInvalidInputValueHandle);
    out[0] = d.bActive ? d.x : 0.0f;
    out[1] = d.bActive ? d.y : 0.0f;
}

}  // namespace

bool init(const std::wstring& manifest_path) {
    g_ready = false;
    ::vr::IVRInput* in = vr::input_api();
    if (!in) {
        HW2VR_LOG("vrinput: IVRInput unavailable - no controller input");
        return false;
    }
    if (GetFileAttributesW(manifest_path.c_str()) == INVALID_FILE_ATTRIBUTES) {
        HW2VR_LOG("vrinput: %ls is missing (reinstall with tools\\install.ps1) - no controller input", manifest_path.c_str());
        return false;
    }
    auto err = in->SetActionManifestPath(utf8(manifest_path).c_str());
    if (err != ::vr::VRInputError_None) {
        HW2VR_LOG("vrinput: SetActionManifestPath failed: %d - no controller input", static_cast<int>(err));
        return false;
    }
    err = in->GetActionSetHandle("/actions/main", &g_set);
    bool ok = err == ::vr::VRInputError_None;
    ok &= action("/actions/main/in/pointer", &g_pointer);
    ok &= action("/actions/main/in/leftclick", &g_left_click);
    ok &= action("/actions/main/in/rightclick", &g_right_click);
    ok &= action("/actions/main/in/camera", &g_camera);
    ok &= action("/actions/main/in/zoom", &g_zoom);
    ok &= action("/actions/main/out/haptic", &g_haptic);
    for (int i = 0; i < input::kButtonCount; ++i)
        ok &= action((std::string("/actions/main/in/button_") + input::kButtons[i].name).c_str(), &g_buttons[i]);
    if (!ok) {
        HW2VR_LOG("vrinput: action setup failed - no controller input");
        return false;
    }
    g_state = State{};
    g_logged_inactive = false;
    g_ready = true;
    HW2VR_LOG("vrinput: actions loaded from %ls", manifest_path.c_str());
    return true;
}

void shutdown() { g_ready = false; }
bool ready() { return g_ready; }

const State& update() {
    if (!g_ready) return g_state;
    ::vr::IVRInput* in = vr::input_api();
    ::vr::VRActiveActionSet_t active{};
    active.ulActionSet = g_set;
    in->UpdateActionState(&active, sizeof active, 1);

    State prev = g_state;
    ::vr::InputPoseActionData_t pose{};
    in->GetPoseActionDataForNextFrame(g_pointer, ::vr::TrackingUniverseSeated, &pose, sizeof pose,
                                      ::vr::k_ulInvalidInputValueHandle);
    g_state.pointer_valid = pose.bActive && pose.pose.bPoseIsValid;
    if (g_state.pointer_valid) mat4::from_rows3x4(pose.pose.mDeviceToAbsoluteTracking.m, g_state.pointer_pose);
    if (!pose.bActive && !g_logged_inactive) {
        HW2VR_LOG("vrinput: laser pose inactive (controller off, SteamVR dashboard open, or no binding for this "
                  "controller - SteamVR > Controller bindings)");
        g_logged_inactive = true;
    }
    if (pose.bActive) g_logged_inactive = false;

    g_state.left_click = track(read_button(g_left_click), prev.left_click);
    g_state.right_click = track(read_button(g_right_click), prev.right_click);
    read_vector(g_camera, g_state.camera);
    read_vector(g_zoom, g_state.zoom);
    for (int i = 0; i < input::kButtonCount; ++i) g_state.buttons[i] = track(read_button(g_buttons[i]), prev.buttons[i]);
    for (int i = 0; i < input::kButtonCount; ++i)
        if (g_state.buttons[i].pressed) HW2VR_VLOG("vrinput: %s pressed", input::kButtons[i].name);
    return g_state;
}

void pulse(float seconds, float amplitude) {
    if (g_ready) vr::input_api()->TriggerHapticVibrationAction(g_haptic, 0.0f, seconds, 160.0f, amplitude,
                                                               ::vr::k_ulInvalidInputValueHandle);
}

}  // namespace hw2vr::vrinput
