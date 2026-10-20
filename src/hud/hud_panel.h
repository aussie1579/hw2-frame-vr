#pragma once

#include <string>

#include "input/panel_geometry.h"

// The VR panel that shows the game's screen-space UI (taskbar, menus, managers). Layouts:
//   - console (default, after headset test 7): a curved console below eye level, tilted towards the player like a
//     cockpit dashboard, on a dark backing so it reads as a solid surface; moved/resized/curved with the grab bar
//     above it, placement saved to hw2vr_console.ini. The world-space markers are in the world (hud_layer.h).
//   - aligned: flat, facing the game camera's direction and exactly spanning the game's field of view, at the
//     head's position;
//   - panel (shelved after headset test 6, "not readable"): a curved screen in front, grab bar under it, placement
//     saved to hw2vr_panel.ini.
namespace hw2vr::panel {

struct Placement {
    float distance = 1.8f;  // metres ahead of the recentre pose
    float side = 0.0f;      // metres to the right
    float height = -0.1f;   // metres up from eye level
    float width = 2.04f;    // metres (arc length when curved)
    float curvature = 0.18f;  // SteamVR overlay curvature: width / (2 pi radius); 0 = flat
};

enum class Layout { Console, Aligned, Panel, Modal };  // Modal: Panel, shown only while set_shown(true)

struct Settings {
    Layout layout = Layout::Console;
    float backing = 0.6f;            // console: opacity of the dark surface behind the UI
    float aligned_distance = 3.0f;   // metres
    float aligned_scale = 1.0f;      // 1 = spans the game's horizontal field of view
    Placement defaults;              // console/panel: used until the player moves it
    float pixels_per_degree = 20.0f; // panel texture resolution (headset pixels per degree); 0 = full size
    bool premultiplied = true;
    std::wstring state_path;         // hw2vr_console.ini / hw2vr_panel.ini
};

// Default placement for a curved panel `degrees` wide at `distance`, centred on the viewer.
Placement centred(float distance, float degrees, float height);

void configure(const Settings& settings);
bool start();  // VR started: create the overlay
bool movable();  // console or panel: placed by the player, grab bar shown
void set_shown(bool shown);
// Screen mode (menus, loading screens: no 3D world): the panel shows the game's whole screen and is solid for the laser.
void set_screen_mode(bool on);  // Modal layout: show the whole screen UI (a menu or dialog is up)
bool shown();               // placed and visible (the laser can use it)
bool solid();    // console: the whole surface is UI (the laser never points through it)
bool bar_above();  // the grab bar goes above it (console, below eye level) rather than under it
void stop();   // VR stopping: forget the overlay (vr::stop destroys it)

void recentre(const float anchor_pose[16]);  // seated pose (yaw + position) the panel is placed relative to
// Aligned layout, every frame: facing the game camera (camera_pose), at the head's position, sized to the game's
// horizontal field of view.
void follow(const float camera_pose[16], const float hmd_pose[16], float tan_half_fov_x);
bool placed();
const float* anchor();
const input::PanelGeometry& geometry();
Placement& placement();  // edit, then apply()
void apply();
void save();
void place_flat_or_centred();  // toggle: flat, or curved round the anchor

// Before the swap: picks the panel texture size for the current head position, filters the game's UI into it and
// hands it to SteamVR. aspect = window height / width.
void update_texture(const float hmd_pose[16], float aspect);

}  // namespace hw2vr::panel
