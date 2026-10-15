#pragma once

// The controller's hotkey buttons: SteamVR actions /actions/main/in/button_<name> (bindings/generate_bindings.py)
// and keys of hw2vr.ini [buttons]. Order matters only inside the proxy.
namespace hw2vr::input {

enum Button {
    kA, kB, kX, kY, kMenu, kView, kDpadUp, kDpadDown, kDpadLeft, kDpadRight,
    kLeftTrigger, kLeftBumper, kLeftStick, kRightStick, kButtonCount
};

struct ButtonInfo {
    const char* name;            // action suffix and ini key
    const wchar_t* ini_key;
    const wchar_t* default_keys;  // see input/key_names.h for the syntax
    const wchar_t* default_hold = L"";  // [buttons] <ini_key>_hold: held hold_ms instead (then the key above taps on release)
};

// Defaults (Steam Frame, after headset test 9). Right hand points: trigger = right mouse button, bumper = left (both
// are fixed actions, not hotkeys). Left hand: the camera, the modifiers and the D-pad's command menus (hold a
// direction). HW2 keys from scripts/keybindings.lua (docs/FINDINGS.md "Input").
inline constexpr ButtonInfo kButtons[kButtonCount] = {
    {"a", L"a", L"M"},                   // move order
    {"b", L"b", L"Esc"},                 // cancel / back
    {"x", L"x", L"F"},                   // focus the camera on the selection
    {"y", L"y", L"Space"},               // Sensors Manager (strategic map)
    {"menu", L"menu", L"F10"},           // in-game menu
    {"view", L"view", L"Tab"},           // tactical overlay
    {"dpad_up", L"dpad_up", L"menu:orders"},          // move, attack, guard, dock, stop...
    {"dpad_down", L"dpad_down", L"menu:production"},  // build, research, launch, the game's screens
    {"dpad_left", L"dpad_left", L"menu:tactics"},     // tactics and stances
    {"dpad_right", L"dpad_right", L"menu:formations"},  // strike-group formations
    {"left_trigger", L"left_trigger", L"Shift"},  // add to selection; move orders: up/down (held, light pull)
    {"left_bumper", L"left_bumper", L"Ctrl"},     // force attack; Ctrl+number assigns groups
    {"left_stick", L"left_stick", L"Home"},       // focus on the mothership
    {"right_stick", L"right_stick", L"middle_click", L"recentre"},  // click: focus on what the laser is on (the
                                                                     // game's middle click); hold: recentre the view
};

}  // namespace hw2vr::input
