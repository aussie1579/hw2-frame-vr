#pragma once

#include <string>

namespace hw2vr {

// hw2vr.ini next to the DLL. Missing file or keys fall back to these defaults.
struct Config {
    bool verbose = false;

    // Phase 0 view-override spike (Ctrl+Shift+F9 cycles off -> yaw -> offset -> yaw+offset).
    float spike_yaw_degrees = 10.0f;  // camera yaw applied in view space
    float spike_offset_x = 150.0f;    // camera moves right by this many world units (eye-offset stand-in)
    int spike_start_mode = 0;         // 0 off, 1 yaw, 2 offset, 3 yaw+offset ("off|yaw|offset|yaw+offset" in the ini)
};

Config load_config(const std::wstring& ini_path);

}  // namespace hw2vr
