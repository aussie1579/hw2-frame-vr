#include "common/config.h"

#include <windows.h>

#include <cstdio>
#include <cwchar>
#include <iterator>

#include "common/hotkeys.h"

namespace hw2vr {
namespace {

int read_key(const std::wstring& ini, const wchar_t* key, int fallback) {
    wchar_t buf[32];
    GetPrivateProfileStringW(L"hotkeys", key, L"", buf, static_cast<DWORD>(std::size(buf)), ini.c_str());
    const int vk = buf[0] ? hotkeys::parse_key(buf) : 0;
    return vk ? vk : fallback;
}

float read_float(const std::wstring& ini, const wchar_t* section, const wchar_t* key, float fallback) {
    wchar_t buf[64];
    GetPrivateProfileStringW(section, key, L"", buf, static_cast<DWORD>(std::size(buf)), ini.c_str());
    if (buf[0] == L'\0') return fallback;
    wchar_t* end = nullptr;
    float v = std::wcstof(buf, &end);
    return end == buf ? fallback : v;
}

}  // namespace

Config load_config(const std::wstring& ini_path) {
    Config c;
    c.verbose = GetPrivateProfileIntW(L"debug", L"verbose", c.verbose ? 1 : 0, ini_path.c_str()) != 0;
    c.debug_hud_capture = GetPrivateProfileIntW(L"debug", L"hud_capture", 0, ini_path.c_str()) != 0;
    c.debug_replay_test_offset_x = read_float(ini_path, L"debug", L"replay_test_offset_x", 0.0f);
    {
        wchar_t stereo[16];
        GetPrivateProfileStringW(L"vr", L"stereo", L"sync", stereo, static_cast<DWORD>(std::size(stereo)), ini_path.c_str());
        c.vr_sync_stereo = _wcsicmp(stereo, L"afr") != 0;
    }

    c.vr_autostart = GetPrivateProfileIntW(L"vr", L"autostart", c.vr_autostart ? 1 : 0, ini_path.c_str()) != 0;
    c.vr_world_scale = read_float(ini_path, L"vr", L"world_scale", c.vr_world_scale);
    c.vr_hud_premultiplied =
        GetPrivateProfileIntW(L"vr", L"hud_premultiplied", c.vr_hud_premultiplied ? 1 : 0, ini_path.c_str()) != 0;
    {
        wchar_t hud[16];
        GetPrivateProfileStringW(L"vr", L"hud", L"tablet", hud, static_cast<DWORD>(std::size(hud)), ini_path.c_str());
        c.vr_hud_layout = _wcsicmp(hud, L"aligned") == 0   ? 1
                          : _wcsicmp(hud, L"panel") == 0   ? 2
                          : _wcsicmp(hud, L"console") == 0 ? 0
                                                           : 3;
    }
    c.vr_world_markers = GetPrivateProfileIntW(L"vr", L"world_markers", 1, ini_path.c_str()) != 0;
    c.tablet_width = read_float(ini_path, L"tablet", L"width", c.tablet_width);
    c.tablet_lift = read_float(ini_path, L"tablet", L"lift", c.tablet_lift);
    {
        wchar_t axis[8];
        GetPrivateProfileStringW(L"tablet", L"palm_axis", L"+x", axis, static_cast<DWORD>(std::size(axis)), ini_path.c_str());
        constexpr const wchar_t* kAxes[] = {L"+x", L"-x", L"+y", L"-y", L"+z", L"-z"};
        for (int i = 0; i < 6; ++i)
            if (_wcsicmp(axis, kAxes[i]) == 0) c.tablet_palm_axis = i;
    }
    c.tablet_always = GetPrivateProfileIntW(L"tablet", L"always", 0, ini_path.c_str()) != 0;
    auto read_rect = [&](const wchar_t* key, float* r) {
        wchar_t buf[96];
        GetPrivateProfileStringW(L"tablet", key, L"", buf, static_cast<DWORD>(std::size(buf)), ini_path.c_str());
        float v[4];
        if (buf[0] && swscanf_s(buf, L"%f , %f , %f , %f", &v[0], &v[1], &v[2], &v[3]) == 4)
            for (int i = 0; i < 4; ++i) r[i] = v[i];
    };
    read_rect(L"status_region", c.status_region);
    read_rect(L"manager_region", c.manager_region);
    read_rect(L"menu_region", c.modal_region);
    c.manager_width = read_float(ini_path, L"tablet", L"manager_width", c.manager_width);
    c.menu_distance = read_float(ini_path, L"tablet", L"menu_distance", c.menu_distance);
    c.menu_width_degrees = read_float(ini_path, L"tablet", L"menu_width_degrees", c.menu_width_degrees);
    c.console_distance = read_float(ini_path, L"console", L"distance", c.console_distance);
    c.console_width_degrees = read_float(ini_path, L"console", L"width_degrees", c.console_width_degrees);
    c.console_height = read_float(ini_path, L"console", L"height", c.console_height);
    c.console_backing = read_float(ini_path, L"console", L"backing", c.console_backing);
    c.vr_fit_window = GetPrivateProfileIntW(L"vr", L"fit_window", c.vr_fit_window ? 1 : 0, ini_path.c_str()) != 0;
    c.vr_hud_distance = read_float(ini_path, L"vr", L"hud_distance", c.vr_hud_distance);
    c.vr_hud_scale = read_float(ini_path, L"vr", L"hud_scale", c.vr_hud_scale);

    c.panel_distance = read_float(ini_path, L"panel", L"distance", c.panel_distance);
    c.panel_width_degrees = read_float(ini_path, L"panel", L"width_degrees", c.panel_width_degrees);
    c.panel_height = read_float(ini_path, L"panel", L"height", c.panel_height);
    c.panel_pixels_per_degree = read_float(ini_path, L"panel", L"pixels_per_degree", c.panel_pixels_per_degree);

    auto flag = [&](const wchar_t* key, bool fallback) {
        return GetPrivateProfileIntW(L"controls", key, fallback ? 1 : 0, ini_path.c_str()) != 0;
    };
    c.stick_deadzone = read_float(ini_path, L"controls", L"stick_deadzone", c.stick_deadzone);
    c.camera_speed = read_float(ini_path, L"controls", L"camera_speed", c.camera_speed);
    c.camera_curve = read_float(ini_path, L"controls", L"camera_curve", c.camera_curve);
    c.camera_invert_y = flag(L"camera_invert_y", c.camera_invert_y);
    c.zoom_notches_per_second = read_float(ini_path, L"controls", L"zoom_notches_per_second", c.zoom_notches_per_second);
    c.zoom_invert = flag(L"zoom_invert", c.zoom_invert);
    c.click_freeze_ms = static_cast<int>(GetPrivateProfileIntW(L"controls", L"click_freeze_ms", c.click_freeze_ms, ini_path.c_str()));
    c.drag_degrees = read_float(ini_path, L"controls", L"drag_degrees", c.drag_degrees);
    c.swivel_degrees = read_float(ini_path, L"controls", L"swivel_degrees", c.swivel_degrees);
    c.swivel_speed = read_float(ini_path, L"controls", L"swivel_speed", c.swivel_speed);
    c.swivel_invert_x = flag(L"swivel_invert_x", c.swivel_invert_x);
    c.swivel_invert_y = flag(L"swivel_invert_y", c.swivel_invert_y);
    c.trigger_press = read_float(ini_path, L"controls", L"trigger_press", c.trigger_press);
    c.trigger_release = read_float(ini_path, L"controls", L"trigger_release", c.trigger_release);
    c.filter_min_cutoff = read_float(ini_path, L"controls", L"filter_min_cutoff", c.filter_min_cutoff);
    c.filter_beta = read_float(ini_path, L"controls", L"filter_beta", c.filter_beta);
    c.haptics = flag(L"haptics", c.haptics);
    c.show_beam = flag(L"show_beam", c.show_beam);
    c.cursor_distance = read_float(ini_path, L"controls", L"cursor_distance", c.cursor_distance);

    for (int i = 0; i < input::kButtonCount; ++i) {
        wchar_t buf[64];
        GetPrivateProfileStringW(L"buttons", input::kButtons[i].ini_key, input::kButtons[i].default_keys, buf,
                                 static_cast<DWORD>(std::size(buf)), ini_path.c_str());
        c.buttons[i] = buf;
        const std::wstring hold_key = std::wstring(input::kButtons[i].ini_key) + L"_hold";
        GetPrivateProfileStringW(L"buttons", hold_key.c_str(), input::kButtons[i].default_hold, buf,
                                 static_cast<DWORD>(std::size(buf)), ini_path.c_str());
        c.holds[i] = buf;
    }
    c.hold_ms = static_cast<int>(GetPrivateProfileIntW(L"buttons", L"hold_ms", c.hold_ms, ini_path.c_str()));

    c.key_vr_toggle = read_key(ini_path, L"vr_toggle", c.key_vr_toggle);
    c.key_recentre = read_key(ini_path, L"recentre", c.key_recentre);
    c.key_scale_up = read_key(ini_path, L"world_scale_up", c.key_scale_up);
    c.key_scale_down = read_key(ini_path, L"world_scale_down", c.key_scale_down);
    c.key_spike = read_key(ini_path, L"spike", c.key_spike);

    c.spike_yaw_degrees = read_float(ini_path, L"spike", L"yaw_degrees", c.spike_yaw_degrees);
    c.spike_offset_x = read_float(ini_path, L"spike", L"offset_x", c.spike_offset_x);

    wchar_t mode[32];
    GetPrivateProfileStringW(L"spike", L"start_mode", L"off", mode, static_cast<DWORD>(std::size(mode)), ini_path.c_str());
    constexpr const wchar_t* kModes[] = {L"off", L"yaw", L"offset", L"yaw+offset"};
    for (int i = 0; i < static_cast<int>(std::size(kModes)); ++i)
        if (_wcsicmp(mode, kModes[i]) == 0) c.spike_start_mode = i;
    return c;
}

}  // namespace hw2vr
