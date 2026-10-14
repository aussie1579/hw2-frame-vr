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
#include "input/input_sim.h"
#include "input/laser_math.h"
#include "input/sprites.h"
#include "proxy/gl_api.h"
#include "vr/overlay.h"
#include "vr/vr_input.h"

namespace hw2vr::controls {
namespace {

constexpr float kBeamThickness = 0.004f;  // metres
constexpr float kBarGap = 0.05f, kBarMargin = 0.02f;
constexpr float kEdgeMargin = 0.02f;      // world aim stays this fraction inside the window (no edge panning)
constexpr long long kTipMs = 20000;
// While dragging the panel by its bar, at full stick:
constexpr float kPushSpeed = 1.0f;   // metres per second along the laser
constexpr float kSizeSpeed = 0.6f;   // fraction of the width per second
constexpr float kCurveSpeed = 0.15f; // curvature per second

const wchar_t* const kTip =
    L"HOMEWORLD 2 VR\n"
    L"Laser = mouse:  trigger = left click,  bumper = right click\n"
    L"Left stick: orbit the camera     Right stick: zoom\n"
    L"To move the screen: point at the bar under it, hold the trigger";
const wchar_t* const kBarHelp =
    L"MOVE THE SCREEN\n"
    L"Hold the right trigger on this bar and move your hand\n"
    L"While holding:\n"
    L"Left stick up / down:  push away / pull closer\n"
    L"Left stick left / right:  smaller / bigger\n"
    L"Right stick left / right:  flatter / more curved\n"
    L"Right stick click:  flat, or curved round you";

enum class Aim { None, Panel, World, Bar };

Settings g_s;
bool g_started = false;

vr::Overlay g_beam, g_cursor, g_bar, g_hint;
gl::GLuint g_beam_tex = 0, g_bar_tex = 0, g_hint_tex = 0;
const wchar_t* g_hint_text = nullptr;
float g_hint_aspect = 0.0f;  // height / width of the hint image

OneEuroFilter g_fx, g_fy;
Aim g_prev_aim = Aim::None;
bool g_mouse_down[2] = {};       // buttons we hold down in the game
long long g_freeze_until = 0;
int g_cursor_x = INT_MIN, g_cursor_y = INT_MIN;  // last client position we put the cursor at

bool g_orbiting = false;
double g_orbit_acc[2] = {}, g_zoom_acc = 0.0;
bool g_held[input::kButtonCount] = {};

bool g_dragging = false;
float g_grab_distance = 0.0f;
Vec3 g_grab_offset;        // grabbed spot relative to the bar centre, in panel axes (right, up, normal)
long long g_tip_until = -1;
bool g_logged_foreground = false, g_logged_window = false;

long long now_ms() { return static_cast<long long>(GetTickCount64()); }

float axis(float v, float deadzone) {
    return std::fabs(v) < deadzone ? 0.0f : (v - std::copysign(deadzone, v)) / (1.0f - deadzone);
}

// The bar's rectangle under the panel, facing the same way.
input::PanelGeometry bar_layout(const input::PanelGeometry& p) {
    input::PanelGeometry b = p;
    b.curvature = 0.0f;
    b.width = std::clamp(p.width * 0.12f, 0.18f, 0.5f);
    b.height = b.width * 32.0f / 256.0f;
    b.centre = p.centre - p.up * (p.height / 2.0f + kBarGap + b.height / 2.0f);
    return b;
}

void release_mouse() {
    for (int b = 0; b < 2; ++b)
        if (g_mouse_down[b]) inputsim::mouse_button(b, false), g_mouse_down[b] = false;
    if (g_orbiting) inputsim::mouse_button(1, false), g_orbiting = false;
}

void release_keys() {
    for (int i = 0; i < input::kButtonCount; ++i)
        if (g_held[i]) inputsim::keys(g_s.buttons[i], false), g_held[i] = false;
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
    const Vec3 centre = bar.centre - p.up * (bar.height / 2.0f + 0.03f + height / 2.0f) + p.normal * 0.05f;
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

// Puts the game's cursor at a client pixel (filtered), unless frozen round a click.
void aim_cursor(const Frame& f, float px, float py, double dt, bool reset_filter) {
    if (reset_filter) g_fx.reset(), g_fy.reset();
    const double fx = g_fx.filter(px, dt), fy = g_fy.filter(py, dt);
    if (now_ms() < g_freeze_until) return;
    const int x = std::clamp(static_cast<int>(std::floor(fx)), 0, f.client_w - 1);
    const int y = std::clamp(static_cast<int>(std::floor(fy)), 0, f.client_h - 1);
    if (x == g_cursor_x && y == g_cursor_y) return;
    g_cursor_x = x;
    g_cursor_y = y;
    inputsim::set_cursor_client(f.window, x, y);
}

void click(const vrinput::ButtonState& b, int button, bool can_press) {
    if (b.pressed && can_press && !g_mouse_down[button]) {
        inputsim::mouse_button(button, true);
        g_mouse_down[button] = true;
        g_freeze_until = now_ms() + g_s.click_freeze_ms;
        if (g_s.haptics) vrinput::pulse(0.01f, 0.4f);
    }
    if (!b.down && g_mouse_down[button]) {
        inputsim::mouse_button(button, false);
        g_mouse_down[button] = false;
        g_freeze_until = now_ms() + g_s.click_freeze_ms / 2;
    }
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
    } else if (!want && g_orbiting) {
        inputsim::mouse_button(1, false);
        g_orbiting = false;
        return;
    }
    if (!g_orbiting) return;
    g_orbit_acc[0] += x * g_s.camera_speed * f.dt;
    g_orbit_acc[1] += (g_s.camera_invert_y ? y : -y) * g_s.camera_speed * f.dt;  // stick up = mouse up
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

void buttons(const vrinput::State& st, bool allowed, Requests* req) {
    for (int i = 0; i < input::kButtonCount; ++i) {
        const vrinput::ButtonState& b = st.buttons[i];
        const keys::Binding& bind = g_s.buttons[i];
        if (g_held[i] && (!b.down || !allowed)) {
            inputsim::keys(bind, false);
            g_held[i] = false;
        }
        if (!b.pressed || (g_dragging && i == input::kRightStick)) continue;  // stick click curves the panel then
        switch (bind.kind) {
            case keys::Binding::Keys:
                if (allowed) inputsim::keys(bind, true), g_held[i] = true;
                break;
            case keys::Binding::Recentre: req->recentre = true; break;
            case keys::Binding::WorldScaleUp: ++req->world_scale_steps; break;
            case keys::Binding::WorldScaleDown: --req->world_scale_steps; break;
            case keys::Binding::None: break;
        }
    }
}

// The game's cursor image: on the panel where the game drew it, or out in the world along the ray the game picks
// along when the laser aims into the world.
void place_cursor(const Frame& f, Aim aim, const Vec3& eye, Vec3* world_point) {
    const hud::Cursor& c = hud::last_cursor();
    if (!c.drawn || f.client_w <= 0 || f.client_h <= 0) return g_cursor.hide();
    const input::PanelGeometry& g = panel::geometry();
    float pose[16];
    const float panel_pixel = g.width / static_cast<float>(f.client_w);  // metres per window pixel on the panel
    if (aim == Aim::World && f.game_projection) {
        const Vec3 view = input::unproject_pixel(f.game_projection, static_cast<float>(c.x), static_cast<float>(c.y),
                                                 f.client_w, f.client_h);
        const Vec3 dir = normalize(rotate(f.camera_pose, view));
        *world_point = position(f.camera_pose) + dir * g_s.cursor_distance;
        face_eye_pose(*world_point, eye, pose);
        // Same angular size as on the panel.
        const float angle = hud::kCursorSize * panel_pixel / std::max(panel::placement().distance, 0.1f);
        g_cursor.set_width(angle * length(*world_point - eye));
    } else {
        if (!panel::placed()) return g_cursor.hide();
        g.pose_at(static_cast<float>(c.x) / f.client_w, static_cast<float>(c.y) / f.client_h, 0.004f, pose);
        g_cursor.set_width(hud::kCursorSize * panel_pixel);
    }
    g_cursor.place(pose);
    g_cursor.show();
}

}  // namespace

void configure(const Settings& settings) { g_s = settings; }

void start(const wchar_t* manifest_path) {
    g_started = true;
    g_dragging = g_orbiting = false;
    g_mouse_down[0] = g_mouse_down[1] = false;
    std::fill(std::begin(g_held), std::end(g_held), false);
    g_prev_aim = Aim::None;
    g_tip_until = -1;
    g_hint_text = nullptr;
    g_logged_window = false;
    vrinput::init(manifest_path);

    const sprites::Image beam = sprites::beam(), bar = sprites::grab_bar();
    g_beam_tex = gl::upload_bgra(g_beam_tex, beam.width, beam.height, beam.pixels.data());
    g_bar_tex = gl::upload_bgra(g_bar_tex, bar.width, bar.height, bar.pixels.data());
    g_beam.create("hw2vr.beam", "Laser", 2, false);
    g_bar.create("hw2vr.bar", "Move the screen", 2, false);
    g_cursor.create("hw2vr.cursor", "Cursor", 3, true);
    g_hint.create("hw2vr.hint", "HW2 VR help", 4, false);
    g_beam.set_texture(g_beam_tex);
    g_bar.set_texture(g_bar_tex);

    char line[256] = {}, desc[32];
    for (int i = 0; i < input::kButtonCount; ++i) {
        keys::describe(g_s.buttons[i], desc, sizeof desc);
        const size_t len = std::strlen(line);
        std::snprintf(line + len, sizeof line - len, "%s%s=%s", i ? ", " : "", input::kButtons[i].name, desc);
        if (i == input::kButtonCount / 2 - 1 || i == input::kButtonCount - 1) {
            HW2VR_LOG("controls: buttons %s", line);
            line[0] = '\0';
        }
    }
}

void stop() {
    if (!g_started) return;
    release_mouse();
    release_keys();
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
        HW2VR_LOG("controls: game client area %dx%d at (%ld, %ld) on a desktop (%ld, %ld)-(%ld, %ld)%s", f.client_w,
                  f.client_h, tl.x, tl.y, desk.left, desk.top, desk.right, desk.bottom,
                  fits ? "" : " - PART OF IT IS OFF-SCREEN: the laser can't reach UI there (use borderless/fullscreen)");
        g_logged_window = true;
    }
    const Vec3 eye = position(f.hmd);
    const bool foreground = inputsim::game_foreground();
    if (!foreground) {
        if (!g_logged_foreground) HW2VR_LOG("controls: the game window is not in the foreground - input paused");
        g_logged_foreground = true;
        release_mouse();
        release_keys();
    } else {
        g_logged_foreground = false;
    }

    const bool showing = panel::placed();
    const input::PanelGeometry& g = panel::geometry();
    if (showing) {
        const input::PanelGeometry bar = bar_layout(g);
        float pose[16];
        bar.pose(pose);
        g_bar.set_width(bar.width);
        g_bar.place(pose);
        g_bar.show();
    }

    Aim aim = Aim::None;
    Vec3 origin, dir;
    float hit_uv[2] = {}, hit_distance = 0.0f, bar_distance = 0.0f;
    if (st.pointer_valid) {
        origin = position(st.pointer_pose);
        dir = normalize(-axis_z(st.pointer_pose));
        input::PanelGeometry bar_area = bar_layout(g);
        bar_area.width += 2.0f * kBarMargin;
        bar_area.height += 2.0f * kBarMargin;
        float unused[2];
        const bool held = g_mouse_down[0] || g_mouse_down[1];
        if (showing && !held && bar_area.intersect(origin, dir, unused, &bar_distance)) {
            aim = Aim::Bar;
        } else if (showing && g.intersect(origin, dir, hit_uv, &hit_distance)) {
            // The panel is mostly see-through: it only counts where the game's UI has something. A held button
            // keeps the mode it was pressed in (a band-select drag in the world can cross UI).
            const bool ui = hud::ui_at(hit_uv[0], hit_uv[1]) != 0;
            aim = held && g_prev_aim == Aim::World ? Aim::World : ui || held ? Aim::Panel : Aim::World;
        } else {
            aim = Aim::World;
        }
    }

    // Grab bar: press the trigger on it to drag the panel; letting go saves.
    if (!g_dragging && aim == Aim::Bar && st.left_click.pressed) start_drag(origin, dir, bar_distance);
    if (g_dragging && (!st.left_click.down || !st.pointer_valid)) {
        g_dragging = false;
        panel::save();
    }
    if (g_dragging) drag_panel(st, origin, dir, static_cast<float>(f.dt));
    g_bar.set_alpha(g_dragging || aim == Aim::Bar ? 1.0f : 0.35f);
    g_dragging ? g_bar.set_color(1.0f, 0.6f, 0.2f) : g_bar.set_color(1.0f, 1.0f, 1.0f);
    g_beam.set_color(1.0f, g_dragging ? 0.55f : 1.0f, g_dragging ? 0.15f : 1.0f);

    // Help under the bar: while on it or dragging, and a tip for the first 20 s.
    if (showing && g_tip_until < 0) g_tip_until = now_ms() + kTipMs;
    if (aim == Aim::Bar || g_dragging) g_tip_until = 0;  // found it
    set_hint(!showing ? nullptr : (aim == Aim::Bar || g_dragging) ? kBarHelp : now_ms() < g_tip_until ? kTip : nullptr);
    if (showing) place_hint(g);

    // The cursor follows the laser unless the panel is being moved or the camera orbits (that drag owns the mouse).
    const bool busy = g_dragging;
    const bool reset = aim != g_prev_aim;
    if (foreground && !busy && !g_orbiting) {
        if (aim == Aim::Panel) {
            aim_cursor(f, hit_uv[0] * f.client_w, hit_uv[1] * f.client_h, f.dt, reset);
        } else if (aim == Aim::World && f.game_projection) {
            float inverse[16], px, py;
            mat4::rigid_inverse(f.camera_pose, inverse);
            if (input::project_direction(f.game_projection, rotate(inverse, dir), f.client_w, f.client_h, &px, &py)) {
                input::clamp_inside(&px, &py, f.client_w, f.client_h, kEdgeMargin);
                aim_cursor(f, px, py, f.dt, reset);
            }
        }
    }
    g_prev_aim = aim;

    const bool can_click = foreground && !busy && !g_orbiting && (aim == Aim::Panel || aim == Aim::World) &&
                           inputsim::cursor_over(f.window);
    click(st.left_click, 0, can_click && aim != Aim::Bar);
    click(st.right_click, 1, can_click);

    const bool sticks = foreground && !busy && !g_mouse_down[0] && !g_mouse_down[1];
    orbit(f, st, sticks);
    zoom(f, st, sticks);
    buttons(st, foreground, &req);

    // Visuals: the beam ends where the laser acts.
    Vec3 world_point = origin + dir * g_s.cursor_distance;
    place_cursor(f, aim, eye, &world_point);
    if (!st.pointer_valid) {
        g_beam.hide();
    } else {
        const float beam_length = g_dragging           ? g_grab_distance
                                  : aim == Aim::Bar    ? bar_distance
                                  : aim == Aim::Panel  ? hit_distance
                                                       : length(world_point - origin);
        show_beam(origin, dir, beam_length, eye);
    }
    return req;
}

}  // namespace hw2vr::controls
