#pragma once

#include <string>

#include "input/buttons.h"

namespace hw2vr {

// hw2vr.ini next to the DLL. Missing file or keys fall back to these defaults.
struct Config {
    bool verbose = false;
    bool debug_hud_capture = false;  // capture/convert the HUD and redirect the cursor even without VR (tests)
    float debug_replay_test_offset_x = 0.0f;  // replay every frame without VR with this view offset (tests)

    // VR (Tier 2). Ctrl+Shift+F8 toggles VR, F10 recentres, PgUp/PgDn change world scale.
    bool vr_autostart = false;      // start VR at the first frame instead of waiting for Ctrl+Shift+F8
    float vr_world_scale = 100.0f;  // game units per real metre (head movement and eye separation)
    bool vr_hud_premultiplied = true;
    bool vr_sync_stereo = true;      // [vr] stereo = sync (both eyes every frame) | afr (alternating frames)
    bool vr_fit_window = true;       // [vr] fit_window: client area fully on its monitor while VR runs
    int vr_hud_layout = 3;           // [vr] hud = console (0) | aligned (1) | panel (2, shelved) | tablet (3)
    bool vr_world_markers = true;    // [vr] world_markers: selection circles etc. at their ship's depth
    float vr_hud_distance = 3.0f;    // aligned HUD: metres
    float vr_hud_scale = 1.0f;       // aligned HUD: 1 = spans the game's field of view

    // [tablet] (hud = tablet): the wrist tablet and the game windows it brings up.
    float tablet_width = 0.32f;
    float tablet_lift = 0.11f;
    int tablet_palm_axis = 0;     // 0 +X ... 5 -Z of the left hand's grip pose
    bool tablet_always = false;
    float status_region[4] = {0.75f, 0.0f, 1.0f, 0.07f};
    float manager_region[4] = {0.855f, 0.06f, 1.0f, 0.86f};
    float modal_region[4] = {0.3f, 0.3f, 0.7f, 0.7f};
    float manager_width = 0.30f;
    float menu_distance = 2.0f;        // the menu/dialog panel
    float menu_width_degrees = 90.0f;

    // [console] (hud = console): until moved with its grab bar (then hw2vr_console.ini).
    float console_distance = 0.85f;       // metres
    float console_width_degrees = 80.0f;  // curved round the player
    float console_height = -0.42f;        // metres above eye level (negative = below)
    float console_backing = 0.6f;         // opacity of its dark surface

    // [panel] (hud = panel): the curved screen, until moved with its grab bar (then hw2vr_panel.ini).
    float panel_distance = 1.8f;           // metres
    float panel_width_degrees = 65.0f;     // curved round the player
    float panel_height = -0.1f;            // metres above eye level
    float panel_pixels_per_degree = 20.0f; // UI image resolution on the panel; 0 = full size (may shimmer)

    // [controls]
    float stick_deadzone = 0.2f;
    float camera_speed = 450.0f;   // left stick orbit: mouse counts per second at full stick
    float camera_curve = 2.0f;     // stick response exponent (1 = linear)
    bool camera_invert_y = false;
    float zoom_notches_per_second = 8.0f;
    bool zoom_invert = false;
    int click_freeze_ms = 80;
    float drag_degrees = 1.5f;     // a held click starts dragging once the laser moves this far
    float swivel_degrees = 2.0f;   // a held trigger turns the camera once the laser moves this far (else: right click)
    float swivel_speed = 12.0f;    // mouse counts per degree the laser turns, while turning the camera
    bool swivel_invert_x = false;
    bool swivel_invert_y = false;
    float trigger_press = 0.25f;   // trigger pull (0..1) that counts as a press
    float trigger_release = 0.15f; // ... and as let go again
    float filter_min_cutoff = 1.0f, filter_beta = 0.007f;
    bool haptics = true;
    bool show_beam = true;
    float cursor_distance = 10.0f;  // metres

    // [buttons]: key names / combos / VR actions (input/key_names.h), defaults in input/buttons.h.
    std::wstring buttons[input::kButtonCount];

    // Hotkeys, all Ctrl+Shift+<key> ([hotkeys] in the ini takes key names, see common/hotkeys.h).
    int key_vr_toggle = 0x77;  // F8
    int key_recentre = 0x79;   // F10
    int key_scale_up = 0x21;   // PageUp
    int key_scale_down = 0x22; // PageDown
    int key_spike = 0x78;      // F9

    // Phase 0 view-override spike, flat screen only (Ctrl+Shift+F9 cycles off -> yaw -> offset -> yaw+offset).
    float spike_yaw_degrees = 10.0f;  // camera yaw applied in view space
    float spike_offset_x = 150.0f;    // camera moves right by this many world units (eye-offset stand-in)
    int spike_start_mode = 0;         // 0 off, 1 yaw, 2 offset, 3 yaw+offset ("off|yaw|offset|yaw+offset" in the ini)
};

Config load_config(const std::wstring& ini_path);

}  // namespace hw2vr
