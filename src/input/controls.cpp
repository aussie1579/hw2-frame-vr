#include "input/controls.h"

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <string>

#include "common/log.h"
#include "common/mat4.h"
#include "common/one_euro.h"
#include "common/vec3.h"
#include "hud/hud_layer.h"
#include "hud/hud_panel.h"
#include "hud/screen_windows.h"
#include "input/command_menus.h"
#include "input/input_sim.h"
#include "input/laser_math.h"
#include "input/macro.h"
#include "input/radial_menu.h"
#include "input/sprites.h"
#include "input/tablet.h"
#include "proxy/gl_api.h"
#include "stereo/depth_probe.h"
#include "vr/overlay.h"
#include "vr/vr_input.h"

namespace hw2vr::controls {
namespace {

constexpr float kPi = 3.14159265358979f;
constexpr float kBeamThickness = 0.004f;  // metres
constexpr float kBarGap = 0.05f, kBarMargin = 0.02f;
constexpr float kEdgeMargin = 0.02f;      // world aim stays this fraction inside the window (no edge panning)
constexpr long long kTipMs = 20000;
constexpr float kMenuDistance = 0.6f;     // metres from the head: a command menu opens there, on the laser
constexpr long long kChordMs = 300;       // a second D-pad direction this soon after the first is a diagonal
// While dragging the panel by its bar, at full stick:
constexpr float kPushSpeed = 1.0f;   // metres per second along the laser
constexpr float kSizeSpeed = 0.6f;   // fraction of the width per second
constexpr float kCurveSpeed = 0.15f; // curvature per second

const wchar_t* const kTip =
    L"HOMEWORLD 2 VR\n"
    L"Laser = mouse:  bumper = left click,  trigger = right click (hold it and move to turn the camera)\n"
    L"Left stick: orbit the camera     Right stick: zoom     D-pad (hold): command menus\n"
    L"To move the screen: point at the bar next to it, hold the bumper";
const wchar_t* const kBarHelp =
    L"MOVE THE SCREEN\n"
    L"Hold the right bumper on this bar and move your hand\n"
    L"While holding:\n"
    L"Left stick up / down:  push away / pull closer\n"
    L"Left stick left / right:  smaller / bigger\n"
    L"Right stick left / right:  flatter / more curved\n"
    L"Right stick click:  flat, or curved round you";

// What the laser is on, in priority order: an open command menu, the wrist tablet, a game window's Close tab, a game
// window (pop-up menu, status strip, build/research/launch), the grab bar, the panel showing the screen UI, the world.
enum class Aim { None, Panel, World, Bar, Tablet, Button, Window, Menu };

Settings g_s;
bool g_started = false;

vr::Overlay g_beam, g_cursor, g_bar, g_hint;
gl::GLuint g_beam_tex = 0, g_bar_tex = 0, g_hint_tex = 0;
const wchar_t* g_hint_text = nullptr;
float g_hint_aspect = 0.0f;  // height / width of the hint image

OneEuroFilter g_fx, g_fy;
Aim g_prev_aim = Aim::None;
long long g_freeze_until = 0;
int g_cursor_x = INT_MIN, g_cursor_y = INT_MIN;  // last client position we put the cursor at

// Bumper = the left mouse button. A press holds the cursor where it went down until the laser moves past
// drag_degrees, so a click never turns into a tiny drag (box select / camera nudge); further than that it drags.
// Never in the first click_ms: pressing the bumper jolts the controller by a few degrees.
bool g_left_down = false;
bool g_sticky = false;
Vec3 g_press_dir;
long long g_press_at = 0;

// Trigger = the right mouse button. On the world a tap is the game's right click (its pop-up menu); held and moved
// past swivel_degrees, the laser's turning becomes mouse motion with the button down - the game's own right-drag
// camera turn (docs/FINDINGS.md "Input": relative mode). On a game window it is just the right button.
struct RightButton {
    bool down = false;
    bool world = false;   // went down on the world
    bool swivel = false;  // turning the camera
    Vec3 press_dir, last_dir;
    double acc[2] = {};   // mouse counts not sent yet
};
RightButton g_right;

bool g_orbiting = false;
double g_orbit_acc[2] = {}, g_zoom_acc = 0.0;
double g_orbit_rate[2] = {};  // eased towards the stick's rate, so the view never jerks into motion

// The D-pad's command menus: the open one, the button holding it open and the target its orders apply to (the
// cursor when it opened, valid if the laser was on the world).
int g_menu = -1, g_menu_button = -1, g_menu_slice = -1;
long long g_menu_opened_at = 0;
int g_anchor_x = 0, g_anchor_y = 0;
bool g_anchor_valid = false;
bool g_held[input::kButtonCount] = {};        // a held binding (keys, middle button) is down for this button
bool g_down_seen[input::kButtonCount] = {};   // the press was seen (holds are timed from it)
long long g_pressed_at[input::kButtonCount] = {};
bool g_hold_fired[input::kButtonCount] = {};

bool g_dragging = false;
float g_grab_distance = 0.0f;
Vec3 g_grab_offset;        // grabbed spot relative to the bar centre, in panel axes (right, up, normal)
long long g_tip_until = -1;
bool g_logged_foreground = false, g_logged_window = false;
float g_cursor_depth = 0.0f;  // metres from the game camera: where the world cursor floats (last touch)

long long now_ms() { return static_cast<long long>(GetTickCount64()); }

float axis(float v, float deadzone) {
    return std::fabs(v) < deadzone ? 0.0f : (v - std::copysign(deadzone, v)) / (1.0f - deadzone);
}

// The bar's rectangle next to the panel, facing the same way: above a console that sits below eye level (between it
// and the battlefield), under a panel at eye height.
input::PanelGeometry bar_layout(const input::PanelGeometry& p) {
    input::PanelGeometry b = p;
    b.curvature = 0.0f;
    b.width = std::clamp(p.width * 0.12f, 0.18f, 0.5f);
    b.height = b.width * 32.0f / 256.0f;
    const float side = panel::bar_above() ? 1.0f : -1.0f;
    b.centre = p.centre + p.up * (side * (p.height / 2.0f + kBarGap + b.height / 2.0f));
    return b;
}

void close_menu() {
    if (g_menu >= 0) radial::close();
    g_menu = g_menu_button = g_menu_slice = -1;
}

void release_mouse() {
    g_sticky = false;
    macro::clear();
    close_menu();
    if (g_left_down) inputsim::mouse_button(0, false), g_left_down = false;
    if (g_right.down) {
        inputsim::mouse_button(1, false);
        screens::popup_cancel();
        g_right = RightButton{};
    }
    if (g_orbiting) inputsim::mouse_button(1, false), g_orbiting = false;
}

void release_binding(int i) {
    if (!g_held[i]) return;
    if (g_s.buttons[i].kind == keys::Binding::MiddleClick) inputsim::mouse_button(2, false);
    else inputsim::keys(g_s.buttons[i], false);
    g_held[i] = false;
}

void release_keys() {
    for (int i = 0; i < input::kButtonCount; ++i) release_binding(i);
}

void show_beam(const Vec3& origin, const Vec3& dir, float length, const Vec3& eye) {
    if (!g_s.show_beam || length < 0.01f) return g_beam.hide();
    const Vec3 centre = origin + dir * (length / 2.0f);
    // Face the strip towards the eye, turning only round the beam's own axis.
    const Vec3 to_eye = eye - centre;
    Vec3 normal = to_eye - dir * dot(to_eye, dir);
    if (dot(normal, normal) < 1e-8f) return g_beam.hide();
    normal = normalize(normal);
    float pose[16];
    pose_from_axes(dir, cross(normal, dir), normal, centre, pose);
    g_beam.set_width(length);
    g_beam.set_texel_aspect(length * 8.0f / (64.0f * kBeamThickness));  // beam texture is 64 x 8
    g_beam.place(pose);
    g_beam.show();
}

// A sprite facing the eye at `point`, `width` metres wide, level with the horizon.
void face_eye_pose(const Vec3& point, const Vec3& eye, float out[16]) {
    Vec3 n = eye - point;
    n = dot(n, n) < 1e-8f ? Vec3{0, 0, 1} : normalize(n);
    Vec3 r = cross(Vec3{0, 1, 0}, n);
    r = dot(r, r) < 1e-8f ? Vec3{1, 0, 0} : normalize(r);
    pose_from_axes(r, cross(n, r), n, point, out);
}

void set_hint(const wchar_t* text) {
    if (text == g_hint_text) return;
    g_hint_text = text;
    if (!text) return g_hint.hide();
    const sprites::Image img = sprites::text_panel(text, 900);
    if (img.pixels.empty()) return;
    g_hint_tex = gl::upload_bgra(g_hint_tex, img.width, img.height, img.pixels.data());
    g_hint_aspect = static_cast<float>(img.height) / static_cast<float>(img.width);
    g_hint.set_texture(g_hint_tex);
}

void place_hint(const input::PanelGeometry& p) {
    if (!g_hint_text) return;
    const input::PanelGeometry bar = bar_layout(p);
    const float width = std::clamp(0.45f * panel::placement().distance, 0.4f, 1.6f);
    const float height = width * g_hint_aspect;
    const float side = panel::bar_above() ? 1.0f : -1.0f;  // help on the far side of the bar from the panel
    const Vec3 centre = bar.centre + p.up * (side * (bar.height / 2.0f + 0.03f + height / 2.0f)) + p.normal * 0.05f;
    float pose[16];
    pose_from_axes(p.right, p.up, p.normal, centre, pose);
    g_hint.set_width(width);
    g_hint.place(pose);
    g_hint.show();
}

// Moving the panel by its bar: the grabbed spot stays on the end of the laser; the sticks push/pull, size and curve.
void drag_panel(const vrinput::State& st, const Vec3& origin, const Vec3& dir, float dt) {
    panel::Placement& pl = panel::placement();
    const float push = axis(st.camera[1], g_s.stick_deadzone), grow = axis(st.camera[0], g_s.stick_deadzone);
    const float bend = axis(st.zoom[0], g_s.stick_deadzone);
    pl.width = pl.width * (1.0f + grow * kSizeSpeed * dt);
    pl.curvature = pl.curvature + bend * kCurveSpeed * dt;
    if (st.buttons[input::kRightStick].pressed) panel::place_flat_or_centred();
    g_grab_distance = std::clamp(g_grab_distance + push * kPushSpeed * dt, 0.3f, 10.0f);
    panel::apply();  // new size first, so the grab offset below is measured on it

    const input::PanelGeometry& g = panel::geometry();
    const input::PanelGeometry bar = bar_layout(g);
    const Vec3 grab_from_centre =
        bar.centre - g.centre + g.right * g_grab_offset.x + g.up * g_grab_offset.y + g.normal * g_grab_offset.z;
    const Vec3 target = origin + dir * g_grab_distance;
    input::PanelGeometry::relative(panel::anchor(), target - grab_from_centre, &pl.distance, &pl.side, &pl.height);
    panel::apply();
}

void start_drag(const Vec3& origin, const Vec3& dir, float distance) {
    const input::PanelGeometry& g = panel::geometry();
    const Vec3 rel = origin + dir * distance - bar_layout(g).centre;
    g_grab_offset = {dot(rel, g.right), dot(rel, g.up), dot(rel, g.normal)};
    g_grab_distance = distance;
    g_dragging = true;
    HW2VR_LOG("controls: moving the panel (grab bar)");
}

// Puts the game's cursor at a client pixel (filtered), unless held by a press or frozen after a release.
void aim_cursor(const Frame& f, float px, float py, double dt, bool reset_filter) {
    if (reset_filter) g_fx.reset(), g_fy.reset();
    const double fx = g_fx.filter(px, dt), fy = g_fy.filter(py, dt);
    if (g_sticky || now_ms() < g_freeze_until) return;
    const int x = std::clamp(static_cast<int>(std::floor(fx)), 0, f.client_w - 1);
    const int y = std::clamp(static_cast<int>(std::floor(fy)), 0, f.client_h - 1);
    if (x == g_cursor_x && y == g_cursor_y) return;
    g_cursor_x = x;
    g_cursor_y = y;
    inputsim::set_cursor_client(f.window, x, y);
}

// Where the game's cursor is: where we last put it, or where Windows says.
void cursor_position(const Frame& f, int* x, int* y) {
    *x = g_cursor_x, *y = g_cursor_y;
    if (*x == INT_MIN && !inputsim::cursor_client(f.window, x, y)) *x = f.client_w / 2, *y = f.client_h / 2;
}

// Bumper: the left mouse button.
void left_button(const vrinput::ButtonState& b, bool can_press, const Vec3& dir) {
    if (b.pressed && can_press && !g_left_down) {
        inputsim::mouse_button(0, true);
        g_left_down = true;
        g_sticky = true;
        g_press_dir = dir;
        g_press_at = now_ms();
        if (g_s.haptics) vrinput::pulse(0.01f, 0.4f);
    }
    if (!b.down && g_left_down) {
        inputsim::mouse_button(0, false);
        g_left_down = false;
        g_sticky = false;
        g_freeze_until = now_ms() + g_s.click_freeze_ms;
        if (g_s.haptics) vrinput::pulse(0.005f, 0.2f);
    }
}

// The laser's heading, degrees: yaw to the right, pitch up.
void yaw_pitch(const Vec3& d, double* yaw, double* pitch) {
    *yaw = std::atan2(static_cast<double>(d.x), -static_cast<double>(d.z)) * 180.0 / kPi;
    *pitch = std::asin(std::clamp(static_cast<double>(d.y), -1.0, 1.0)) * 180.0 / kPi;
}

// Trigger: the right mouse button (see RightButton).
void right_button(const Frame& f, const vrinput::ButtonState& b, bool can_press, Aim aim, bool pointer_valid,
                  const Vec3& origin, const Vec3& dir) {
    if (b.pressed && can_press && !g_right.down) {
        inputsim::mouse_button(1, true);
        g_right = RightButton{};
        g_right.down = true;
        g_right.world = aim == Aim::World;
        g_right.press_dir = g_right.last_dir = dir;
        if (g_right.world) {
            // The game may answer with its pop-up menu: watch the screen UI round the cursor from now on.
            int x, y;
            cursor_position(f, &x, &y);
            screens::popup_watch((static_cast<float>(x) + 0.5f) / static_cast<float>(f.client_w),
                                 (static_cast<float>(y) + 0.5f) / static_cast<float>(f.client_h), origin, dir);
        }
        if (g_s.haptics) vrinput::pulse(0.01f, 0.4f);
    }
    if (!g_right.down) return;
    if (!b.down) {
        inputsim::mouse_button(1, false);
        if (g_right.swivel) HW2VR_LOG("controls: camera turn done");
        else if (g_right.world) screens::popup_released();
        g_right = RightButton{};
        g_freeze_until = now_ms() + g_s.click_freeze_ms;
        if (g_s.haptics) vrinput::pulse(0.005f, 0.2f);
        return;
    }
    if (!g_right.world || !pointer_valid) return;
    if (!g_right.swivel) {
        if (dot(dir, g_right.press_dir) >= std::cos(g_s.swivel_degrees * kPi / 180.0f)) return;  // still a click
        g_right.swivel = true;
        g_right.last_dir = dir;  // turning starts from here: the dead zone doesn't jump the view
        screens::popup_cancel();
        if (g_s.haptics) vrinput::pulse(0.008f, 0.3f);
        HW2VR_LOG("controls: trigger held and moved - turning the camera");
        return;
    }
    double yaw0, pitch0, yaw1, pitch1;
    yaw_pitch(g_right.last_dir, &yaw0, &pitch0);
    yaw_pitch(dir, &yaw1, &pitch1);
    g_right.last_dir = dir;
    double dyaw = yaw1 - yaw0;
    if (dyaw > 180.0) dyaw -= 360.0;
    if (dyaw < -180.0) dyaw += 360.0;
    g_right.acc[0] += dyaw * g_s.swivel_speed * (g_s.swivel_invert_x ? -1.0 : 1.0);
    g_right.acc[1] -= (pitch1 - pitch0) * g_s.swivel_speed * (g_s.swivel_invert_y ? -1.0 : 1.0);  // laser up = mouse up
    const int dx = static_cast<int>(g_right.acc[0]), dy = static_cast<int>(g_right.acc[1]);
    g_right.acc[0] -= dx;
    g_right.acc[1] -= dy;
    inputsim::move_relative(dx, dy);
}

// Left stick: orbit by holding the right mouse button and moving the mouse, as the game's own right-drag.
void orbit(const Frame& f, const vrinput::State& st, bool allowed) {
    const float x = axis(st.camera[0], g_s.stick_deadzone), y = axis(st.camera[1], g_s.stick_deadzone);
    const float magnitude = std::sqrt(st.camera[0] * st.camera[0] + st.camera[1] * st.camera[1]);
    const bool want = allowed && (g_orbiting ? magnitude > g_s.stick_deadzone * 0.75f : (x != 0.0f || y != 0.0f));
    if (want && !g_orbiting) {
        // Start from the middle of the window so the right button goes down on the world, not on a UI element.
        inputsim::set_cursor_client(f.window, f.client_w / 2, f.client_h / 2);
        g_cursor_x = g_cursor_y = INT_MIN;
        inputsim::mouse_button(1, true);
        g_orbiting = true;
        g_orbit_acc[0] = g_orbit_acc[1] = 0.0;
        g_orbit_rate[0] = g_orbit_rate[1] = 0.0;
    } else if (!want && g_orbiting) {
        inputsim::mouse_button(1, false);
        g_orbiting = false;
        return;
    }
    if (!g_orbiting) return;
    // Curved response (fine control near the centre, full speed at the edge), eased over ~0.15 s.
    auto curve = [&](float v) { return std::copysign(std::pow(std::fabs(v), g_s.camera_curve), v); };
    const double target[2] = {curve(x) * g_s.camera_speed, (g_s.camera_invert_y ? 1.0 : -1.0) * curve(y) * g_s.camera_speed};
    const double ease = std::min(1.0, f.dt / 0.15);
    for (int i = 0; i < 2; ++i) g_orbit_rate[i] += (target[i] - g_orbit_rate[i]) * ease;
    g_orbit_acc[0] += g_orbit_rate[0] * f.dt;
    g_orbit_acc[1] += g_orbit_rate[1] * f.dt;  // stick up = mouse up
    const int dx = static_cast<int>(g_orbit_acc[0]), dy = static_cast<int>(g_orbit_acc[1]);
    g_orbit_acc[0] -= dx;
    g_orbit_acc[1] -= dy;
    inputsim::move_relative(dx, dy);
}

void zoom(const Frame& f, const vrinput::State& st, bool allowed) {
    const float y = axis(g_s.zoom_invert ? -st.zoom[1] : st.zoom[1], g_s.stick_deadzone);
    if (!allowed || y == 0.0f) {
        g_zoom_acc = 0.0;
        return;
    }
    g_zoom_acc += y * g_s.zoom_notches_per_second * f.dt;
    while (std::fabs(g_zoom_acc) >= 1.0) {
        const int notch = g_zoom_acc > 0 ? 1 : -1;
        inputsim::wheel(notch);  // stick up = wheel away = zoom in
        g_zoom_acc -= notch;
    }
}

// The point on the laser `distance` metres from the head (the hand is nearer the head than that): menus open there,
// centred on the laser, so they start with nothing chosen.
Vec3 on_laser(const Vec3& head, const Vec3& origin, const Vec3& dir, float distance) {
    const Vec3 rel = origin - head;
    const float b = dot(dir, rel), c = dot(rel, rel) - distance * distance;
    const float disc = b * b - c;
    const float t = disc >= 0.0f ? std::max(0.15f, -b + std::sqrt(disc)) : 0.4f;
    return origin + dir * t;
}

void open_menu(const Frame& f, const vrinput::State& st, int menu, int button, Aim aim, const Vec3& origin,
               const Vec3& dir) {
    const int id = menus::radial_id(menu);
    if (id < 0) return;
    close_menu();
    cursor_position(f, &g_anchor_x, &g_anchor_y);
    g_anchor_valid = aim == Aim::World;
    const Vec3 head = position(f.hmd);
    const Vec3 toward = st.pointer_valid ? on_laser(head, origin, dir, kMenuDistance) : head - axis_z(f.hmd);
    radial::open(id, head, toward, kMenuDistance);
    g_menu = menu;
    g_menu_button = button;
    g_menu_slice = -1;
    g_menu_opened_at = now_ms();
    if (g_s.haptics) vrinput::pulse(0.01f, 0.3f);
    HW2VR_LOG("controls: %s menu%s", menus::name(menu), g_anchor_valid ? " (orders go to the target)" : "");
}

// Closes the open menu and runs the slice the laser is on (the centre: nothing).
void finish_menu(const Frame& f) {
    if (g_menu < 0) return;
    const int menu = g_menu;
    const int item = radial::close();
    g_menu = g_menu_button = g_menu_slice = -1;
    if (item < 0) {
        HW2VR_LOG("controls: %s menu closed (nothing chosen)", menus::name(menu));
        return;
    }
    if (menus::run(menu, item, f.window, g_anchor_x, g_anchor_y, g_anchor_valid) && g_s.haptics)
        vrinput::pulse(0.015f, 0.6f);
}

struct ButtonContext {
    const Frame& f;
    const vrinput::State& st;
    bool allowed;            // the game is in the foreground
    const char* menu_block;  // why a command menu can't open now (nullptr: it can)
    Aim aim;
    Vec3 origin, dir;
    Requests* req;
};

// Runs a binding. tap: a short press of a button that has a hold action - sent at its release as a quick press and
// release; otherwise keys and the middle button stay down while the controller button is held.
void run_binding(const ButtonContext& c, int i, const keys::Binding& bind, bool tap) {
    switch (bind.kind) {
        case keys::Binding::Keys:
            if (!c.allowed) break;
            if (tap) macro::tap(bind);
            else inputsim::keys(bind, true), g_held[i] = true;
            break;
        case keys::Binding::MiddleClick:
            if (!c.allowed) break;
            if (tap) {
                int x, y;
                cursor_position(c.f, &x, &y);
                macro::click_with(keys::Binding{}, c.f.window, x, y, 2);
            } else {
                inputsim::mouse_button(2, true);
                g_held[i] = true;
            }
            break;
        case keys::Binding::Menu: {
            if (!c.allowed) break;
            // A neighbouring D-pad direction pressed with the first (the pad rocked into a diagonal): keep the first.
            if (g_menu >= 0 && g_menu_button >= 0 && g_menu_button != i && c.st.buttons[g_menu_button].down &&
                now_ms() - g_menu_opened_at < kChordMs) {
                HW2VR_LOG("controls: %s ignored - pressed with %s (a diagonal)", input::kButtons[i].name,
                          input::kButtons[g_menu_button].name);
                break;
            }
            if (c.menu_block) {
                HW2VR_LOG("controls: %s menu not opened - %s", menus::name(bind.menu), c.menu_block);
                break;
            }
            open_menu(c.f, c.st, bind.menu, i, c.aim, c.origin, c.dir);
            break;
        }
        case keys::Binding::Recentre: c.req->recentre = true; break;
        case keys::Binding::WorldScaleUp: ++c.req->world_scale_steps; break;
        case keys::Binding::WorldScaleDown: --c.req->world_scale_steps; break;
        case keys::Binding::None: break;
    }
}

void buttons(const ButtonContext& c) {
    const long long now = now_ms();
    for (int i = 0; i < input::kButtonCount; ++i) {
        const vrinput::ButtonState& b = c.st.buttons[i];
        const keys::Binding& bind = g_s.buttons[i];
        const keys::Binding& hold = g_s.holds[i];
        if (g_held[i] && (!b.down || !c.allowed)) release_binding(i);
        if (b.released && g_menu_button == i) finish_menu(c.f);  // let go over a slice: that one
        if (b.pressed) {
            char desc[32], hold_desc[32];
            keys::describe(bind, desc, sizeof desc);
            keys::describe(hold, hold_desc, sizeof hold_desc);
            HW2VR_LOG("controls: %s pressed -> %s%s%s", input::kButtons[i].name, desc,
                      hold.kind != keys::Binding::None ? " (held: " : "", hold.kind != keys::Binding::None ? hold_desc : "");
            g_down_seen[i] = true;
            g_pressed_at[i] = now;
            g_hold_fired[i] = false;
        }
        const bool seen = g_down_seen[i];
        if (!b.down) g_down_seen[i] = false;
        if (g_dragging && i == input::kRightStick) continue;  // the stick click curves the panel then
        if (hold.kind != keys::Binding::None) {
            // Held long enough: the hold action, once. Let go sooner: the button's own action, as a tap.
            if (seen && b.down && !g_hold_fired[i] && now - g_pressed_at[i] >= g_s.hold_ms) {
                g_hold_fired[i] = true;
                char desc[32];
                keys::describe(hold, desc, sizeof desc);
                HW2VR_LOG("controls: %s held -> %s", input::kButtons[i].name, desc);
                if (g_s.haptics) vrinput::pulse(0.02f, 0.5f);
                run_binding(c, i, hold, true);
            }
            if (seen && b.released && !g_hold_fired[i]) run_binding(c, i, bind, true);
            continue;
        }
        if (b.pressed) run_binding(c, i, bind, false);
    }
}

// The game's cursor image: on the panel or a window where the game drew it, or out in the world along the ray the
// game picks along when the laser aims into the world.
void place_cursor(const Frame& f, Aim aim, const Vec3& eye, Vec3* world_point) {
    const hud::Cursor& c = hud::last_cursor();
    if (!c.drawn || f.client_w <= 0 || f.client_h <= 0) return g_cursor.hide();
    const input::PanelGeometry& g = panel::geometry();
    float pose[16];
    const float panel_pixel = g.width / static_cast<float>(f.client_w);  // metres per window pixel on the panel
    if (aim == Aim::Window) {
        float metres_per_pixel = 0.0f;
        if (!screens::pose_at(static_cast<float>(c.x) / f.client_w, static_cast<float>(c.y) / f.client_h, f.client_w,
                              pose, &metres_per_pixel))
            return g_cursor.hide();
        g_cursor.set_width(hud::kCursorSize * metres_per_pixel);
    } else if (aim == Aim::World || aim == Aim::Tablet || aim == Aim::Button || aim == Aim::Menu || !panel::shown()) {
        if (!f.game_projection) return g_cursor.hide();
        const Vec3 view = input::unproject_pixel(f.game_projection, static_cast<float>(c.x), static_cast<float>(c.y),
                                                 f.client_w, f.client_h);
        const Vec3 dir = normalize(rotate(f.camera_pose, view));
        *world_point = position(f.camera_pose) + dir * g_cursor_depth;
        face_eye_pose(*world_point, eye, pose);
        // The size it has in the game's own view.
        const float angle = 2.0f * std::atan(1.0f / f.game_projection[0]) / static_cast<float>(f.client_w) * hud::kCursorSize;
        g_cursor.set_width(angle * length(*world_point - eye));
    } else {
        if (!panel::shown()) return g_cursor.hide();
        g.pose_at(static_cast<float>(c.x) / f.client_w, static_cast<float>(c.y) / f.client_h, 0.004f, pose);
        g_cursor.set_width(hud::kCursorSize * panel_pixel);
    }
    g_cursor.place(pose);
    g_cursor.show();
}

// Every 5 s: what the laser is doing (for reading a test session's log afterwards).
void log_status(const Frame& f, const vrinput::State& st, Aim aim, bool foreground) {
    static long long next = 0;
    const long long now = now_ms();
    if (now < next) return;
    next = now + 5000;
    static const char* const kAim[] = {"nothing", "panel", "world", "grab bar", "tablet", "close tab", "game window", "menu"};
    int x = 0, y = 0;
    cursor_position(f, &x, &y);
    HW2VR_LOG("controls: laser %s, on %s, beam %s, game %s, mouse %s, cursor (%d, %d)",
              st.pointer_valid ? "tracked" : "NOT tracked", kAim[static_cast<int>(aim)], g_beam.visible() ? "shown" : "hidden",
              foreground ? "in front" : "in the BACKGROUND (input paused)", inputsim::window_mode() ? "to the window" : "real cursor",
              x, y);
}

}  // namespace

void configure(const Settings& settings) { g_s = settings; }

void start(const wchar_t* manifest_path) {
    g_started = true;
    g_dragging = g_orbiting = false;
    g_left_down = g_sticky = false;
    g_right = RightButton{};
    g_menu = g_menu_button = g_menu_slice = -1;
    std::fill(std::begin(g_held), std::end(g_held), false);
    std::fill(std::begin(g_down_seen), std::end(g_down_seen), false);
    std::fill(std::begin(g_hold_fired), std::end(g_hold_fired), false);
    g_prev_aim = Aim::None;
    g_tip_until = -1;
    g_hint_text = nullptr;
    g_logged_window = false;
    g_cursor_depth = g_s.cursor_distance;
    vrinput::init(manifest_path);

    const sprites::Image beam = sprites::beam(), bar = sprites::grab_bar();
    g_beam_tex = gl::upload_bgra(g_beam_tex, beam.width, beam.height, beam.pixels.data());
    g_bar_tex = gl::upload_bgra(g_bar_tex, bar.width, bar.height, bar.pixels.data());
    g_beam.create("hw2vr.beam", "Laser", 2, false);
    g_bar.create("hw2vr.bar", "Move the screen", 2, false);
    // Above every panel: game windows 4, the tablet 5, the pop-up menu and command menus 6, a menu's highlight 7.
    g_cursor.create("hw2vr.cursor", "Cursor", 8, true);
    g_hint.create("hw2vr.hint", "HW2 VR help", 4, false);
    g_beam.set_texture(g_beam_tex);
    g_bar.set_texture(g_bar_tex);
    menus::register_menus();
    tablet::start();
    radial::start();
    screens::start();

    char line[320] = {}, desc[32], hold[32];
    for (int i = 0; i < input::kButtonCount; ++i) {
        keys::describe(g_s.buttons[i], desc, sizeof desc);
        keys::describe(g_s.holds[i], hold, sizeof hold);
        const size_t len = std::strlen(line);
        std::snprintf(line + len, sizeof line - len, "%s%s=%s%s%s", i ? ", " : "", input::kButtons[i].name, desc,
                      g_s.holds[i].kind != keys::Binding::None ? "/hold:" : "",
                      g_s.holds[i].kind != keys::Binding::None ? hold : "");
        if (i == input::kButtonCount / 2 - 1 || i == input::kButtonCount - 1) {
            HW2VR_LOG("controls: buttons %s", line);
            line[0] = '\0';
        }
    }
    HW2VR_LOG("controls: right bumper = left click, right trigger = right click (held and moved past %.1f deg: camera "
              "turn, %.0f counts/deg)", g_s.swivel_degrees, g_s.swivel_speed);
}

void stop() {
    if (!g_started) return;
    release_mouse();
    release_keys();
    inputsim::set_window_mode(nullptr, false);
    macro::clear();
    radial::stop();
    tablet::stop();
    screens::stop();
    vrinput::shutdown();
    for (vr::Overlay* o : {&g_beam, &g_cursor, &g_bar, &g_hint}) o->destroy();
    const gl::Api& a = gl::api();
    for (gl::GLuint* t : {&g_beam_tex, &g_bar_tex, &g_hint_tex})
        if (*t && a.DeleteTextures) a.DeleteTextures(1, t), *t = 0;
    g_started = false;
}

void before_swap() {
    if (!g_started) return;
    const hud::Cursor& c = hud::current_cursor();
    if (c.drawn) g_cursor.set_texture(c.texture);
}

Requests update(const Frame& f) {
    Requests req;
    if (!g_started || !f.window || f.client_w <= 0 || f.client_h <= 0) return req;
    const vrinput::State& st = vrinput::update();  // all idle without SteamVR Input: the panel cursor still works
    if (!g_logged_window) {
        // The cursor can't reach client pixels that are off the desktop (e.g. under a title bar pushed off-screen).
        POINT tl{0, 0}, br{f.client_w, f.client_h};
        ClientToScreen(f.window, &tl);
        ClientToScreen(f.window, &br);
        const RECT desk{GetSystemMetrics(SM_XVIRTUALSCREEN), GetSystemMetrics(SM_YVIRTUALSCREEN),
                        GetSystemMetrics(SM_XVIRTUALSCREEN) + GetSystemMetrics(SM_CXVIRTUALSCREEN),
                        GetSystemMetrics(SM_YVIRTUALSCREEN) + GetSystemMetrics(SM_CYVIRTUALSCREEN)};
        const bool fits = tl.x >= desk.left && tl.y >= desk.top && br.x <= desk.right && br.y <= desk.bottom;
        const bool window_mode = g_s.mouse_mode == 1 || (g_s.mouse_mode == 0 && !fits);
        inputsim::set_window_mode(f.window, window_mode);
        HW2VR_LOG("controls: game client area %dx%d at (%ld, %ld) on a desktop (%ld, %ld)-(%ld, %ld)%s", f.client_w,
                  f.client_h, tl.x, tl.y, desk.left, desk.top, desk.right, desk.bottom,
                  fits || inputsim::window_mode() ? "" : " - PART OF IT IS OFF-SCREEN: the laser can't reach UI there "
                                                          "([controls] mouse = window)");
        g_logged_window = true;
    }
    const Vec3 eye = position(f.hmd);
    bool foreground = inputsim::game_foreground();
    if (!foreground) {
        // The player pressing something means they want the game back (the SteamVR dashboard or another program
        // took the focus): bring it to the front.
        bool pressed = st.trigger.pressed || st.bumper.pressed;
        for (const vrinput::ButtonState& b : st.buttons) pressed |= b.pressed;
        if (pressed) {
            foreground = inputsim::bring_to_front(f.window);
            HW2VR_LOG("controls: controller pressed with the game in the background - %s",
                      foreground ? "brought the game back to the front" : "couldn't bring it to the front (click the game window)");
        }
    }
    if (!foreground) {
        if (!g_logged_foreground) HW2VR_LOG("controls: the game window is not in the foreground - input paused");
        g_logged_foreground = true;
        release_mouse();
        release_keys();
    } else {
        if (g_logged_foreground) HW2VR_LOG("controls: the game window is in the foreground again");
        g_logged_foreground = false;
    }

    macro::run();
    tablet::update(st, f.hmd, f.dt);
    screens::update(f.hmd);

    const bool showing = panel::shown();
    const bool movable = showing && panel::movable();  // grab bar and help only on a movable panel (the console)
    const input::PanelGeometry& g = panel::geometry();
    if (movable) {
        const input::PanelGeometry bar = bar_layout(g);
        float pose[16];
        bar.pose(pose);
        g_bar.set_width(bar.width);
        g_bar.place(pose);
        g_bar.show();
    }

    Aim aim = Aim::None;
    Vec3 origin, dir;
    float hit_uv[2] = {}, hit_distance = 0.0f, bar_distance = 0.0f, screen_uv[2] = {};
    if (st.pointer_valid) {
        origin = position(st.pointer_pose);
        dir = normalize(-axis_z(st.pointer_pose));
        input::PanelGeometry bar_area = bar_layout(g);
        bar_area.width += 2.0f * kBarMargin;
        bar_area.height += 2.0f * kBarMargin;
        float unused[2];
        const bool held = g_left_down || g_right.down;
        if (radial::is_open()) {
            hit_distance = radial::point(origin, dir);
            aim = Aim::Menu;
            if (radial::selected() != g_menu_slice) {  // a tick per slice
                g_menu_slice = radial::selected();
                if (g_menu_slice >= 0 && g_s.haptics) vrinput::pulse(0.004f, 0.15f);
            }
        } else if (held && g_prev_aim == Aim::World) {
            aim = Aim::World;  // a button held down on the world stays there (box select, camera turn)
        } else if (!held && tablet::hover(origin, dir, &hit_distance)) {
            aim = Aim::Tablet;
        } else if (!held && screens::close_hover(origin, dir, &hit_distance)) {
            aim = Aim::Button;
        } else if (screens::hit(origin, dir, &hit_distance, screen_uv)) {
            aim = Aim::Window;
        } else if (movable && !held && bar_area.intersect(origin, dir, unused, &bar_distance)) {
            aim = Aim::Bar;
        } else if (showing && g.intersect(origin, dir, hit_uv, &hit_distance)) {
            // The panel is mostly see-through: it only counts where the game's UI has something.
            const bool ui = panel::solid() || hud::ui_at(hit_uv[0], hit_uv[1]) != 0;
            aim = ui || held ? Aim::Panel : Aim::World;
        } else {
            aim = Aim::World;
        }
        if (aim != Aim::Tablet) tablet::clear_hover();
        if (aim != Aim::Button) screens::close_clear_hover();
    } else {
        tablet::clear_hover();
        screens::close_clear_hover();
    }

    // Grab bar: hold the bumper on it to drag the panel; letting go saves.
    if (!g_dragging && aim == Aim::Bar && st.bumper.pressed) start_drag(origin, dir, bar_distance);
    if (g_dragging && (!st.bumper.down || !st.pointer_valid)) {
        g_dragging = false;
        panel::save();
    }
    if (g_dragging) drag_panel(st, origin, dir, static_cast<float>(f.dt));
    g_bar.set_alpha(g_dragging || aim == Aim::Bar ? 1.0f : 0.35f);
    g_dragging ? g_bar.set_color(1.0f, 0.6f, 0.2f) : g_bar.set_color(1.0f, 1.0f, 1.0f);
    if (!foreground) g_beam.set_color(0.45f, 0.45f, 0.45f);  // input paused: the game isn't in the foreground
    else g_beam.set_color(1.0f, g_dragging ? 0.55f : 1.0f, g_dragging ? 0.15f : 1.0f);

    // Help next to the bar: while on it or dragging, and a tip for the first 20 s.
    if (movable && g_tip_until < 0) g_tip_until = now_ms() + kTipMs;
    if (aim == Aim::Bar || g_dragging) g_tip_until = 0;  // found it
    set_hint(!movable ? nullptr : (aim == Aim::Bar || g_dragging) ? kBarHelp : now_ms() < g_tip_until ? kTip : nullptr);
    if (movable) place_hint(g);
    if (!movable) g_bar.hide();

    // Into the world: the laser ray in game view space (game units) goes to the depth probe, and the newest result
    // (a frame or two old) says where along it the laser touches the scene. Touching: aim at that point, the cursor
    // sits there. Not touching: aim along the laser's direction, the cursor keeps the last touch's depth.
    Vec3 world_target = {0, 0, -1};  // game view space: a point, or a direction for "nothing touched"
    if (aim == Aim::World && f.game_projection) {
        float inverse[16];
        mat4::rigid_inverse(f.camera_pose, inverse);
        const Vec3 o = transform_point(inverse, origin) * f.world_scale, d = rotate(inverse, dir);
        depth::set_ray(o, d);
        float t = 0.0f;
        world_target = d;
        if (depth::latest(&t) == depth::Result::Hit) {
            const Vec3 hit = o + d * t;
            if (hit.z < 0.0f) {
                world_target = hit;
                g_cursor_depth = length(hit) / f.world_scale;
            }
        }
    } else if (aim != Aim::Menu) {
        depth::clear_ray();
    }

    // The cursor follows the laser unless the panel is being moved, the camera turns (that drag owns the mouse), an
    // order is going out (a menu open, or its clicks still being sent) or the laser is on the tablet.
    const bool busy = g_dragging;
    const bool reset = aim != g_prev_aim;
    const bool cursor_free = foreground && !busy && !g_orbiting && !g_right.down && !macro::busy() && aim != Aim::Menu &&
                             aim != Aim::Tablet && aim != Aim::Button;
    if (cursor_free) {
        if (aim == Aim::Panel) {
            aim_cursor(f, hit_uv[0] * f.client_w, hit_uv[1] * f.client_h, f.dt, reset);
        } else if (aim == Aim::Window) {
            aim_cursor(f, screen_uv[0] * f.client_w, screen_uv[1] * f.client_h, f.dt, reset);
        } else if (aim == Aim::World && f.game_projection) {
            float px, py;
            if (input::project_direction(f.game_projection, world_target, f.client_w, f.client_h, &px, &py)) {
                input::clamp_inside(&px, &py, f.client_w, f.client_h, kEdgeMargin);
                aim_cursor(f, px, py, f.dt, reset);
            }
        }
    }
    g_prev_aim = aim;
    log_status(f, st, aim, foreground);

    // Our own panels: the bumper or the trigger presses a tablet button or picks a menu's slice.
    const bool ui_press = foreground && (st.bumper.pressed || st.trigger.pressed);
    if (aim == Aim::Tablet && ui_press && tablet::press() && g_s.haptics) vrinput::pulse(0.012f, 0.5f);
    if (aim == Aim::Button && ui_press && screens::close_press() && g_s.haptics) vrinput::pulse(0.012f, 0.5f);
    if (st.bumper.released || st.trigger.released) tablet::release();
    if (aim == Aim::Menu && ui_press) finish_menu(f);

    // The game: bumper = left button, trigger = right button.
    const bool game_aim = aim == Aim::Panel || aim == Aim::World || aim == Aim::Window;
    const bool can_click = foreground && !busy && !g_orbiting && !macro::busy() && game_aim && inputsim::cursor_over(f.window);
    left_button(st.bumper, can_click && !g_right.down, dir);
    right_button(f, st.trigger, can_click && !g_left_down, aim, st.pointer_valid, origin, dir);

    // A held left button starts dragging once the laser has moved far enough from where it went down.
    if (g_sticky && st.pointer_valid && now_ms() - g_press_at >= g_s.click_ms &&
        dot(dir, g_press_dir) < std::cos(g_s.drag_degrees * kPi / 180.0f)) {
        g_sticky = false;
        HW2VR_LOG("controls: bumper held and moved - dragging (box select)");
    }

    const bool free_hands = foreground && !busy && !g_left_down && !radial::is_open();
    orbit(f, st, free_hands && !g_right.down);
    zoom(f, st, free_hands);  // also while the trigger turns the camera
    const char* menu_block = busy             ? "the panel is being moved"
                             : g_left_down      ? "the left mouse button (bumper) is held"
                             : g_right.down     ? "the right mouse button (trigger) is held"
                             : g_orbiting       ? "the camera is orbiting (left stick)"
                             : macro::busy()    ? "keys are still going out"
                                                : nullptr;
    buttons(ButtonContext{f, st, foreground, menu_block, aim, origin, dir, &req});

    // Visuals: the beam ends where the laser acts.
    Vec3 world_point = origin + dir * g_s.cursor_distance;
    place_cursor(f, aim, eye, &world_point);
    if (!st.pointer_valid) {
        g_beam.hide();
    } else {
        const float beam_length = g_dragging                                                       ? g_grab_distance
                                  : aim == Aim::Bar                                                ? bar_distance
                                  : aim == Aim::Panel || aim == Aim::Tablet || aim == Aim::Window ||
                                            aim == Aim::Button                                     ? hit_distance
                                  : aim == Aim::Menu && hit_distance > 0.0f                       ? hit_distance
                                                                                                   : length(world_point - origin);
        show_beam(origin, dir, beam_length, eye);
    }
    return req;
}

}  // namespace hw2vr::controls
