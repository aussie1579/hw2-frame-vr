#pragma once

#include <windows.h>

#include "input/buttons.h"
#include "input/key_names.h"

// The Tier 3 controls, once per frame on the render thread after WaitGetPoses:
//   - right-hand laser = the mouse. On the HUD panel's UI or a game window the cursor goes to that spot; elsewhere
//     the laser points into the world: where it touches the scene (stereo/depth_probe.h) the mouse goes to the pixel
//     through which the game's camera sees that point and the cursor is drawn right there, at the ship's depth.
//     Bumper = the left mouse button (select, box-drag, click); a press holds the cursor still until the laser moves
//     past drag_degrees, so clicks never become tiny drags. Trigger (a light pull) = the right mouse button: a tap is
//     the game's right click (its pop-up menu is shown where the laser pointed - hud/screen_windows.h); held and
//     moved past swivel_degrees it turns the camera like the game's own right-drag. Filtered aim, haptic ticks.
//   - left stick orbits the camera (emulated right-mouse drag), right stick up/down zooms (mouse wheel).
//   - left D-pad, held: a command menu (input/command_menus.h) opens on the laser - let go over a slice, or click it.
//     A neighbouring direction pressed with it (a diagonal) is ignored.
//   - every other button holds its hw2vr.ini [buttons] key(s) while held, or runs a VR action (recentre, the middle
//     mouse button...). A button with a <button>_hold action runs that once held hold_ms, its own on a short press
//     (right stick: click = middle click - the game's focus on what the laser is on -, hold = recentre).
//   - a controller press while the game window is in the background (after the SteamVR dashboard) brings it back.
//   - our own panels (wrist tablet, menus): the bumper or the trigger presses.
//   - movable panel layouts only: the grab bar next to the panel moves, resizes and curves it (bumper).
// Visuals: the beam, the game's own cursor (on the panel, or out in the world at the laser's aim), the bar and help.
namespace hw2vr::controls {

struct Settings {
    keys::Binding buttons[input::kButtonCount];
    keys::Binding holds[input::kButtonCount];  // held hold_ms: this instead (None = the button's binding as usual)
    int hold_ms = 600;
    float stick_deadzone = 0.2f;
    float camera_speed = 450.0f;   // mouse counts per second at full stick (right-drag orbit)
    float camera_curve = 2.0f;     // stick response exponent: 1 = linear, 2 = gentle near the centre
    bool camera_invert_y = false;
    float zoom_notches_per_second = 8.0f;
    bool zoom_invert = false;
    int click_freeze_ms = 80;       // the cursor stays put this long after a release
    float drag_degrees = 1.5f;      // a held left click starts dragging once the laser moves this far
    float swivel_degrees = 2.0f;    // a held trigger turns the camera once the laser moves this far (else: right click)
    float swivel_speed = 12.0f;     // mouse counts per degree the laser turns while turning the camera
    bool swivel_invert_x = false, swivel_invert_y = false;
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
