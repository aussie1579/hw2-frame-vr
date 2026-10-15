#include "input/key_names.h"

#include <windows.h>

#include <cstdio>
#include <cstring>
#include <cwchar>
#include <cwctype>
#include <string>

#include "input/command_menus.h"

namespace hw2vr::keys {
namespace {

struct Named {
    int vk;
    const wchar_t* name;
    const char* narrow;
};
constexpr Named kNamed[] = {
    {VK_ESCAPE, L"Esc", "Esc"},           {VK_ESCAPE, L"Escape", "Esc"},       {VK_SPACE, L"Space", "Space"},
    {VK_TAB, L"Tab", "Tab"},              {VK_RETURN, L"Enter", "Enter"},      {VK_RETURN, L"Return", "Enter"},
    {VK_BACK, L"Backspace", "Backspace"}, {VK_SHIFT, L"Shift", "Shift"},       {VK_CONTROL, L"Ctrl", "Ctrl"},
    {VK_CONTROL, L"Control", "Ctrl"},     {VK_MENU, L"Alt", "Alt"},            {VK_CAPITAL, L"CapsLock", "CapsLock"},
    {VK_UP, L"Up", "Up"},                 {VK_DOWN, L"Down", "Down"},          {VK_LEFT, L"Left", "Left"},
    {VK_RIGHT, L"Right", "Right"},        {VK_HOME, L"Home", "Home"},          {VK_END, L"End", "End"},
    {VK_PRIOR, L"PageUp", "PageUp"},      {VK_PRIOR, L"PgUp", "PageUp"},       {VK_NEXT, L"PageDown", "PageDown"},
    {VK_NEXT, L"PgDn", "PageDown"},       {VK_INSERT, L"Insert", "Insert"},    {VK_DELETE, L"Delete", "Delete"},
    {VK_PAUSE, L"Pause", "Pause"},        {VK_NUMLOCK, L"NumLock", "NumLock"}, {VK_OEM_4, L"LBracket", "LBracket"},
    {VK_OEM_6, L"RBracket", "RBracket"},  {VK_OEM_2, L"Slash", "Slash"},       {VK_OEM_MINUS, L"Minus", "Minus"},
    {VK_OEM_PLUS, L"Equals", "Equals"},   {VK_OEM_COMMA, L"Comma", "Comma"},   {VK_OEM_PERIOD, L"Period", "Period"},
    {VK_OEM_7, L"Apostrophe", "Apostrophe"}, {VK_OEM_1, L"Semicolon", "Semicolon"},
    {VK_OEM_5, L"Backslash", "Backslash"},   {VK_OEM_3, L"Grave", "Grave"},
};

std::wstring trim(const std::wstring& s) {
    const size_t a = s.find_first_not_of(L" \t"), b = s.find_last_not_of(L" \t");
    return a == std::wstring::npos ? std::wstring() : s.substr(a, b - a + 1);
}

}  // namespace

int parse_key(const wchar_t* name) {
    for (const Named& k : kNamed)
        if (_wcsicmp(name, k.name) == 0) return k.vk;
    if ((name[0] == L'F' || name[0] == L'f') && name[1]) {
        wchar_t* end = nullptr;
        const long n = std::wcstol(name + 1, &end, 10);
        if (*end == L'\0' && n >= 1 && n <= 12) return VK_F1 + static_cast<int>(n) - 1;
    }
    if (_wcsnicmp(name, L"Numpad", 6) == 0 && name[6] >= L'0' && name[6] <= L'9' && !name[7])
        return VK_NUMPAD0 + (name[6] - L'0');
    if (name[0] && !name[1]) {
        const wchar_t c = static_cast<wchar_t>(std::towupper(name[0]));
        if ((c >= L'A' && c <= L'Z') || (c >= L'0' && c <= L'9')) return c;
    }
    return 0;
}

const char* key_name(int vk) {
    for (const Named& k : kNamed)
        if (k.vk == vk) return k.narrow;
    static char buf[8][12];  // a few in flight in one log line
    static int slot = 0;
    char* out = buf[slot++ & 7];
    if (vk >= VK_F1 && vk <= VK_F12) std::snprintf(out, 12, "F%d", vk - VK_F1 + 1);
    else if (vk >= VK_NUMPAD0 && vk <= VK_NUMPAD9) std::snprintf(out, 12, "Numpad%d", vk - VK_NUMPAD0);
    else if ((vk >= 'A' && vk <= 'Z') || (vk >= '0' && vk <= '9')) std::snprintf(out, 12, "%c", vk);
    else std::snprintf(out, 12, "?");
    return out;
}

bool parse_binding(const wchar_t* text, Binding* out) {
    *out = Binding{};
    const std::wstring t = trim(text ? text : L"");
    if (t.empty() || _wcsicmp(t.c_str(), L"none") == 0) return true;
    if (_wcsicmp(t.c_str(), L"recentre") == 0 || _wcsicmp(t.c_str(), L"recenter") == 0) {
        out->kind = Binding::Recentre;
        return true;
    }
    if (_wcsicmp(t.c_str(), L"world_scale_up") == 0) return out->kind = Binding::WorldScaleUp, true;
    if (_wcsicmp(t.c_str(), L"world_scale_down") == 0) return out->kind = Binding::WorldScaleDown, true;
    if (_wcsnicmp(t.c_str(), L"menu:", 5) == 0) {
        const int menu = menus::parse(trim(t.substr(5)).c_str());
        if (menu < 0) return false;
        out->kind = Binding::Menu;
        out->menu = menu;
        return true;
    }
    size_t start = 0;
    while (start <= t.size()) {
        const size_t plus = t.find(L'+', start);
        const std::wstring part = trim(t.substr(start, plus == std::wstring::npos ? std::wstring::npos : plus - start));
        const int vk = parse_key(part.c_str());
        if (!vk || out->count >= 4) {
            *out = Binding{};
            return false;
        }
        out->vk[out->count++] = static_cast<unsigned char>(vk);
        if (plus == std::wstring::npos) break;
        start = plus + 1;
    }
    out->kind = Binding::Keys;
    return true;
}

void describe(const Binding& b, char* out, int size) {
    switch (b.kind) {
        case Binding::None: std::snprintf(out, size, "none"); return;
        case Binding::Recentre: std::snprintf(out, size, "recentre"); return;
        case Binding::WorldScaleUp: std::snprintf(out, size, "world_scale_up"); return;
        case Binding::WorldScaleDown: std::snprintf(out, size, "world_scale_down"); return;
        case Binding::Menu: std::snprintf(out, size, "menu:%s", menus::name(b.menu)); return;
        case Binding::Keys: break;
    }
    out[0] = '\0';
    for (int i = 0; i < b.count; ++i) {
        const size_t len = std::strlen(out);
        std::snprintf(out + len, size - len, "%s%s", i ? "+" : "", key_name(b.vk[i]));
    }
}

}  // namespace hw2vr::keys
