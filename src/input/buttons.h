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
    const wchar_t* default_alt = L"";   // [buttons] <ini_key>_alt: pressed with the menu modifier held
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
    // D-pad menus; with the left trigger held, the ship abilities (see kDefaultMenuModifier).
    {"dpad_up", L"dpad_up", L"menu:orders", L"", L"menu:abilities"},          // move, attack, ...; cloak, mines, EMP...
    {"dpad_down", L"dpad_down", L"menu:production", L"", L"menu:commands"},   // build, research...; rally, retire...
    {"dpad_left", L"dpad_left", L"menu:tactics", L"", L"menu:special"},       // tactics, stances; capture, kamikaze...
    {"dpad_right", L"dpad_right", L"menu:formations"},  // strike-group formations
    {"left_trigger", L"left_trigger", L"Shift"},  // add to selection; move orders: up/down (held, light pull)
    {"left_bumper", L"left_bumper", L"Ctrl"},     // force attack; Ctrl+number assigns groups
    {"left_stick", L"left_stick", L"Home"},       // focus on the mothership
    {"right_stick", L"right_stick", L"middle_click", L"recentre"},  // click: focus on what the laser is on (the
                                                                     // game's middle click); hold: recentre the view
};

// The button that, held, switches buttons to their _alt binding ([buttons] menu_modifier).
inline constexpr const wchar_t* kDefaultMenuModifier = L"left_trigger";

}  // namespace hw2vr::input
