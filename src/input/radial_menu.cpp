#include "input/radial_menu.h"

#include <cmath>
#include <map>

#include "assets/game_assets.h"
#include "input/panel_geometry.h"
#include "input/ui_canvas.h"
#include "proxy/gl_api.h"
#include "vr/overlay.h"

namespace hw2vr::radial {
namespace {

constexpr int kSize = 512;
constexpr float kWidth = 0.36f;  // metres
constexpr float kPi = 3.14159265358979f;

struct Built {
    Menu menu;
    std::vector<sprites::Image> icons;  // loaded from the game (empty image = none)
    gl::GLuint texture = 0;
};
std::vector<Built> g_menus;
std::map<int, gl::GLuint> g_wedges;  // slice count -> highlight wedge texture
vr::Overlay g_base, g_highlight;
int g_open = -1, g_selected = -1;
input::PanelGeometry g_geometry;

std::vector<const sprites::Image*> icon_pointers(const Built& b) {
    std::vector<const sprites::Image*> out;
    for (const sprites::Image& i : b.icons) out.push_back(i.width > 0 ? &i : nullptr);
    return out;
}

std::vector<std::wstring> labels(const Menu& m) {
    std::vector<std::wstring> out;
    for (const Item& i : m.items) out.push_back(i.label);
    return out;
}

void load_icons(Built& b) {
    b.icons.clear();
    for (const Item& item : b.menu.items)
        b.icons.push_back(assets::load_icon(item.icon));
}

}  // namespace

int add(const Menu& menu) {
    g_menus.push_back(Built{menu, {}, 0});
    return static_cast<int>(g_menus.size()) - 1;
}

void start() {
    g_base.create("hw2vr.radial", "Command menu", 6, false);
    g_highlight.create("hw2vr.radial.highlight", "Command menu highlight", 7, false);
    g_base.set_width(kWidth);
    g_highlight.set_width(kWidth);
    for (Built& b : g_menus) {
        load_icons(b);
        const sprites::Image img = ui::render_wheel(kSize, labels(b.menu), b.menu.title, -1, icon_pointers(b));
        b.texture = gl::upload_bgra(b.texture, img.width, img.height, img.pixels.data());
        const int n = static_cast<int>(b.menu.items.size());
        if (!g_wedges.count(n)) {
            const sprites::Image wedge = ui::render_wedge(kSize, n);
            g_wedges[n] = gl::upload_bgra(0, wedge.width, wedge.height, wedge.pixels.data());
        }
    }
    g_open = g_selected = -1;
}

void stop() {
    g_base.destroy();
    g_highlight.destroy();
    const gl::Api& a = gl::api();
    for (Built& b : g_menus)
        if (b.texture && a.DeleteTextures) a.DeleteTextures(1, &b.texture), b.texture = 0;
    for (auto& [n, t] : g_wedges)
        if (t && a.DeleteTextures) a.DeleteTextures(1, &t);
    g_wedges.clear();
    g_open = -1;
}

void open(int menu, const Vec3& head, const Vec3& toward, float distance) {
    if (menu < 0 || menu >= static_cast<int>(g_menus.size()) || !g_menus[menu].texture) return;
    Vec3 dir = normalize(toward - head);
    if (dot(dir, dir) < 0.5f) dir = {0, 0, -1};
    input::PanelGeometry g;
    g.centre = head + dir * distance;
    g.normal = -dir;
    const Vec3 right = cross(Vec3{0, 1, 0}, g.normal);
    g.right = dot(right, right) < 1e-6f ? Vec3{1, 0, 0} : normalize(right);
    g.up = cross(g.normal, g.right);
    g.width = g.height = kWidth;
    g_geometry = g;
    float pose[16];
    g.pose(pose);
    g_base.set_texture(g_menus[menu].texture);
    g_base.place(pose);
    g_base.show();
    g_highlight.hide();
    g_open = menu;
    g_selected = -1;
}

bool is_open() { return g_open >= 0; }
int open_menu() { return g_open; }
int selected() { return g_open >= 0 ? g_selected : -1; }

float point(const Vec3& origin, const Vec3& dir) {
    if (g_open < 0) return 0.0f;
    const float facing = dot(dir, g_geometry.normal);
    if (facing > -1e-4f) return 0.0f;
    const float t = dot(g_geometry.centre - origin, g_geometry.normal) / facing;
    if (t <= 0.0f) return 0.0f;
    const Vec3 offset = origin + dir * t - g_geometry.centre;
    const float radius = 0.5f * kWidth;
    const float x = dot(offset, g_geometry.right) / radius, y = dot(offset, g_geometry.up) / radius;
    const int n = static_cast<int>(g_menus[g_open].menu.items.size());
    int slice = ui::wheel_slice(x, y, n, ui::kWheelInner);
    if (slice == -2) {  // past the rim: the slice in that direction
        const float r = std::sqrt(x * x + y * y);
        slice = ui::wheel_slice(x * 0.99f / r, y * 0.99f / r, n, ui::kWheelInner);
    }
    if (slice != g_selected) {
        g_selected = slice;
        if (slice < 0) {
            g_highlight.hide();
        } else {
            // The wedge is drawn at the top; turn it clockwise (as seen) to the slice.
            const float a = slice * 2.0f * kPi / static_cast<float>(n);
            const Vec3 up = g_geometry.up * std::cos(a) + g_geometry.right * std::sin(a);
            const Vec3 right = g_geometry.right * std::cos(a) - g_geometry.up * std::sin(a);
            float pose[16];
            pose_from_axes(right, up, g_geometry.normal, g_geometry.centre + g_geometry.normal * 0.002f, pose);
            g_highlight.set_texture(g_wedges[n]);
            g_highlight.place(pose);
            g_highlight.show();
        }
    }
    return t;
}

int close() {
    if (g_open < 0) return -1;
    g_open = -1;
    g_base.hide();
    g_highlight.hide();
    return g_selected;
}

bool test_render(int menu, int highlight, std::vector<unsigned>* pixels, int* size) {
    if (menu < 0 || menu >= static_cast<int>(g_menus.size())) return false;
    Built& b = g_menus[menu];
    if (b.icons.size() != b.menu.items.size()) load_icons(b);
    const sprites::Image img = ui::render_wheel(kSize, labels(b.menu), b.menu.title, highlight, icon_pointers(b));
    *pixels = img.pixels;
    *size = img.width;
    return true;
}

}  // namespace hw2vr::radial
