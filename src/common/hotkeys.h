#pragma once

namespace hw2vr::hotkeys {

// Ctrl+Shift+<vk>, edge-triggered, only while the game window has focus. Poll once per frame per key.
bool pressed(int vk);

// Key names used in hw2vr.ini [hotkeys]: F1-F12, PageUp, PageDown, Home, End, Insert, Delete, A-Z, 0-9.
int parse_key(const wchar_t* name);  // 0 if unknown
const char* key_name(int vk);

// Diagnostics: logs each Ctrl+Shift+<F-key/PgUp/PgDn/Home/End> the proxy actually sees, once per press. Another
// program can register a combo as a global hotkey and swallow it before the game sees it; this shows which.
void log_seen_keys();

}  // namespace hw2vr::hotkeys
