#include "input/tablet.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <iterator>
#include <string>
#include <vector>

#include "assets/game_assets.h"
#include "common/log.h"
#include "common/one_euro.h"
#include "input/key_names.h"
#include "input/macro.h"
#include "input/ui_canvas.h"
#include "proxy/gl_api.h"
#include "vr/overlay.h"

namespace hw2vr::tablet {
namespace {

constexpr int kCanvasW = 640, kCanvasH = 540;
constexpr float kShowFacing = 0.55f, kHideFacing = 0.30f;  // palm-to-head cosine, with hysteresis
constexpr float kMaxReach = 0.9f;                          // metres from the head

struct Def {
    const wchar_t* label;
    const wchar_t* keys;      // input/key_names.h syntax
    assets::IconRef icon;     // the game's own taskbar icon
    int hover_cell;           // its cell under the cursor
};

// HW2 keys (scripts/keybindings.lua): Sensors Manager Space, select all visible CapsLock, focus on the mothership
// Home, in-game menu F10; groups 1-0 select, Ctrl+1-0 assign. Icons and cells as on the game's taskbar
// (ui\newui\newtaskbar.lua).
const Def kBig[] = {
    {L"Sensors", L"Space", {"ui\\newui\\taskbar\\hudicons\\menu_ico_sensors4k.dds", 136, 1}, 3},
    {L"Select all", L"CapsLock", {"ui\\newui\\taskbar\\selectall.tga", 0, 0}, 0},
    {L"Mothership", L"Home", {"ui\\newui\\taskbar\\focuscapship.tga", 0, 0}, 0},
    {L"Menu", L"F10", {"ui\\newui\\taskbar\\hudicons\\menu_ico_main.dds", 128, 3}, 4},
};
const wchar_t* const kGroups[] = {L"1", L"2", L"3", L"4", L"5", L"6", L"7", L"8", L"9", L"0"};

Settings g_s;
bool g_started = false;
vr::Overlay g_overlay;
gl::GLuint g_tex = 0;
std::vector<ui::Item> g_items;
std::vector<keys::Binding> g_bindings;               // per item (titles and labels: none)
std::vector<std::array<sprites::Image, 2>> g_icons;  // per item: normal, hover (empty: no icon)
bool g_dirty = true, g_visible = false;
int g_hover = -1, g_pressed = -1;
input::PanelGeometry g_geometry;
OneEuroFilter g_fx, g_fy, g_fz;

void add(const ui::Rect& r, const wchar_t* text, ui::Kind kind, const wchar_t* keys = L"", const assets::IconRef& icon = {},
         int hover_cell = 0) {
    g_items.push_back({r, text, kind});
    keys::Binding b;
    if (keys[0] && !keys::parse_binding(keys, &b)) HW2VR_LOG("tablet: bad key '%ls'", keys);
    g_bindings.push_back(b);
    assets::IconRef hover = icon;
    hover.cell = hover_cell;
    g_icons.push_back({assets::load_icon(icon), assets::load_icon(hover)});
}

void build_layout() {
    g_items.clear();
    g_bindings.clear();
    g_icons.clear();
    constexpr float x0 = 0.04f, x1 = 0.96f, gap = 0.016f;
    add({x0, 0.015f, x1, 0.075f}, L"FLEET COMMAND", ui::Kind::Title);
    const float big_w = (x1 - x0) / static_cast<float>(std::size(kBig));
    for (size_t i = 0; i < std::size(kBig); ++i)
        add({x0 + static_cast<float>(i) * big_w + gap / 2, 0.09f, x0 + static_cast<float>(i + 1) * big_w - gap / 2, 0.44f},
            kBig[i].label, ui::Kind::Button, kBig[i].keys, kBig[i].icon, kBig[i].hover_cell);
    add({x0, 0.465f, x1, 0.525f}, L"GROUPS", ui::Kind::Title);
    const float group_w = (x1 - x0) / 5.0f;
    for (int i = 0; i < 10; ++i) {
        const int row = i / 5, col = i % 5;
        const float y0 = row ? 0.655f : 0.535f, y1 = row ? 0.765f : 0.645f;
        add({x0 + static_cast<float>(col) * group_w + gap / 2, y0, x0 + static_cast<float>(col + 1) * group_w - gap / 2, y1},
            kGroups[i], ui::Kind::Button, kGroups[i]);
    }
    add({x0, 0.79f, x1, 0.875f}, L"Hold the D-pad:  ▲ Orders   ◀ Tactics   ▶ Formations   ▼ Fleet",
        ui::Kind::Label);
    add({x0, 0.885f, x1, 0.97f}, L"Hold the left bumper and press a group to assign the selection", ui::Kind::Label);
}

void refresh_items() {
    for (size_t i = 0; i < g_items.size(); ++i) {
        const bool hover = static_cast<int>(i) == g_hover;
        g_items[i].hover = hover;
        g_items[i].pressed = static_cast<int>(i) == g_pressed;
        const sprites::Image& icon = g_icons[i][hover && g_icons[i][1].width > 0 ? 1 : 0];
        g_items[i].icon = icon.width > 0 ? &icon : nullptr;
    }
}

void redraw() {
    refresh_items();
    const sprites::Image img = ui::render_panel(kCanvasW, kCanvasH, g_items);
    g_tex = gl::upload_bgra(g_tex, img.width, img.height, img.pixels.data());
    g_overlay.set_texture(g_tex);
    g_dirty = false;
}

Vec3 palm_normal(const float pose[16]) {
    switch (g_s.palm_axis) {
        case 1: return -axis_x(pose);
        case 2: return axis_y(pose);
        case 3: return -axis_y(pose);
        case 4: return axis_z(pose);
        case 5: return -axis_z(pose);
        default: return axis_x(pose);
    }
}

}  // namespace

void configure(const Settings& settings) { g_s = settings; }

sprites::Image test_render(int hover) {
    build_layout();
    g_hover = g_pressed = -1;
    for (size_t i = 0, buttons = 0; i < g_items.size(); ++i)
        if (g_items[i].kind == ui::Kind::Button && static_cast<int>(buttons++) == hover) g_hover = static_cast<int>(i);
    refresh_items();
    const sprites::Image img = ui::render_panel(kCanvasW, kCanvasH, g_items);
    g_hover = -1;
    return img;
}

void start() {
    g_started = true;
    g_visible = false;
    g_hover = g_pressed = -1;
    build_layout();
    g_overlay.create("hw2vr.tablet", "Fleet command", 5, false);
    g_overlay.set_width(g_s.width);
    g_dirty = true;
    for (OneEuroFilter* f : {&g_fx, &g_fy, &g_fz}) {
        f->reset();
        f->min_cutoff = 1.5;
        f->beta = 1.0;  // metres: follow the hand quickly when it moves, hold steady when it doesn't
    }
}

void stop() {
    g_overlay.destroy();
    const gl::Api& a = gl::api();
    if (g_tex && a.DeleteTextures) a.DeleteTextures(1, &g_tex);
    g_tex = 0;
    g_started = g_visible = false;
}

void update(const vrinput::State& st, const float hmd[16], double dt) {
    if (!g_started || !g_s.enabled) return;
    const Vec3 head = position(hmd);
    bool show = false;
    Vec3 hand, normal;
    if (st.left_valid) {
        hand = position(st.left_pose);
        normal = normalize(palm_normal(st.left_pose));
        const Vec3 to_head = head - hand;
        const float facing = dot(normal, normalize(to_head));
        show = g_s.always || (length(to_head) < kMaxReach && facing > (g_visible ? kHideFacing : kShowFacing));
    }
    if (show != g_visible) {
        HW2VR_LOG("tablet: %s", show ? "shown (left palm towards you)" : "hidden");
        g_visible = show;
        if (!show) g_hover = g_pressed = -1, g_dirty = true;
        if (show) g_fx.reset(), g_fy.reset(), g_fz.reset();
    }
    if (!g_visible) return g_overlay.hide();

    // Above the palm, turned to the head with its edges level; smoothed so hand tremor doesn't shake it.
    const Vec3 raw = hand + normal * g_s.lift;
    const Vec3 centre{static_cast<float>(g_fx.filter(raw.x, dt)), static_cast<float>(g_fy.filter(raw.y, dt)),
                      static_cast<float>(g_fz.filter(raw.z, dt))};
    input::PanelGeometry g;
    g.centre = centre;
    g.normal = normalize(head - centre);
    Vec3 right = cross(Vec3{0, 1, 0}, g.normal);
    g.right = dot(right, right) < 1e-6f ? Vec3{1, 0, 0} : normalize(right);
    g.up = cross(g.normal, g.right);
    g.width = g_s.width;
    g.height = g_s.width * static_cast<float>(kCanvasH) / static_cast<float>(kCanvasW);
    g.curvature = 0.0f;
    g_geometry = g;
    if (g_dirty || !g_tex) redraw();
    float pose[16];
    g.pose(pose);
    g_overlay.place(pose);
    g_overlay.show();
}

bool visible() { return g_visible; }
const input::PanelGeometry& geometry() { return g_geometry; }

bool hover(const Vec3& origin, const Vec3& dir, float* distance) {
    float uv[2];
    if (!g_visible || !g_geometry.intersect(origin, dir, uv, distance)) {
        clear_hover();
        return false;
    }
    const int h = ui::hit(g_items, uv[0], uv[1]);
    if (h != g_hover) {
        g_hover = h;
        g_dirty = true;
    }
    return true;
}

void clear_hover() {
    if (g_hover != -1 || g_pressed != -1) g_dirty = true;
    g_hover = -1;
    g_pressed = -1;
}

bool press() {
    if (g_hover < 0) return false;
    g_pressed = g_hover;
    g_dirty = true;
    const keys::Binding& b = g_bindings[g_hover];
    macro::tap(b);
    char desc[32];
    keys::describe(b, desc, sizeof desc);
    HW2VR_LOG("tablet: %ls -> %s", g_items[g_hover].text.c_str(), desc);
    return true;
}

void release() {
    if (g_pressed != -1) g_dirty = true;
    g_pressed = -1;
}

}  // namespace hw2vr::tablet
