#include "hud/screen_windows.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "common/log.h"
#include "hud/hud_layer.h"
#include "input/panel_geometry.h"
#include "input/tablet.h"
#include "vr/overlay.h"

namespace hw2vr::screens {
namespace {

constexpr float kOpenCoverage = 0.35f, kClosedCoverage = 0.15f;  // manager / modal, with hysteresis
constexpr int kCloseFrames = 8;                                   // closed this many frames in a row before hiding

struct Window {
    const char* key;
    const char* name;
    int slot;              // hud crop slot
    float region[4];       // screen uv
    vr::Overlay overlay;
    input::PanelGeometry geometry;
    bool open = false, placed = false;
    int closed_frames = 0;
    int crop_w = 0, crop_h = 0;
    float width = 0.3f;    // metres
};

Settings g_s;
Window g_status{"hw2vr.status", "Game status", 0, {}};
Window g_manager{"hw2vr.manager", "Game window", 1, {}};
bool g_modal = false;
int g_modal_closed_frames = 0;
bool g_started = false;
input::PanelGeometry g_last_tablet;
bool g_have_last_tablet = false;
unsigned g_frames_since_tablet = 1000;

// Open/closed from coverage, with hysteresis and a few frames' grace (the UI flickers while animating).
bool track(bool open, float coverage, int* closed_frames) {
    if (coverage < 0.0f) return open;  // no mask yet
    if (!open) return coverage >= kOpenCoverage;
    *closed_frames = coverage < kClosedCoverage ? *closed_frames + 1 : 0;
    return *closed_frames < kCloseFrames;
}

void face(input::PanelGeometry& g, const Vec3& centre, const Vec3& head, float width, float height) {
    g.centre = centre;
    g.normal = normalize(head - centre);
    Vec3 right = cross(Vec3{0, 1, 0}, g.normal);
    g.right = dot(right, right) < 1e-6f ? Vec3{1, 0, 0} : normalize(right);
    g.up = cross(g.normal, g.right);
    g.width = width;
    g.height = height;
    g.curvature = 0.0f;
}

void show(Window& w) {
    float pose[16];
    w.geometry.pose(pose);
    w.overlay.set_width(w.geometry.width);
    w.overlay.place(pose);
    w.overlay.show();
}

}  // namespace

void configure(const Settings& settings) {
    g_s = settings;
    std::memcpy(g_status.region, g_s.status_region, sizeof g_status.region);
    std::memcpy(g_manager.region, g_s.manager_region, sizeof g_manager.region);
    g_manager.width = g_s.manager_width;
}

void start() {
    for (Window* w : {&g_status, &g_manager}) {
        w->overlay.create(w->key, w->name, 4, true);  // the game's UI: premultiplied colour, coverage alpha
        w->open = w->placed = false;
        w->closed_frames = 0;
    }
    g_modal = false;
    g_modal_closed_frames = 0;
    g_have_last_tablet = false;
    g_started = true;
}

void stop() {
    for (Window* w : {&g_status, &g_manager}) w->overlay.destroy();
    g_started = false;
}

void before_swap() {
    if (!g_started || !g_s.enabled) return;
    const bool was_manager = g_manager.open, was_modal = g_modal;
    g_manager.open = track(g_manager.open, hud::ui_coverage(g_manager.region), &g_manager.closed_frames);
    g_modal = track(g_modal, hud::ui_coverage(g_s.modal_region), &g_modal_closed_frames);
    if (g_manager.open != was_manager) {
        HW2VR_LOG("screens: game window (build/research/launch) %s", g_manager.open ? "opened" : "closed");
        g_manager.placed = false;
    }
    if (g_modal != was_modal) HW2VR_LOG("screens: menu/dialog %s - front panel %s", g_modal ? "up" : "gone", g_modal ? "shown" : "hidden");

    // Fresh crops for what's visible (the status strip only with the tablet).
    g_status.open = tablet::visible();
    for (Window* w : {&g_status, &g_manager}) {
        if (!w->open) continue;
        int cw = 0, ch = 0;
        if (const unsigned tex = hud::crop_texture(w->slot, w->region, &cw, &ch)) {
            w->crop_w = cw;
            w->crop_h = ch;
            w->overlay.set_texture(tex);
        }
    }
}

void update(const float hmd[16]) {
    if (!g_started || !g_s.enabled) return;
    const Vec3 head = position(hmd);
    if (tablet::visible()) {
        g_last_tablet = tablet::geometry();
        g_have_last_tablet = true;
        g_frames_since_tablet = 0;
    } else {
        ++g_frames_since_tablet;
    }

    // Status strip: along the tablet's top edge, the tablet's width.
    if (g_status.open && g_status.crop_w > 0 && tablet::visible()) {
        const input::PanelGeometry& t = tablet::geometry();
        const float h = t.width * static_cast<float>(g_status.crop_h) / static_cast<float>(g_status.crop_w);
        g_status.geometry = t;
        g_status.geometry.width = t.width;
        g_status.geometry.height = h;
        g_status.geometry.centre = t.centre + t.up * (t.height * 0.5f + 0.01f + h * 0.5f) + t.normal * 0.002f;
        show(g_status);
    } else {
        g_status.overlay.hide();
    }

    // Manager window: pinned when it opens - beside the tablet if it was up in the last couple of seconds,
    // otherwise front-left of the head.
    if (g_manager.open && g_manager.crop_w > 0) {
        const float w = g_manager.width;
        const float h = w * static_cast<float>(g_manager.crop_h) / static_cast<float>(g_manager.crop_w);
        if (!g_manager.placed) {
            Vec3 centre;
            if (g_have_last_tablet && g_frames_since_tablet < 180) {
                // Beside the tablet, to its right, bottoms level.
                const input::PanelGeometry& t = g_last_tablet;
                centre = t.centre + t.right * (t.width * 0.5f + 0.03f + w * 0.5f) + t.up * (h * 0.5f - t.height * 0.5f);
            } else {
                Vec3 forward = -axis_z(hmd);
                forward.y = 0.0f;
                forward = dot(forward, forward) < 1e-6f ? Vec3{0, 0, -1} : normalize(forward);
                const Vec3 left = cross(Vec3{0, 1, 0}, forward);
                centre = head + forward * 0.7f + left * 0.25f + Vec3{0, -0.1f, 0};
            }
            face(g_manager.geometry, centre, head, w, h);
            g_manager.placed = true;
            HW2VR_LOG("screens: game window pinned %.2f m from the head, %.2f x %.2f m", length(centre - head), w, h);
        } else {
            g_manager.geometry.width = w;
            g_manager.geometry.height = h;
        }
        show(g_manager);
    } else {
        g_manager.overlay.hide();
    }
}

bool modal_open() { return g_modal; }

bool hit(const Vec3& origin, const Vec3& dir, float* distance, float screen_uv[2]) {
    for (Window* w : {&g_manager, &g_status}) {
        if (!w->open || !w->overlay.visible()) continue;
        float uv[2], d;
        if (!w->geometry.intersect(origin, dir, uv, &d)) continue;
        *distance = d;
        screen_uv[0] = w->region[0] + uv[0] * (w->region[2] - w->region[0]);
        screen_uv[1] = w->region[1] + uv[1] * (w->region[3] - w->region[1]);
        return true;
    }
    return false;
}

bool pose_at(float su, float sv, int screen_w, float pose[16], float* metres_per_pixel) {
    for (Window* w : {&g_manager, &g_status}) {
        if (!w->open || !w->overlay.visible()) continue;
        const float u = (su - w->region[0]) / (w->region[2] - w->region[0]);
        const float v = (sv - w->region[1]) / (w->region[3] - w->region[1]);
        if (u < 0.0f || u > 1.0f || v < 0.0f || v > 1.0f) continue;
        w->geometry.pose_at(u, v, 0.003f, pose);
        *metres_per_pixel = w->geometry.width / ((w->region[2] - w->region[0]) * static_cast<float>(screen_w));
        return true;
    }
    return false;
}

}  // namespace hw2vr::screens
