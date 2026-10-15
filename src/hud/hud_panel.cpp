#include "hud/hud_panel.h"

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <iterator>

#include "common/log.h"
#include "common/mat4.h"
#include "hud/hud_layer.h"
#include "vr/overlay.h"

namespace hw2vr::panel {
namespace {

constexpr float kPi = 3.14159265358979f;
constexpr float kMaxCurvature = 0.5f;  // half a cylinder

Settings g_settings;
Placement g_placement;
vr::Overlay g_overlay;
float g_anchor[16];
bool g_placed = false;
bool g_shown = false;  // Modal layout only
float g_aspect = 1440.0f / 3440.0f;
input::PanelGeometry g_geometry;

int g_tex_width = 0;                 // chosen panel texture width (0 = not yet)
long long g_next_size_check = 0;     // GetTickCount64
bool g_logged_size = false;

float read_float(const wchar_t* key, float fallback) {
    wchar_t buf[64];
    GetPrivateProfileStringW(L"panel", key, L"", buf, static_cast<DWORD>(std::size(buf)), g_settings.state_path.c_str());
    if (!buf[0]) return fallback;
    wchar_t* end = nullptr;
    const float v = std::wcstof(buf, &end);
    return end == buf || !std::isfinite(v) ? fallback : v;
}

void write_float(const wchar_t* key, float v) {
    wchar_t buf[64];
    std::swprintf(buf, std::size(buf), L"%.4f", v);
    WritePrivateProfileStringW(L"panel", key, buf, g_settings.state_path.c_str());
}

void clamp_placement(Placement& p) {
    p.distance = std::clamp(p.distance, 0.4f, 8.0f);
    p.side = std::clamp(p.side, -8.0f, 8.0f);
    p.height = std::clamp(p.height, -3.0f, 3.0f);
    p.width = std::clamp(p.width, 0.3f, 30.0f);
    p.curvature = std::clamp(p.curvature, 0.0f, kMaxCurvature);
}

void load() {
    g_placement = g_settings.defaults;
    if (GetFileAttributesW(g_settings.state_path.c_str()) != INVALID_FILE_ATTRIBUTES) {
        g_placement.distance = read_float(L"distance", g_placement.distance);
        g_placement.side = read_float(L"side", g_placement.side);
        g_placement.height = read_float(L"height", g_placement.height);
        g_placement.width = read_float(L"width", g_placement.width);
        g_placement.curvature = read_float(L"curvature", g_placement.curvature);
        HW2VR_LOG("panel: saved placement from %ls", g_settings.state_path.c_str());
    }
    clamp_placement(g_placement);
}

}  // namespace

Placement centred(float distance, float degrees, float height) {
    Placement p;
    p.distance = distance;
    p.height = height;
    p.width = distance * degrees * kPi / 180.0f;     // arc length round the viewer
    p.curvature = degrees / 360.0f;                  // width / (2 pi distance)
    clamp_placement(p);
    return p;
}

void configure(const Settings& settings) {
    g_settings = settings;
    g_placement = g_settings.defaults;
    if (movable()) load();
}

bool start() {
    g_placed = false;
    g_tex_width = 0;
    g_logged_size = false;
    if (movable()) load();  // pick up placement edits made while VR was off
    if (!g_overlay.create("hw2vr.panel", "Homeworld 2 UI", 1, g_settings.premultiplied)) return false;
    if (g_settings.layout == Layout::Modal)
        HW2VR_LOG("panel: menus and dialogs only, %.2f m ahead, %.2f m wide (%.0f degrees)", g_placement.distance,
                  g_placement.width, g_placement.width / g_placement.distance * 180.0f / kPi);
    else if (movable())
        HW2VR_LOG("panel: %s, %.2f m ahead, %.2f m side, %.2f m up, %.2f m wide (%.0f degrees), curvature %.3f",
                  solid() ? "console" : "movable panel", g_placement.distance, g_placement.side, g_placement.height,
                  g_placement.width, g_placement.width / g_placement.distance * 180.0f / kPi, g_placement.curvature);
    else
        HW2VR_LOG("panel: aligned with the game's view, %.1f m away, scale %.2f", g_settings.aligned_distance,
                  g_settings.aligned_scale);
    return true;
}

bool movable() { return g_settings.layout == Layout::Console || g_settings.layout == Layout::Panel; }
void set_shown(bool shown) { g_shown = shown; }
bool shown() { return g_placed && (g_settings.layout != Layout::Modal || g_shown); }
bool solid() { return g_settings.layout == Layout::Console; }
bool bar_above() { return g_placed && g_geometry.centre.y < position(g_anchor).y - 0.05f; }

void follow(const float camera_pose[16], const float hmd_pose[16], float tan_half_fov_x) {
    if (g_settings.layout != Layout::Aligned) return;
    // The panel is far closer than the (world-scaled) ships, so it sits at the head's position: leaning would
    // otherwise slide markers off their ships. Its orientation stays the game camera's.
    std::memcpy(g_anchor, camera_pose, sizeof g_anchor);
    g_anchor[12] = hmd_pose[12];
    g_anchor[13] = hmd_pose[13];
    g_anchor[14] = hmd_pose[14];
    g_placed = true;
    g_placement = Placement{g_settings.aligned_distance, 0.0f, 0.0f,
                            g_settings.aligned_scale * 2.0f * g_settings.aligned_distance * tan_half_fov_x, 0.0f};
    apply();
}

void stop() {
    g_overlay.destroy();
    g_placed = false;
}

void recentre(const float anchor_pose[16]) {
    std::memcpy(g_anchor, anchor_pose, sizeof g_anchor);
    g_placed = true;
    apply();
}

bool placed() { return g_placed; }
const float* anchor() { return g_anchor; }
const input::PanelGeometry& geometry() { return g_geometry; }
Placement& placement() { return g_placement; }

void apply() {
    clamp_placement(g_placement);
    if (!g_placed) return;
    g_geometry = input::PanelGeometry::in_front(g_anchor, g_placement.distance, g_placement.side, g_placement.height,
                                                g_placement.width, g_aspect, g_placement.curvature);
    float pose[16];
    g_geometry.pose(pose);
    g_overlay.set_width(g_placement.width);
    g_overlay.set_curvature(g_placement.curvature);
    g_overlay.place(pose);
}

void save() {
    if (!movable()) return;
    write_float(L"distance", g_placement.distance);
    write_float(L"side", g_placement.side);
    write_float(L"height", g_placement.height);
    write_float(L"width", g_placement.width);
    write_float(L"curvature", g_placement.curvature);
    HW2VR_LOG("panel: saved - %.2f m ahead, %.2f m side, %.2f m up, %.2f m wide, curvature %.3f", g_placement.distance,
              g_placement.side, g_placement.height, g_placement.width, g_placement.curvature);
}

void place_flat_or_centred() {
    // Curved so the anchor sits on its axis (radius = distance), or back to flat.
    g_placement.curvature = g_placement.curvature > 0.001f
                                ? 0.0f
                                : g_placement.width / (2.0f * kPi * std::max(g_placement.distance, 0.1f));
    apply();
}

void update_texture(const float hmd_pose[16], float aspect) {
    if (!g_overlay.valid() || !g_placed) return;
    if (!shown()) return g_overlay.hide();
    if (std::fabs(aspect - g_aspect) > 1e-4f) {
        g_aspect = aspect;
        apply();
    }
    const int src_w = hud::overlay_width(), src_h = hud::overlay_height();
    if (!hud::overlay_ready() || src_w <= 0 || src_h <= 0) return;  // nothing new: SteamVR keeps the last image

    // Texture width ~ the headset pixels the panel covers, re-checked twice a second; changes under 10% ignored.
    const long long now = static_cast<long long>(GetTickCount64());
    if (now >= g_next_size_check || g_tex_width == 0) {
        g_next_size_check = now + 500;
        const int across = g_geometry.pixels_across(position(hmd_pose), g_settings.pixels_per_degree);
        const int wanted = std::clamp(across > 0 ? across : src_w, 256, src_w);  // never more than the UI has
        if (g_tex_width == 0 || std::abs(wanted - g_tex_width) > g_tex_width / 10) {
            g_tex_width = wanted;
            g_logged_size = false;
        }
    }
    const int width = std::min(g_tex_width, src_w);
    const int height = std::max(1, static_cast<int>(std::lround(static_cast<double>(width) * src_h / src_w)));
    if (!g_logged_size) {
        HW2VR_LOG("panel: UI image %dx%d, filtered down from %dx%d (%.0f px/degree)", width, height, src_w, src_h,
                  g_settings.pixels_per_degree);
        g_logged_size = true;
    }
    const unsigned tex = hud::build_panel_texture(width, height, solid() ? g_settings.backing : 0.0f);
    if (!tex) return;
    g_overlay.set_texture(tex);
    g_overlay.show();
}

}  // namespace hw2vr::panel
