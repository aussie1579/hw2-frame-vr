#pragma once

#include <string>

#include "input/panel_geometry.h"

// The VR panel that shows the game's 2D UI. Two layouts:
//   - aligned (default): flat, facing the game camera's direction and exactly spanning the game's field of view, at
//     the head's position, so screen-placed markers sit over their ships and the UI is as sharp as it gets;
//   - movable (shelved after headset test 6, "not readable"): one curved screen fixed relative to the recentre pose,
//     moved/resized/curved with the grab bar under it (input/controls.cpp), placement saved to hw2vr_panel.ini.
namespace hw2vr::panel {

struct Placement {
    float distance = 1.8f;  // metres ahead of the recentre pose
    float side = 0.0f;      // metres to the right
    float height = -0.1f;   // metres up from eye level
    float width = 2.04f;    // metres (arc length when curved)
    float curvature = 0.18f;  // SteamVR overlay curvature: width / (2 pi radius); 0 = flat
};

struct Settings {
    bool movable = false;            // false = aligned layout
    float aligned_distance = 3.0f;   // metres
    float aligned_scale = 1.0f;      // 1 = spans the game's horizontal field of view
    Placement defaults;              // movable: used until the player moves the panel
    float pixels_per_degree = 20.0f; // panel texture resolution (headset pixels per degree); 0 = full size
    bool premultiplied = true;
    std::wstring state_path;         // hw2vr_panel.ini
};

// Default placement for a curved panel `degrees` wide at `distance`, centred on the viewer.
Placement centred(float distance, float degrees, float height);

void configure(const Settings& settings);
bool start();  // VR started: create the overlay
bool movable();
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
