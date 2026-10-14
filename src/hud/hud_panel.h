#pragma once

#include <string>

#include "input/panel_geometry.h"

// The VR panel that shows the game's 2D UI: one curved screen in front of the player, fixed in the room relative
// to the recentre pose (it moves with recentring, not with the head). The player moves, resizes and curves it with
// the grab bar under it (input/controls.cpp); the placement is saved to hw2vr_panel.ini next to the DLL.
namespace hw2vr::panel {

struct Placement {
    float distance = 1.8f;  // metres ahead of the recentre pose
    float side = 0.0f;      // metres to the right
    float height = -0.1f;   // metres up from eye level
    float width = 2.04f;    // metres (arc length when curved)
    float curvature = 0.18f;  // SteamVR overlay curvature: width / (2 pi radius); 0 = flat
};

struct Settings {
    Placement defaults;              // used until the player moves the panel
    float pixels_per_degree = 20.0f; // panel texture resolution (headset pixels per degree); 0 = full size
    bool premultiplied = true;
    std::wstring state_path;         // hw2vr_panel.ini
};

// Default placement for a curved panel `degrees` wide at `distance`, centred on the viewer.
Placement centred(float distance, float degrees, float height);

void configure(const Settings& settings);
bool start();  // VR started: create the overlay
void stop();   // VR stopping: forget the overlay (vr::stop destroys it)

void recentre(const float anchor_pose[16]);  // seated pose (yaw + position) the panel is placed relative to
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
