#pragma once

#include "common/vec3.h"
#include "input/panel_geometry.h"
#include "input/sprites.h"
#include "vr/vr_input.h"

// The wrist tablet: turn the left palm towards your face and a panel of big buttons appears above it - tactics,
// stances, strike-group formations, production and view screens, control groups. Pointed at with the right-hand
// laser; the trigger presses a button, which sends the game's own hotkey. Our own panel (input/ui_canvas.h),
// not a copy of the game's UI; the game's resource readout is shown above it (hud/screen_windows.h).
namespace hw2vr::tablet {

struct Settings {
    bool enabled = true;  // [vr] hud = tablet
    float width = 0.32f;  // metres
    float lift = 0.11f;   // metres from the palm to the tablet's centre
    int palm_axis = 0;    // the left hand's palm normal in its pose: 0 +X (OpenXR grip), 1 -X, 2 +Y, 3 -Y, 4 +Z, 5 -Z
    bool always = false;  // show it whatever the palm does (for trying it out)
};

void configure(const Settings& settings);
void start();
void stop();

// After WaitGetPoses: shown while the left palm faces the head, placed above it and turned to the head.
void update(const vrinput::State& st, const float hmd_pose[16], double dt);
bool visible();
const input::PanelGeometry& geometry();

// The laser on the tablet: highlights the button under it; false (and no highlight) if it misses the tablet.
bool hover(const Vec3& origin, const Vec3& dir, float* distance);
void clear_hover();
// The trigger pressed / released while hovering. press() runs the button's action (true if it hit a button).
bool press();
void release();

// Tests: the tablet's image with the hover-th button highlighted (-1 none) and Assign lit or not.
sprites::Image test_render(int hover, bool assign);

}  // namespace hw2vr::tablet
