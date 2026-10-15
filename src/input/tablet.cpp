#include "input/tablet.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "common/log.h"
#include "common/one_euro.h"
#include "input/key_names.h"
#include "input/macro.h"
#include "input/ui_canvas.h"
#include "proxy/gl_api.h"
#include "vr/overlay.h"

namespace hw2vr::tablet {
namespace {

constexpr int kCanvasW = 640, kCanvasH = 660;
constexpr float kShowFacing = 0.55f, kHideFacing = 0.30f;  // palm-to-head cosine, with hysteresis
constexpr float kMaxReach = 0.9f;                          // metres from the head

enum class Special { None, Group, Assign };

struct Def {
    const wchar_t* label;
    const wchar_t* keys;  // input/key_names.h syntax
    Special special = Special::None;
};
struct Row {
    const wchar_t* title;  // section label on the left ("" = continues the previous)
    std::vector<Def> buttons;
};

// HW2 keys (scripts/keybindings.lua): tactics F2-F4, stances F5-F7, strike-group formations Shift+F1-F9, leave
// strike group "/", build B, research R, launch L, Sensors Manager Space, objectives O, events V, menu F10,
// select all visible CapsLock, focus home Home, groups 1-0 (Ctrl+n assigns).
const std::vector<Row> kRows = {
    {L"TACTICS", {{L"Aggressive", L"F2"}, {L"Defensive", L"F3"}, {L"Passive", L"F4"}}},
    {L"STANCE", {{L"Damage", L"F5"}, {L"Evasive", L"F6"}, {L"Neutral", L"F7"}}},
    {L"FORMATION",
     {{L"Phalanx", L"Shift+F1"}, {L"Line", L"Shift+F2"}, {L"Screen", L"Shift+F3"}, {L"Delta", L"Shift+F4"},
      {L"Broad", L"Shift+F5"}}},
    {L"", {{L"X", L"Shift+F6"}, {L"Movers", L"Shift+F7"}, {L"Wall", L"Shift+F8"}, {L"Sphere", L"Shift+F9"}, {L"Leave", L"Slash"}}},
    {L"PRODUCE", {{L"Build", L"B"}, {L"Research", L"R"}, {L"Launch", L"L"}}},
    {L"SCREENS", {{L"Sensors", L"Space"}, {L"Objectives", L"O"}, {L"Events", L"V"}, {L"Menu", L"F10"}}},
    {L"GROUPS",
     {{L"1", L"1", Special::Group}, {L"2", L"2", Special::Group}, {L"3", L"3", Special::Group},
      {L"4", L"4", Special::Group}, {L"5", L"5", Special::Group}}},
    {L"",
     {{L"6", L"6", Special::Group}, {L"7", L"7", Special::Group}, {L"8", L"8", Special::Group},
      {L"9", L"9", Special::Group}, {L"0", L"0", Special::Group}}},
    {L"SELECT", {{L"All visible", L"CapsLock"}, {L"Home", L"Home"}, {L"Assign group", L"", Special::Assign}}},
};

Settings g_s;
bool g_started = false;
vr::Overlay g_overlay;
gl::GLuint g_tex = 0;
std::vector<ui::Item> g_items;
std::vector<Def> g_defs;          // per item (labels get an empty Def)
std::vector<keys::Binding> g_bindings;
bool g_dirty = true, g_visible = false;
int g_hover = -1, g_pressed = -1;
bool g_assign = false;
input::PanelGeometry g_geometry;
OneEuroFilter g_fx, g_fy, g_fz;

void build_layout() {
    g_items.clear();
    g_defs.clear();
    g_bindings.clear();
    const float top = 0.09f, bottom = 0.975f, gap = 0.012f;
    const float row_h = (bottom - top) / static_cast<float>(kRows.size());
    g_items.push_back({{0.04f, 0.015f, 0.96f, 0.075f}, L"FLEET COMMAND", ui::Kind::Title});
    g_defs.push_back({});
    g_bindings.push_back({});
    for (size_t r = 0; r < kRows.size(); ++r) {
        const float y0 = top + r * row_h + gap * 0.5f, y1 = top + (r + 1) * row_h - gap * 0.5f;
        if (kRows[r].title[0]) {
            g_items.push_back({{0.04f, y0, 0.21f, y1}, kRows[r].title, ui::Kind::Label});
            g_defs.push_back({});
            g_bindings.push_back({});
        }
        const float x0 = 0.22f, x1 = 0.96f, n = static_cast<float>(kRows[r].buttons.size());
        const float w = (x1 - x0) / n;
        for (size_t b = 0; b < kRows[r].buttons.size(); ++b) {
            const Def& d = kRows[r].buttons[b];
            g_items.push_back({{x0 + b * w + gap * 0.5f, y0, x0 + (b + 1) * w - gap * 0.5f, y1}, d.label, ui::Kind::Button});
            g_defs.push_back(d);
            keys::Binding binding;
            if (d.keys[0] && !keys::parse_binding(d.keys, &binding)) HW2VR_LOG("tablet: bad key '%ls'", d.keys);
            g_bindings.push_back(binding);
        }
    }
}

void redraw() {
    for (size_t i = 0; i < g_items.size(); ++i) {
        g_items[i].hover = static_cast<int>(i) == g_hover;
        g_items[i].pressed = static_cast<int>(i) == g_pressed;
        g_items[i].lit = g_defs[i].special == Special::Assign && g_assign;
    }
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

sprites::Image test_render(int hover, bool assign) {
    build_layout();
    g_hover = -1;
    for (size_t i = 0, buttons = 0; i < g_items.size(); ++i)
        if (g_items[i].kind == ui::Kind::Button && static_cast<int>(buttons++) == hover) g_hover = static_cast<int>(i);
    g_assign = assign;
    for (size_t i = 0; i < g_items.size(); ++i) {
        g_items[i].hover = static_cast<int>(i) == g_hover;
        g_items[i].lit = g_defs[i].special == Special::Assign && g_assign;
    }
    return ui::render_panel(kCanvasW, kCanvasH, g_items);
}

void start() {
    g_started = true;
    g_visible = false;
    g_hover = g_pressed = -1;
    g_assign = false;
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
    const Def& d = g_defs[g_hover];
    keys::Binding b = g_bindings[g_hover];
    if (d.special == Special::Assign) {
        g_assign = !g_assign;
    } else if (d.special == Special::Group && g_assign) {
        // Ctrl + the digit assigns the selection to that group.
        keys::Binding ctrl;
        keys::parse_binding((std::wstring(L"Ctrl+") + d.keys).c_str(), &ctrl);
        macro::tap(ctrl);
        g_assign = false;
    } else {
        macro::tap(b);
    }
    char desc[32];
    keys::describe(d.special == Special::Assign ? keys::Binding{} : b, desc, sizeof desc);
    HW2VR_LOG("tablet: %ls -> %s", d.label, d.special == Special::Assign ? (g_assign ? "assign on" : "assign off") : desc);
    return true;
}

void release() {
    if (g_pressed != -1) g_dirty = true;
    g_pressed = -1;
}

}  // namespace hw2vr::tablet
