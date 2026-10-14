#include "common/hotkeys.h"

#include <windows.h>

#include <cwchar>

#include "common/log.h"

namespace hw2vr::hotkeys {
namespace {

bool g_down[256] = {};       // pressed() edge state
bool g_seen_down[256] = {};  // log_seen_keys() edge state

struct Named {
    int vk;
    const wchar_t* wide;
    const char* narrow;
};
constexpr Named kNamed[] = {
    {VK_F1, L"F1", "F1"},       {VK_F2, L"F2", "F2"},       {VK_F3, L"F3", "F3"},
    {VK_F4, L"F4", "F4"},       {VK_F5, L"F5", "F5"},       {VK_F6, L"F6", "F6"},
    {VK_F7, L"F7", "F7"},       {VK_F8, L"F8", "F8"},       {VK_F9, L"F9", "F9"},
    {VK_F10, L"F10", "F10"},    {VK_F11, L"F11", "F11"},    {VK_F12, L"F12", "F12"},
    {VK_PRIOR, L"PageUp", "PageUp"}, {VK_NEXT, L"PageDown", "PageDown"}, {VK_HOME, L"Home", "Home"},
    {VK_END, L"End", "End"},    {VK_INSERT, L"Insert", "Insert"}, {VK_DELETE, L"Delete", "Delete"},
};

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

int parse_key(const wchar_t* name) {
    for (const Named& k : kNamed)
        if (_wcsicmp(name, k.wide) == 0) return k.vk;
    if (name[0] && !name[1]) {
        const wchar_t c = static_cast<wchar_t>(towupper(name[0]));
        if ((c >= L'A' && c <= L'Z') || (c >= L'0' && c <= L'9')) return c;
    }
    return 0;
}

const char* key_name(int vk) {
    for (const Named& k : kNamed)
        if (k.vk == vk) return k.narrow;
    static char single[2];
    single[0] = static_cast<char>(vk);
    return single;
}

void log_seen_keys() {
    const bool modifiers = game_has_focus() && ctrl_shift();
    for (const Named& k : kNamed) {
        const bool down = modifiers && (GetAsyncKeyState(k.vk) & 0x8000);
        if (down && !g_seen_down[k.vk]) HW2VR_LOG("hotkeys: saw Ctrl+Shift+%s", k.narrow);
        g_seen_down[k.vk] = down;
    }
}

}  // namespace hw2vr::hotkeys
