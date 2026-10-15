#pragma once

#include <windows.h>

#include "input/buttons.h"
#include "input/key_names.h"

// The Tier 3 controls, once per frame on the render thread after WaitGetPoses:
//   - right-hand laser = the mouse. On the HUD panel's UI the cursor goes to the panel spot; elsewhere the laser
//     points into the world: where it touches the scene (stereo/depth_probe.h) the mouse goes to the pixel through
//     which the game's camera sees that point and the cursor is drawn right there, at the ship's depth.
//     Trigger (a light pull) = left click, bumper = right click; a press holds the cursor still until the laser
//     moves past drag_degrees, so clicks never become tiny drags (filtered aim, haptic ticks).
//   - left stick orbits the camera (emulated right-mouse drag), right stick up/down zooms (mouse wheel).
//   - every other button holds its hw2vr.ini [buttons] key(s) while held, or runs a VR action (recentre...).
//   - movable panel layout only (shelved): the grab bar under the panel moves, resizes and curves it.
// Visuals: the beam, the game's own cursor (on the panel, or out in the world at the laser's aim), the bar and help.
namespace hw2vr::controls {

struct Settings {
    keys::Binding buttons[input::kButtonCount];
    float stick_deadzone = 0.2f;
    float camera_speed = 1200.0f;  // mouse counts per second at full stick (right-drag orbit)
    bool camera_invert_y = false;
    float zoom_notches_per_second = 8.0f;
    bool zoom_invert = false;
    int click_freeze_ms = 80;       // the cursor stays put this long after a release
    float drag_degrees = 1.5f;      // a held click starts dragging once the laser moves this far
    double filter_min_cutoff = 1.0, filter_beta = 0.007;  // 1-euro filter on the cursor, in pixels
    bool haptics = true;
    bool show_beam = true;
    float cursor_distance = 10.0f;  // metres: cursor depth until the laser first touches something
};

struct Frame {
    HWND window = nullptr;
    int client_w = 0, client_h = 0;
    const float* hmd = nullptr;              // seated head pose
    const float* camera_pose = nullptr;      // seated pose of the game camera (the recentre pose)
    const float* game_projection = nullptr;  // the game's own camera projection; nullptr until seen
    float world_scale = 100.0f;              // game units per metre
    double dt = 0.0;
};

struct Requests {
    bool recentre = false;
    int world_scale_steps = 0;  // +1 per "world_scale_up" press, -1 per "world_scale_down"
};

void configure(const Settings& settings);
void start(const wchar_t* manifest_path);  // after vr::start() and panel::start()
void stop();                               // releases every held key/button
Requests update(const Frame& frame);
void before_swap();                         // hands this frame's cursor image to its overlay

}  // namespace hw2vr::controls
