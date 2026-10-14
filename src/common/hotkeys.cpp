#include "common/hotkeys.h"

#include <windows.h>

#include "common/log.h"
#include "input/key_names.h"

namespace hw2vr::hotkeys {
namespace {

bool g_down[256] = {};       // pressed() edge state
bool g_seen_down[256] = {};  // log_seen_keys() edge state

// Keys log_seen_keys() watches.
constexpr int kWatched[] = {VK_F1, VK_F2, VK_F3, VK_F4, VK_F5, VK_F6, VK_F7, VK_F8, VK_F9, VK_F10, VK_F11,
                            VK_F12, VK_PRIOR, VK_NEXT, VK_HOME, VK_END, VK_INSERT, VK_DELETE};

bool game_has_focus() {
    DWORD pid = 0;
    GetWindowThreadProcessId(GetForegroundWindow(), &pid);
    return pid == GetCurrentProcessId();
}

bool ctrl_shift() { return (GetAsyncKeyState(VK_CONTROL) & 0x8000) && (GetAsyncKeyState(VK_SHIFT) & 0x8000); }

}  // namespace

bool pressed(int vk) {
    const bool down = game_has_focus() && ctrl_shift() && (GetAsyncKeyState(vk) & 0x8000);
    const bool edge = down && !g_down[vk & 0xFF];
    g_down[vk & 0xFF] = down;
    return edge;
}

int parse_key(const wchar_t* name) { return keys::parse_key(name); }
const char* key_name(int vk) { return keys::key_name(vk); }

void log_seen_keys() {
    const bool modifiers = game_has_focus() && ctrl_shift();
    for (const int vk : kWatched) {
        const bool down = modifiers && (GetAsyncKeyState(vk) & 0x8000);
        if (down && !g_seen_down[vk]) HW2VR_LOG("hotkeys: saw Ctrl+Shift+%s", keys::key_name(vk));
        g_seen_down[vk] = down;
    }
}

}  // namespace hw2vr::hotkeys
