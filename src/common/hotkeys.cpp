#include "common/hotkeys.h"

#include <windows.h>

namespace hw2vr::hotkeys {
namespace {

bool g_down[256] = {};

bool game_has_focus() {
    DWORD pid = 0;
    GetWindowThreadProcessId(GetForegroundWindow(), &pid);
    return pid == GetCurrentProcessId();
}

}  // namespace

bool pressed(int vk) {
    const bool down = game_has_focus() && (GetAsyncKeyState(VK_CONTROL) & 0x8000) && (GetAsyncKeyState(VK_SHIFT) & 0x8000) &&
                      (GetAsyncKeyState(vk) & 0x8000);
    const bool edge = down && !g_down[vk & 0xFF];
    g_down[vk & 0xFF] = down;
    return edge;
}

}  // namespace hw2vr::hotkeys
