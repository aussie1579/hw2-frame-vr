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
};

// Defaults (Steam Frame): right hand A/B/X/Y/Menu, left hand View/D-pad/trigger/bumper, both stick clicks.
// HW2 keys from scripts/keybindings.lua (docs/FINDINGS.md "Default keys").
inline constexpr ButtonInfo kButtons[kButtonCount] = {
    {"a", L"a", L"F"},                   // focus on the selection
    {"b", L"b", L"Esc"},                 // cancel
    {"x", L"x", L"Space"},               // Sensors Manager
    {"y", L"y", L"M"},                   // move order
    {"menu", L"menu", L"F10"},           // in-game menu
    {"view", L"view", L"Tab"},           // tactical overlay
    {"dpad_up", L"dpad_up", L"Up"},      // camera pan (held)
    {"dpad_down", L"dpad_down", L"Down"},
    {"dpad_left", L"dpad_left", L"Left"},
    {"dpad_right", L"dpad_right", L"Right"},
    {"left_trigger", L"left_trigger", L"Shift"},  // add to selection; move orders: vertical
    {"left_bumper", L"left_bumper", L"Ctrl"},     // force attack; Ctrl+number assigns groups
    {"left_stick", L"left_stick", L"Home"},       // focus on the mothership
    {"right_stick", L"right_stick", L"recentre"}, // recentre the view (VR action, not a key)
};

}  // namespace hw2vr::input
