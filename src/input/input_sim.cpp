#include "input/input_sim.h"

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

}  // namespace

bool game_foreground() {
    DWORD pid = 0;
    GetWindowThreadProcessId(GetForegroundWindow(), &pid);
    return pid == GetCurrentProcessId();
}

bool cursor_over(HWND game) {
    POINT p;
    if (!GetCursorPos(&p)) return false;
    return GetAncestor(WindowFromPoint(p), GA_ROOT) == GetAncestor(game, GA_ROOT);
}

void set_cursor_client(HWND game, int x, int y) {
    POINT p{x, y};
    if (ClientToScreen(game, &p)) SetCursorPos(p.x, p.y);
}

bool cursor_client(HWND game, int* x, int* y) {
    POINT p;
    if (!GetCursorPos(&p) || !ScreenToClient(game, &p)) return false;
    *x = p.x;
    *y = p.y;
    return true;
}

void move_relative(int dx, int dy) {
    if (dx || dy) send_mouse(MOUSEEVENTF_MOVE, dx, dy, 0);
}

void mouse_button(int button, bool down) {
    const DWORD flags = button == 0   ? (down ? MOUSEEVENTF_LEFTDOWN : MOUSEEVENTF_LEFTUP)
                        : button == 1 ? (down ? MOUSEEVENTF_RIGHTDOWN : MOUSEEVENTF_RIGHTUP)
                                      : (down ? MOUSEEVENTF_MIDDLEDOWN : MOUSEEVENTF_MIDDLEUP);
    send_mouse(flags, 0, 0, 0);
}

void wheel(int notches) {
    if (notches) send_mouse(MOUSEEVENTF_WHEEL, 0, 0, static_cast<DWORD>(notches * WHEEL_DELTA));
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
