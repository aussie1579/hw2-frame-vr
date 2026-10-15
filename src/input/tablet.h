#pragma once

#include "common/vec3.h"
#include "input/panel_geometry.h"
#include "input/sprites.h"
#include "vr/vr_input.h"

// The wrist tablet: turn the left palm towards your face and a small panel appears above it - four big buttons with
// the game's own taskbar icons (Sensors Manager, select all, focus on the mothership, main menu), the control groups
// 1-0 (hold the left bumper - Ctrl - to assign one) and a reminder of the D-pad's command menus
// (input/command_menus.h). Pointed at with the right-hand laser; the bumper (or the trigger) presses a button, which
// sends the game's own hotkey. Our own panel (input/ui_canvas.h); the game's resource readout is shown above it
// (hud/screen_windows.h).
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
// A button pressed / released while hovering. press() sends the button's hotkey (true if it hit a button).
bool press();
void release();

// Tests: the tablet's image with the hover-th button highlighted (-1 none).
sprites::Image test_render(int hover);

}  // namespace hw2vr::tablet
