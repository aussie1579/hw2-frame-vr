#include "stereo/view_override.h"

#include <windows.h>

#include <cmath>
#include <cstring>

#include "common/log.h"

namespace hw2vr::stereo {
namespace {

enum class Mode { Off, Yaw, Offset, YawAndOffset };
constexpr const char* kModeNames[] = {"off", "yaw", "offset", "yaw+offset"};

Mode g_mode = Mode::Off;
float g_yaw_radians = 0.0f;
float g_offset_x = 0.0f;
float g_eye_full[16];      // E = Ry(yaw) * T(-offset_x, 0, 0)
float g_eye_rotation[16];  // E without translation, for skybox views
bool g_hotkey_down = false;
unsigned g_frames_in_mode = 0;
unsigned g_counts[4] = {};  // overrides per CameraUniform in the current frame

void multiply(const float a[16], const float b[16], float out[16]) {
    for (int c = 0; c < 4; ++c)
        for (int r = 0; r < 4; ++r) {
            float s = 0.0f;
            for (int k = 0; k < 4; ++k) s += a[k * 4 + r] * b[c * 4 + k];
            out[c * 4 + r] = s;
        }
}

void rebuild_eye_transform() {
    const bool yaw = g_mode == Mode::Yaw || g_mode == Mode::YawAndOffset;
    const bool offset = g_mode == Mode::Offset || g_mode == Mode::YawAndOffset;
    const float a = yaw ? g_yaw_radians : 0.0f;
    const float x = offset ? g_offset_x : 0.0f;
    const float c = std::cos(a), s = std::sin(a);
    const float rotation[16] = {c, 0, -s, 0, 0, 1, 0, 0, s, 0, c, 0, 0, 0, 0, 1};
    const float translation[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, -x, 0, 0, 1};  // camera moves +x
    multiply(rotation, translation, g_eye_full);
    std::memcpy(g_eye_rotation, rotation, sizeof rotation);
}

bool game_has_focus() {
    DWORD pid = 0;
    GetWindowThreadProcessId(GetForegroundWindow(), &pid);
    return pid == GetCurrentProcessId();
}

bool hotkey_pressed() {
    return game_has_focus() && (GetAsyncKeyState(VK_CONTROL) & 0x8000) && (GetAsyncKeyState(VK_SHIFT) & 0x8000) &&
           (GetAsyncKeyState(VK_F9) & 0x8000);
}

}  // namespace

void configure(float yaw_degrees, float offset_x, int start_mode) {
    g_yaw_radians = yaw_degrees * 3.14159265f / 180.0f;
    g_offset_x = offset_x;
    g_mode = static_cast<Mode>(start_mode & 3);
    rebuild_eye_transform();
}

void on_frame() {
    // Report what the first full frame in a new mode actually overrode.
    if (g_mode != Mode::Off && ++g_frames_in_mode == 1)
        HW2VR_LOG("spike [%s]: overrode inMatV x%u, inMatC x%u, inMatMV-as-view x%u, view-space inMatP x%u in one frame",
                  kModeNames[static_cast<int>(g_mode)], g_counts[0], g_counts[1], g_counts[2], g_counts[3]);
    std::memset(g_counts, 0, sizeof g_counts);

    const bool down = hotkey_pressed();
    if (down && !g_hotkey_down) {
        g_mode = static_cast<Mode>((static_cast<int>(g_mode) + 1) % 4);
        g_frames_in_mode = 0;
        rebuild_eye_transform();
        HW2VR_LOG("spike mode -> %s", kModeNames[static_cast<int>(g_mode)]);
    }
    g_hotkey_down = down;
}

bool override_active() { return g_mode != Mode::Off; }

void apply_view_override(const float in[16], float out[16], CameraUniform source) {
    ++g_counts[static_cast<int>(source)];
    const bool rotation_only = std::fabs(in[12]) < 1.0f && std::fabs(in[13]) < 1.0f && std::fabs(in[14]) < 1.0f;
    multiply(rotation_only ? g_eye_rotation : g_eye_full, in, out);
}

void apply_projection_override(const float proj[16], float out[16]) {
    ++g_counts[static_cast<int>(CameraUniform::InMatP)];
    multiply(proj, g_eye_full, out);
}

bool mode_indicator_color(float rgb[3]) {
    static constexpr float kColors[4][3] = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}, {0, 0.4f, 1}};  // -, yaw, offset, both
    if (g_mode == Mode::Off) return false;
    std::memcpy(rgb, kColors[static_cast<int>(g_mode)], sizeof kColors[0]);
    return true;
}

}  // namespace hw2vr::stereo
