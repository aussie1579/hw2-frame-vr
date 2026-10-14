#include "common/config.h"

#include <windows.h>

#include <cwchar>
#include <iterator>

namespace hw2vr {
namespace {

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

    c.vr_autostart = GetPrivateProfileIntW(L"vr", L"autostart", c.vr_autostart ? 1 : 0, ini_path.c_str()) != 0;
    c.vr_world_scale = read_float(ini_path, L"vr", L"world_scale", c.vr_world_scale);
    c.vr_hud_distance = read_float(ini_path, L"vr", L"hud_distance", c.vr_hud_distance);
    c.vr_hud_scale = read_float(ini_path, L"vr", L"hud_scale", c.vr_hud_scale);
    c.vr_hud_premultiplied =
        GetPrivateProfileIntW(L"vr", L"hud_premultiplied", c.vr_hud_premultiplied ? 1 : 0, ini_path.c_str()) != 0;

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
