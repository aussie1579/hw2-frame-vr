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
    bool vr_hud_movable = false;     // [vr] hud = aligned (spans the game's view) | panel (shelved curved panel)
    float vr_hud_distance = 3.0f;    // aligned HUD: metres
    float vr_hud_scale = 1.0f;       // aligned HUD: 1 = spans the game's field of view

    // [panel] (hud = panel): the curved screen, until moved with its grab bar (then hw2vr_panel.ini).
    float panel_distance = 1.8f;           // metres
    float panel_width_degrees = 65.0f;     // curved round the player
    float panel_height = -0.1f;            // metres above eye level
    float panel_pixels_per_degree = 20.0f; // UI image resolution on the panel; 0 = full size (may shimmer)

    // [controls]
    float stick_deadzone = 0.2f;
    float camera_speed = 1200.0f;  // left stick orbit: mouse counts per second at full stick
    bool camera_invert_y = false;
    float zoom_notches_per_second = 8.0f;
    bool zoom_invert = false;
    int click_freeze_ms = 120;
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
