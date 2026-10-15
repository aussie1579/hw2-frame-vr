#include "hud/screen_windows.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

#include "common/log.h"
#include "hud/hud_layer.h"
#include "input/command_menus.h"
#include "input/panel_geometry.h"
#include "input/tablet.h"
#include "input/ui_canvas.h"
#include "proxy/gl_api.h"
#include "vr/overlay.h"

namespace hw2vr::screens {
namespace {

constexpr float kOpenCoverage = 0.35f, kClosedCoverage = 0.15f;  // manager / modal, with hysteresis
constexpr int kCloseFrames = 8;                                   // closed this many frames in a row before hiding

// The right-click menu (ui\newui\rightclickmenu.lua): a column of 130 x 13 items (in 800 x 600 screen units) at the
// cursor, sub-menus cascading to its right, pushed back inside the screen near its edges.
constexpr float kPopupSearch[4] = {-0.2f, -0.6f, 0.45f, 0.6f};  // where it can be, around the click (screen uv)
constexpr int kPopupMinCells = 20;          // new UI cells (8 x 8 pixels each) that make a menu
constexpr int kPopupGiveUpFrames = 45;      // after the release: nothing turned up, no menu
constexpr float kPopupDistance = 0.7f;      // metres from the head
constexpr float kPopupScreenHeight = 0.86f; // its scale: as if the whole screen were this many metres tall
constexpr int kCloseW = 240, kCloseH = 72;  // the Close tab's image
constexpr float kCloseWidth = 0.11f;        // metres

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

struct Popup {
    enum class State { Off, Watching, Open } state = State::Off;
    float click[2] = {};                  // screen uv of the right click
    std::vector<unsigned char> baseline;  // the UI mask when the button went down
    int mask_w = 0, mask_h = 0;
    bool released = false;
    int frames = 0;                       // since the release
    Vec3 origin, dir;                     // the laser at the click
    bool placed = false;
    Vec3 anchor;                          // where the click spot sits in the room
    input::PanelGeometry frame;           // the window's orientation, fixed when it opens
};

Settings g_s;
Window g_status{"hw2vr.status", "Game status", 0, {}};
Window g_manager{"hw2vr.manager", "Game window", 1, {}};
Window g_popup{"hw2vr.popup", "Game menu", 2, {}};
Popup g_pop;
vr::Overlay g_close;
gl::GLuint g_close_tex[2] = {};  // normal, highlighted
input::PanelGeometry g_close_geometry;
bool g_close_shown = false, g_close_hovered = false;
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

void close_popup(const char* why) {
    if (g_pop.state == Popup::State::Open) HW2VR_LOG("screens: game pop-up menu gone (%s)", why);
    g_pop.state = Popup::State::Off;
    g_popup.open = false;
    g_popup.overlay.hide();
}

// UI that has appeared near the click since the watch began: how many mask cells, and their bounding box in
// screen uv {u0, v0, u1, v1}. -1 = the mask changed size.
int new_cells(float box[4]) {
    int w = 0, h = 0;
    const std::vector<unsigned char>& mask = hud::ui_mask(&w, &h);
    if (w != g_pop.mask_w || h != g_pop.mask_h || mask.size() != g_pop.baseline.size()) return -1;
    const int x0 = std::clamp(static_cast<int>((g_pop.click[0] + kPopupSearch[0]) * w), 0, w - 1);
    const int x1 = std::clamp(static_cast<int>((g_pop.click[0] + kPopupSearch[2]) * w), 0, w - 1);
    const int y0 = std::clamp(static_cast<int>((1.0f - (g_pop.click[1] + kPopupSearch[3])) * h), 0, h - 1);  // rows bottom-up
    const int y1 = std::clamp(static_cast<int>((1.0f - (g_pop.click[1] + kPopupSearch[1])) * h), 0, h - 1);
    int count = 0, bx0 = w, bx1 = -1, by0 = h, by1 = -1;
    for (int y = y0; y <= y1; ++y)
        for (int x = x0; x <= x1; ++x) {
            const size_t i = static_cast<size_t>(y) * w + x;
            if (!hud::mask_covered(mask[i]) || hud::mask_covered(g_pop.baseline[i])) continue;
            ++count;
            bx0 = std::min(bx0, x), bx1 = std::max(bx1, x), by0 = std::min(by0, y), by1 = std::max(by1, y);
        }
    if (count > 0) {  // a cell of margin all round
        box[0] = static_cast<float>(std::max(0, bx0 - 1)) / static_cast<float>(w);
        box[2] = static_cast<float>(std::min(w, bx1 + 2)) / static_cast<float>(w);
        box[1] = 1.0f - static_cast<float>(std::min(h, by1 + 2)) / static_cast<float>(h);
        box[3] = 1.0f - static_cast<float>(std::max(0, by0 - 1)) / static_cast<float>(h);
    }
    return count;
}

void popup_before_swap() {
    if (g_pop.state == Popup::State::Off) return;
    float box[4];
    const int n = new_cells(box);
    if (n < 0) return close_popup("the screen changed size");
    if (g_pop.state == Popup::State::Watching) {
        if (n < kPopupMinCells) {
            if (g_pop.released && ++g_pop.frames > kPopupGiveUpFrames) g_pop.state = Popup::State::Off;
            return;
        }
        g_pop.state = Popup::State::Open;
        g_pop.placed = false;
    } else if (n < kPopupMinCells) {
        if (++g_popup.closed_frames >= kCloseFrames) close_popup("closed");
        return;  // keeps the last image while the menu flickers
    }
    g_popup.closed_frames = 0;
    std::memcpy(g_popup.region, box, sizeof box);
    int cw = 0, ch = 0;
    if (const unsigned tex = hud::crop_texture(g_popup.slot, g_popup.region, &cw, &ch)) {
        if (!g_popup.open)
            HW2VR_LOG("screens: game pop-up menu (right click) %dx%d px at screen (%.3f, %.3f)", cw, ch, box[0], box[1]);
        g_popup.crop_w = cw;
        g_popup.crop_h = ch;
        g_popup.overlay.set_texture(tex);
        g_popup.open = true;
    }
}

// The pop-up where the laser pointed when it was clicked, kPopupDistance from the head and facing it, with the
// click spot on the laser (so the menu's first item is right there); it grows from that spot as it cascades.
void place_popup(const Vec3& head) {
    if (g_pop.state != Popup::State::Open || !g_popup.open || g_popup.crop_w <= 0) return g_popup.overlay.hide();
    if (!g_pop.placed) {
        const Vec3 rel = g_pop.origin - head;
        const float b = dot(g_pop.dir, rel), c = dot(rel, rel) - kPopupDistance * kPopupDistance;
        const float disc = b * b - c;
        const float t = disc >= 0.0f ? std::max(0.2f, -b + std::sqrt(disc)) : 0.5f;
        g_pop.anchor = g_pop.origin + g_pop.dir * t;
        face(g_pop.frame, g_pop.anchor, head, 1.0f, 1.0f);
        g_pop.placed = true;
    }
    const float mpp = kPopupScreenHeight / static_cast<float>(std::max(1, hud::overlay_height()));
    const float w = static_cast<float>(g_popup.crop_w) * mpp, h = static_cast<float>(g_popup.crop_h) * mpp;
    const float* r = g_popup.region;
    const float pu = std::clamp((g_pop.click[0] - r[0]) / std::max(r[2] - r[0], 1e-4f), 0.0f, 1.0f);
    const float pv = std::clamp((g_pop.click[1] - r[1]) / std::max(r[3] - r[1], 1e-4f), 0.0f, 1.0f);
    input::PanelGeometry& g = g_popup.geometry;
    g = g_pop.frame;
    g.width = w;
    g.height = h;
    g.centre = g_pop.anchor - g.right * ((pu - 0.5f) * w) - g.up * ((0.5f - pv) * h) + g.normal * 0.01f;
    show(g_popup);
}

}  // namespace

void configure(const Settings& settings) {
    g_s = settings;
    std::memcpy(g_status.region, g_s.status_region, sizeof g_status.region);
    std::memcpy(g_manager.region, g_s.manager_region, sizeof g_manager.region);
    g_manager.width = g_s.manager_width;
}

void start() {
    for (Window* w : {&g_status, &g_manager, &g_popup}) {
        // The game's UI: premultiplied colour, coverage alpha. The pop-up above the tablet.
        w->overlay.create(w->key, w->name, w == &g_popup ? 6 : 4, true);
        w->open = w->placed = false;
        w->closed_frames = 0;
    }
    g_pop = Popup{};
    g_close.create("hw2vr.manager.close", "Close game window", 5, false);
    for (int hover = 0; hover < 2; ++hover) {
        ui::Item item{{0.03f, 0.08f, 0.97f, 0.92f}, L"\u00D7  Close", ui::Kind::Button};
        item.hover = hover != 0;
        const sprites::Image img = ui::render_panel(kCloseW, kCloseH, {item});
        g_close_tex[hover] = gl::upload_bgra(g_close_tex[hover], img.width, img.height, img.pixels.data());
    }
    g_close.set_texture(g_close_tex[0]);
    g_close.set_width(kCloseWidth);
    g_close_shown = g_close_hovered = false;
    g_modal = false;
    g_modal_closed_frames = 0;
    g_have_last_tablet = false;
    g_started = true;
}

void stop() {
    for (Window* w : {&g_status, &g_manager, &g_popup}) w->overlay.destroy();
    g_close.destroy();
    const gl::Api& a = gl::api();
    for (gl::GLuint& t : g_close_tex)
        if (t && a.DeleteTextures) a.DeleteTextures(1, &t), t = 0;
    g_close_shown = g_close_hovered = false;
    g_pop = Popup{};
    g_popup.open = false;
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
    popup_before_swap();
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
        // The Close tab: above the window's top-right corner, facing the same way.
        const input::PanelGeometry& m = g_manager.geometry;
        g_close_geometry = m;
        g_close_geometry.width = kCloseWidth;
        g_close_geometry.height = kCloseWidth * static_cast<float>(kCloseH) / static_cast<float>(kCloseW);
        g_close_geometry.centre = m.centre + m.up * (m.height * 0.5f + 0.008f + g_close_geometry.height * 0.5f) +
                                  m.right * (m.width * 0.5f - kCloseWidth * 0.5f);
        float pose[16];
        g_close_geometry.pose(pose);
        g_close.place(pose);
        g_close.show();
        g_close_shown = true;
    } else {
        g_manager.overlay.hide();
        g_close.hide();
        g_close_shown = g_close_hovered = false;
    }

    place_popup(head);
}

bool modal_open() { return g_modal; }

bool close_hover(const Vec3& origin, const Vec3& dir, float* distance) {
    float uv[2];
    if (!g_close_shown || !g_close_geometry.intersect(origin, dir, uv, distance)) {
        close_clear_hover();
        return false;
    }
    if (!g_close_hovered) g_close.set_texture(g_close_tex[1]);
    g_close_hovered = true;
    return true;
}

void close_clear_hover() {
    if (g_close_hovered) g_close.set_texture(g_close_tex[0]);
    g_close_hovered = false;
}

bool close_press() {
    if (!g_close_shown || !g_close_hovered) return false;
    menus::close_manager();
    return true;
}

void popup_watch(float u, float v, const Vec3& origin, const Vec3& dir) {
    if (!g_started || !g_s.enabled) return;
    int w = 0, h = 0;
    const std::vector<unsigned char>& mask = hud::ui_mask(&w, &h);
    if (mask.empty()) return;
    if (g_pop.state == Popup::State::Open) close_popup("another right click");
    g_pop.baseline = mask;
    g_pop.mask_w = w;
    g_pop.mask_h = h;
    g_pop.click[0] = u;
    g_pop.click[1] = v;
    g_pop.origin = origin;
    g_pop.dir = dir;
    g_pop.released = false;
    g_pop.frames = 0;
    g_pop.state = Popup::State::Watching;
}

void popup_cancel() {
    if (g_pop.state == Popup::State::Watching) g_pop.state = Popup::State::Off;
}

void popup_released() {
    g_pop.released = true;
    g_pop.frames = 0;
}

bool popup_open() { return g_pop.state == Popup::State::Open && g_popup.open; }

bool hit(const Vec3& origin, const Vec3& dir, float* distance, float screen_uv[2]) {
    for (Window* w : {&g_popup, &g_manager, &g_status}) {
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
    for (Window* w : {&g_popup, &g_manager, &g_status}) {
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
