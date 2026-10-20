#include "input/input_sim.h"

#include <cstdlib>
#include <cstring>
#include <deque>

#include "common/log.h"

namespace hw2vr::inputsim {
namespace {

void send_mouse(DWORD flags, LONG dx, LONG dy, DWORD data) {
    INPUT in{};
    in.type = INPUT_MOUSE;
    in.mi.dx = dx;
    in.mi.dy = dy;
    in.mi.mouseData = data;
    in.mi.dwFlags = flags;
    SendInput(1, &in, sizeof in);
}

bool extended(int vk) {
    switch (vk) {
        case VK_INSERT: case VK_DELETE: case VK_HOME: case VK_END: case VK_PRIOR: case VK_NEXT:
        case VK_LEFT: case VK_RIGHT: case VK_UP: case VK_DOWN: case VK_NUMLOCK: case VK_DIVIDE:
        case VK_RCONTROL: case VK_RMENU:
            return true;
        default:
            return false;
    }
}

INPUT key_event(int vk, bool down) {
    INPUT in{};
    in.type = INPUT_KEYBOARD;
    in.ki.wVk = static_cast<WORD>(vk);
    in.ki.wScan = static_cast<WORD>(MapVirtualKeyW(static_cast<UINT>(vk), MAPVK_VK_TO_VSC));
    in.ki.dwFlags = (down ? 0 : KEYEVENTF_KEYUP) | (extended(vk) ? KEYEVENTF_EXTENDEDKEY : 0);
    return in;
}

// --- Window mode ---------------------------------------------------------------------------------------------------

constexpr UINT kDeliver = WM_APP + 0x3A7;  // "deliver the next queued mouse event" (posted, so it keeps its place)

struct Event {
    UINT msg;
    WPARAM wparam;
    LPARAM lparam;
    bool relative = false;  // a move by (dx, dy) from wherever the game's cursor is when it's delivered
    int dx = 0, dy = 0;
};

bool g_window_mode = false;
HWND g_window = nullptr;
WNDPROC g_game_proc = nullptr;
std::deque<Event> g_events;
int g_x = 0, g_y = 0;          // the game's cursor as far as we know (client pixels)
WPARAM g_buttons = 0;          // MK_LBUTTON / MK_RBUTTON / MK_MBUTTON held through us
DWORD g_last_left_down = 0;    // double-click detection (Windows only makes WM_LBUTTONDBLCLK from real input)
int g_last_left_x = 0, g_last_left_y = 0;
bool g_hooks_tried = false;

using PFN_SetCursorPos = BOOL(WINAPI*)(int, int);
using PFN_ClipCursor = BOOL(WINAPI*)(const RECT*);
PFN_SetCursorPos g_real_set_cursor_pos = nullptr;
PFN_ClipCursor g_real_clip_cursor = nullptr;

bool is_mouse_message(UINT msg) {
    return (msg >= WM_MOUSEFIRST && msg <= WM_MOUSELAST) || msg == WM_MOUSELEAVE || msg == WM_MOUSEHOVER;
}

LRESULT CALLBACK window_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    if (msg == kDeliver) {
        if (g_events.empty()) return 0;
        Event e = g_events.front();
        g_events.pop_front();
        if (e.relative) {
            // Resolved now: in the game's relative mode its warp after the previous move has just put the cursor
            // back on the anchor (hook_set_cursor_pos).
            g_x += e.dx, g_y += e.dy;
            e.lparam = MAKELPARAM(static_cast<short>(g_x), static_cast<short>(g_y));
        }
        return CallWindowProcW(g_game_proc, hwnd, e.msg, e.wparam, e.lparam);
    }
    if (g_window_mode && is_mouse_message(msg)) return 0;  // the real mouse: the laser drives the game now
    return CallWindowProcW(g_game_proc, hwnd, msg, wparam, lparam);
}

// The game warping its cursor (the right-drag turn puts it back on its anchor after every move): only remember where.
BOOL WINAPI hook_set_cursor_pos(int x, int y) {
    if (!g_window_mode || !g_window) return g_real_set_cursor_pos(x, y);
    POINT p{x, y};
    if (ScreenToClient(g_window, &p)) g_x = p.x, g_y = p.y;
    return TRUE;
}

BOOL WINAPI hook_clip_cursor(const RECT* rect) {
    if (!g_window_mode) return g_real_clip_cursor(rect);
    return TRUE;
}

// The import table slot of `name` from `dll` in a loaded module (by name, so an app-compat shim in the slot doesn't
// matter), or nullptr.
void** import_slot(HMODULE module, const char* dll, const char* name) {
    auto* base = reinterpret_cast<BYTE*>(module);
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + reinterpret_cast<const IMAGE_DOS_HEADER*>(base)->e_lfanew);
    const IMAGE_DATA_DIRECTORY& dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (!dir.VirtualAddress) return nullptr;
    for (auto* d = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(base + dir.VirtualAddress); d->Name; ++d) {
        if (_stricmp(reinterpret_cast<const char*>(base + d->Name), dll) != 0 || !d->OriginalFirstThunk) continue;
        auto* names = reinterpret_cast<IMAGE_THUNK_DATA*>(base + d->OriginalFirstThunk);
        auto* slots = reinterpret_cast<IMAGE_THUNK_DATA*>(base + d->FirstThunk);
        for (; names->u1.AddressOfData; ++names, ++slots) {
            if (IMAGE_SNAP_BY_ORDINAL(names->u1.Ordinal)) continue;
            const auto* by_name = reinterpret_cast<const IMAGE_IMPORT_BY_NAME*>(base + names->u1.AddressOfData);
            if (std::strcmp(reinterpret_cast<const char*>(by_name->Name), name) == 0)
                return reinterpret_cast<void**>(&slots->u1.Function);
        }
    }
    return nullptr;
}

template <class Fn>
bool hook_import(const char* name, Fn hook, Fn* real) {
    void** slot = import_slot(GetModuleHandleW(nullptr), "USER32.dll", name);
    if (!slot) return false;
    DWORD old = 0;
    if (!VirtualProtect(slot, sizeof(void*), PAGE_READWRITE, &old)) return false;
    *real = reinterpret_cast<Fn>(*slot);
    *slot = reinterpret_cast<void*>(hook);
    VirtualProtect(slot, sizeof(void*), old, &old);
    return true;
}

void install(HWND game) {
    if (g_hooks_tried) return;
    g_hooks_tried = true;
    const bool warp = hook_import("SetCursorPos", &hook_set_cursor_pos, &g_real_set_cursor_pos);
    const bool clip = hook_import("ClipCursor", &hook_clip_cursor, &g_real_clip_cursor);
    g_game_proc = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(game, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&window_proc)));
    HW2VR_LOG("input: window mode set up - window procedure %s, the game's SetCursorPos %s, ClipCursor %s",
              g_game_proc ? "subclassed" : "NOT subclassed", warp ? "hooked" : "NOT hooked", clip ? "hooked" : "NOT hooked");
}

WPARAM key_state() {
    WPARAM s = g_buttons;
    if (GetAsyncKeyState(VK_SHIFT) & 0x8000) s |= MK_SHIFT;
    if (GetAsyncKeyState(VK_CONTROL) & 0x8000) s |= MK_CONTROL;
    return s;
}

LPARAM client_lparam() { return MAKELPARAM(static_cast<short>(g_x), static_cast<short>(g_y)); }

void queue(const Event& e) {
    g_events.push_back(e);
    PostMessageW(g_window, kDeliver, 0, 0);
}
void queue(UINT msg, WPARAM wparam, LPARAM lparam) { queue(Event{msg, wparam, lparam}); }

void queue_move() { queue(WM_MOUSEMOVE, key_state(), client_lparam()); }

}  // namespace

void set_window_mode(HWND game, bool on) {
    if (on && game) {
        install(game);
        if (!g_game_proc) return;  // can't deliver: stay with the real cursor
        g_window = game;
    }
    if (on == g_window_mode) return;
    g_window_mode = on && g_game_proc;
    g_buttons = 0;
    HW2VR_LOG("input: mouse %s", g_window_mode ? "events go straight to the game window (window mode)" : "is the real cursor");
}

bool window_mode() { return g_window_mode; }

bool game_foreground() {
    DWORD pid = 0;
    GetWindowThreadProcessId(GetForegroundWindow(), &pid);
    return pid == GetCurrentProcessId();
}

bool cursor_over(HWND game) {
    if (g_window_mode) return game != nullptr;
    POINT p;
    if (!GetCursorPos(&p)) return false;
    return GetAncestor(WindowFromPoint(p), GA_ROOT) == GetAncestor(game, GA_ROOT);
}

void set_cursor_client(HWND game, int x, int y) {
    if (g_window_mode) {
        g_x = x, g_y = y;
        queue_move();
        return;
    }
    POINT p{x, y};
    if (ClientToScreen(game, &p)) SetCursorPos(p.x, p.y);
}

bool cursor_client(HWND game, int* x, int* y) {
    if (g_window_mode) {
        *x = g_x, *y = g_y;
        return true;
    }
    POINT p;
    if (!GetCursorPos(&p) || !ScreenToClient(game, &p)) return false;
    *x = p.x;
    *y = p.y;
    return true;
}

void move_relative(int dx, int dy) {
    if (!dx && !dy) return;
    if (g_window_mode) {
        Event e{WM_MOUSEMOVE, key_state(), 0};
        e.relative = true, e.dx = dx, e.dy = dy;
        queue(e);
        return;
    }
    send_mouse(MOUSEEVENTF_MOVE, dx, dy, 0);
}

void mouse_button(int button, bool down) {
    if (g_window_mode) {
        static constexpr WPARAM kBit[3] = {MK_LBUTTON, MK_RBUTTON, MK_MBUTTON};
        static constexpr UINT kDown[3] = {WM_LBUTTONDOWN, WM_RBUTTONDOWN, WM_MBUTTONDOWN};
        static constexpr UINT kUp[3] = {WM_LBUTTONUP, WM_RBUTTONUP, WM_MBUTTONUP};
        UINT msg = down ? kDown[button] : kUp[button];
        if (down && button == 0) {
            // A second press soon after and close to the first is a double click (select all of that type), as
            // Windows would report it for a window class that asks for them.
            const DWORD now = GetTickCount();
            const bool wants = (GetClassLongPtrW(g_window, GCL_STYLE) & CS_DBLCLKS) != 0;
            if (wants && now - g_last_left_down <= GetDoubleClickTime() &&
                std::abs(g_x - g_last_left_x) <= GetSystemMetrics(SM_CXDOUBLECLK) / 2 &&
                std::abs(g_y - g_last_left_y) <= GetSystemMetrics(SM_CYDOUBLECLK) / 2) {
                msg = WM_LBUTTONDBLCLK;
                g_last_left_down = 0;
            } else {
                g_last_left_down = now;
            }
            g_last_left_x = g_x, g_last_left_y = g_y;
        }
        g_buttons = down ? (g_buttons | kBit[button]) : (g_buttons & ~kBit[button]);
        queue(msg, key_state(), client_lparam());
        return;
    }
    const DWORD flags = button == 0   ? (down ? MOUSEEVENTF_LEFTDOWN : MOUSEEVENTF_LEFTUP)
                        : button == 1 ? (down ? MOUSEEVENTF_RIGHTDOWN : MOUSEEVENTF_RIGHTUP)
                                      : (down ? MOUSEEVENTF_MIDDLEDOWN : MOUSEEVENTF_MIDDLEUP);
    send_mouse(flags, 0, 0, 0);
}

void wheel(int notches) {
    if (!notches) return;
    if (g_window_mode) {
        POINT p{g_x, g_y};
        ClientToScreen(g_window, &p);
        queue(WM_MOUSEWHEEL, MAKEWPARAM(static_cast<WORD>(key_state()), static_cast<WORD>(static_cast<short>(notches * WHEEL_DELTA))),
              MAKELPARAM(static_cast<short>(p.x), static_cast<short>(p.y)));
        return;
    }
    send_mouse(MOUSEEVENTF_WHEEL, 0, 0, static_cast<DWORD>(notches * WHEEL_DELTA));
}

bool bring_to_front(HWND game) {
    const HWND root = GetAncestor(game, GA_ROOT);
    if (!root) return false;
    // Windows lets a background process take the focus only from the thread that has it: share input state with
    // that thread for the call.
    const HWND fg = GetForegroundWindow();
    const DWORD fg_thread = fg ? GetWindowThreadProcessId(fg, nullptr) : 0;
    const DWORD self = GetCurrentThreadId();
    const bool attached = fg_thread && fg_thread != self && AttachThreadInput(self, fg_thread, TRUE);
    if (IsIconic(root)) ShowWindow(root, SW_RESTORE);
    BringWindowToTop(root);
    SetForegroundWindow(root);
    if (attached) AttachThreadInput(self, fg_thread, FALSE);
    return game_foreground();
}

void keys(const keys::Binding& combo, bool down) {
    if (combo.kind != keys::Binding::Keys || combo.count <= 0) return;
    INPUT events[4];
    for (int i = 0; i < combo.count; ++i) events[i] = key_event(combo.vk[down ? i : combo.count - 1 - i], down);
    SendInput(static_cast<UINT>(combo.count), events, sizeof(INPUT));
}

}  // namespace hw2vr::inputsim
