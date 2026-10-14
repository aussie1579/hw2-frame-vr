#pragma once

// Key names for hw2vr.ini ([hotkeys] and [buttons]).
//   Keys: A-Z, 0-9, F1-F12, Esc, Space, Tab, Enter, Backspace, Shift, Ctrl, Alt, CapsLock, Up, Down, Left, Right,
//         Home, End, PageUp, PageDown, Insert, Delete, Pause, NumLock, Numpad0-Numpad9, LBracket, RBracket, Slash,
//         Minus, Equals, Comma, Period, Apostrophe, Semicolon, Backslash, Grave.
//   Combos (buttons only): "Ctrl+A", "Alt+1", "Ctrl+Shift+E" - pressed in order, released in reverse.
//   VR actions (buttons only): recentre, world_scale_up, world_scale_down. "none" or empty = unbound.
namespace hw2vr::keys {

int parse_key(const wchar_t* name);  // virtual-key code, 0 if unknown
const char* key_name(int vk);        // for logs; "?" if unknown

struct Binding {
    enum Kind { None, Keys, Recentre, WorldScaleUp, WorldScaleDown } kind = None;
    int count = 0;
    unsigned char vk[4] = {};
};
bool parse_binding(const wchar_t* text, Binding* out);  // false = unknown name (out = None)
void describe(const Binding& b, char* out, int size);   // "Ctrl+A", "recentre", "none"

}  // namespace hw2vr::keys
