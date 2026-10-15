#include "input/order_wheel.h"

#include <cmath>
#include <string>
#include <vector>

#include "common/log.h"
#include "input/panel_geometry.h"
#include "input/ui_canvas.h"
#include "proxy/gl_api.h"
#include "vr/overlay.h"

namespace hw2vr::wheel {
namespace {

constexpr int kSize = 512;
constexpr float kWidth = 0.34f;  // metres
const wchar_t* const kLabels[kOrderCount] = {L"Move", L"Attack", L"Guard", L"Dock", L"Stop", L"Repair", L"Harvest", L"Special"};

vr::Overlay g_overlay;
gl::GLuint g_tex[kOrderCount + 2] = {};  // highlight: none, each slice, the centre
bool g_open = false;
int g_highlight = -1;  // slice, kOrderCount = centre, -1 = none
input::PanelGeometry g_geometry;

int texture_index(int highlight) { return highlight < 0 ? 0 : highlight + 1; }

}  // namespace

void start() {
    g_overlay.create("hw2vr.wheel", "Order wheel", 6, false);
    g_overlay.set_width(kWidth);
    const std::vector<std::wstring> labels(kLabels, kLabels + kOrderCount);
    for (int h = -1; h <= kOrderCount; ++h) {
        const sprites::Image img = ui::render_wheel(kSize, labels, L"cancel", h);
        g_tex[texture_index(h)] = gl::upload_bgra(g_tex[texture_index(h)], img.width, img.height, img.pixels.data());
    }
    g_open = false;
}

void stop() {
    g_overlay.destroy();
    const gl::Api& a = gl::api();
    for (gl::GLuint& t : g_tex)
        if (t && a.DeleteTextures) a.DeleteTextures(1, &t), t = 0;
    g_open = false;
}

void open(const Vec3& head, const Vec3& toward, float distance) {
    Vec3 dir = normalize(toward - head);
    if (dot(dir, dir) < 0.5f) dir = {0, 0, -1};
    input::PanelGeometry g;
    g.centre = head + dir * distance;
    g.normal = -dir;
    Vec3 right = cross(Vec3{0, 1, 0}, g.normal);
    g.right = dot(right, right) < 1e-6f ? Vec3{1, 0, 0} : normalize(right);
    g.up = cross(g.normal, g.right);
    g.width = g.height = kWidth;
    g_geometry = g;
    float pose[16];
    g.pose(pose);
    g_highlight = -1;
    g_overlay.set_texture(g_tex[0]);
    g_overlay.place(pose);
    g_overlay.show();
    g_open = true;
}

bool is_open() { return g_open; }

float point(const Vec3& origin, const Vec3& dir) {
    if (!g_open) return 0.0f;
    // The laser against the wheel's plane (unbounded, so the slice still reads just outside the rim).
    const float facing = dot(dir, g_geometry.normal);
    if (facing > -1e-4f) return 0.0f;
    const float t = dot(g_geometry.centre - origin, g_geometry.normal) / facing;
    if (t <= 0.0f) return 0.0f;
    const Vec3 offset = origin + dir * t - g_geometry.centre;
    const float radius = 0.5f * kWidth;
    const float x = dot(offset, g_geometry.right) / radius, y = dot(offset, g_geometry.up) / radius;
    int slice = ui::wheel_slice(x, y, kOrderCount, ui::kWheelInner);
    if (slice == -2) slice = ui::wheel_slice(x * 0.999f / std::sqrt(x * x + y * y), y * 0.999f / std::sqrt(x * x + y * y),
                                             kOrderCount, ui::kWheelInner);  // past the rim: the slice in that direction
    const int highlight = slice == -1 ? kOrderCount : slice;
    if (highlight != g_highlight) {
        g_highlight = highlight;
        g_overlay.set_texture(g_tex[texture_index(highlight)]);
    }
    return t;
}

int close() {
    if (!g_open) return -1;
    g_open = false;
    g_overlay.hide();
    return g_highlight >= 0 && g_highlight < kOrderCount ? g_highlight : -1;
}

const wchar_t* label(int order) { return order >= 0 && order < kOrderCount ? kLabels[order] : L"none"; }

}  // namespace hw2vr::wheel
