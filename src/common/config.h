#pragma once

#include <string>

namespace hw2vr {

// hw2vr.ini next to the DLL. Missing file or keys fall back to these defaults.
struct Config {
    bool verbose = false;

    // VR (Tier 2). Ctrl+Shift+F8 toggles VR, F10 recentres, PgUp/PgDn change world scale.
    bool vr_autostart = false;      // start VR at the first frame instead of waiting for Ctrl+Shift+F8
    float vr_world_scale = 100.0f;  // game units per real metre (head movement and eye separation)
    float vr_hud_distance = 3.0f;   // metres
    float vr_hud_scale = 1.0f;      // 1 = HUD spans the game's field of view (markers line up with ships)
    bool vr_hud_premultiplied = true;

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
