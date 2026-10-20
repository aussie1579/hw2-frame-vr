#pragma once

#include "common/vec3.h"

// The pieces of the game's own screen UI that the VR interface still shows, each cropped out at native resolution
// (so it's as sharp as the headset allows) rather than squeezing the whole 3440-pixel screen into view:
//   - status: the resource readout (top-right of the screen), above the wrist tablet while it's up;
//   - manager: the build / research / launch window (a column down the screen's right side) while it's open,
//     pinned in the room where the tablet was (or front-left), so the hand can drop; a Close tab on top of it
//     (the game's window has no close button of its own);
//   - popup: the game's right-click menu - UI that turns up next to the cursor after a right click in the world -
//     shown where the laser pointed, the click spot under the laser, until it closes;
//   - modal: when a menu or dialog covers the middle of the screen (front end, Esc menu, save/load), the whole
//     screen UI is shown on the front panel (hud/hud_panel.h) until it closes.
// Open/closed comes from how much of each screen region the UI covers (hud::ui_coverage / hud::ui_mask). Clicks on a
// window go to the game at the matching screen pixel.
namespace hw2vr::screens {

struct Settings {
    bool enabled = true;  // [vr] hud = tablet
    float status_region[4] = {0.75f, 0.0f, 1.0f, 0.07f};    // screen uv {u0, v0, u1, v1}, top-left origin
    float manager_region[4] = {0.855f, 0.06f, 1.0f, 0.86f};
    // The same on a square game screen (a 2880x2880 VR window): the game lays its UI out differently there.
    float status_region_square[4] = {0.75f, 0.0f, 1.0f, 0.035f};
    float manager_region_square[4] = {0.852f, 0.028f, 1.0f, 0.965f};
    float modal_region[4] = {0.3f, 0.3f, 0.7f, 0.7f};
    float manager_width = 0.30f;  // metres
};

void configure(const Settings& settings);
void start();
void stop();

// Before the swap, after the game's draws (and after hud::update_mask): open/closed, fresh crops.
void before_swap();
// After WaitGetPoses: place the visible windows (status strip on the tablet, manager where it was pinned).
void update(const float hmd_pose[16]);
bool modal_open();
// Screen mode (the whole screen is on the panel): the cut-out windows are hidden and not tracked.
void set_suspended(bool on);

// The right-click menu. watch: the right button went down on the world at screen uv (u, v), the laser along
// origin/dir - UI appearing near there from now on is the menu. cancel: the press became a camera turn.
// released: the button is up; if nothing has appeared within a moment, the watch ends.
void popup_watch(float u, float v, const Vec3& origin, const Vec3& dir);
void popup_cancel();
void popup_released();
bool popup_open();

// The manager window's Close tab: the laser on it highlights it (false if it misses or isn't shown); a press closes
// the window (menus::close_manager).
bool close_hover(const Vec3& origin, const Vec3& dir, float* distance);
void close_clear_hover();
bool close_press();

// The laser on a visible window: distance along the ray and the screen uv (top-left origin) it points at.
bool hit(const Vec3& origin, const Vec3& dir, float* distance, float screen_uv[2]);
// The pose of a point on a window at screen uv (for the cursor sprite) and the window's metres per screen pixel.
bool pose_at(float screen_u, float screen_v, int screen_w, float pose[16], float* metres_per_pixel);

}  // namespace hw2vr::screens
