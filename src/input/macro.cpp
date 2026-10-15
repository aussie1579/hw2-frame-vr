#include "input/macro.h"

#include <deque>
#include <functional>

#include "input/input_sim.h"

namespace hw2vr::macro {
namespace {

struct Step {
    int wait;  // frames to wait before running it
    std::function<void()> run;
};
std::deque<Step> g_steps;
keys::Binding g_held;        // a modifier the queue is holding down
bool g_button_down[2] = {};  // mouse buttons the queue is holding down

void key(const keys::Binding& b, bool down) {
    inputsim::keys(b, down);
    g_held = down ? b : keys::Binding{};
}

void click_steps(HWND window, int x, int y, int button) {
    g_steps.push_back({0, [=] { inputsim::set_cursor_client(window, x, y); }});
    g_steps.push_back({1, [=] {
                           inputsim::mouse_button(button, true);
                           g_button_down[button] = true;
                       }});
    g_steps.push_back({1, [=] {
                           inputsim::mouse_button(button, false);
                           g_button_down[button] = false;
                       }});
}

}  // namespace

void tap(const keys::Binding& b) {
    if (b.kind != keys::Binding::Keys) return;
    g_steps.push_back({0, [b] { key(b, true); }});
    g_steps.push_back({1, [b] { key(b, false); }});
}

void click_with(const keys::Binding& modifier, HWND window, int x, int y, int button) {
    const bool mod = modifier.kind == keys::Binding::Keys;
    if (mod) g_steps.push_back({0, [modifier] { key(modifier, true); }});
    click_steps(window, x, y, button);
    if (mod) g_steps.push_back({1, [modifier] { key(modifier, false); }});
}

void key_then_click(const keys::Binding& b, HWND window, int x, int y) {
    tap(b);
    // Let the game enter the mode, see the cursor move onto the spot, then confirm there.
    g_steps.push_back({3, [=] { inputsim::set_cursor_client(window, x + 1, y); }});
    click_steps(window, x, y, 0);
}

void run() {
    if (g_steps.empty()) return;
    Step& s = g_steps.front();
    if (s.wait > 0) {
        --s.wait;
        return;
    }
    const auto fn = std::move(s.run);
    g_steps.pop_front();
    fn();
    // Steps due on the same frame run together.
    while (!g_steps.empty() && g_steps.front().wait == 0) {
        const auto next = std::move(g_steps.front().run);
        g_steps.pop_front();
        next();
    }
}

bool busy() { return !g_steps.empty(); }

void clear() {
    g_steps.clear();
    for (int b = 0; b < 2; ++b)
        if (g_button_down[b]) inputsim::mouse_button(b, false), g_button_down[b] = false;
    if (g_held.kind == keys::Binding::Keys) inputsim::keys(g_held, false);
    g_held = keys::Binding{};
}

}  // namespace hw2vr::macro
