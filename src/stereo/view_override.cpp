#include "stereo/view_override.h"

#include <cmath>
#include <cstring>

#include "common/hotkeys.h"
#include "common/log.h"
#include "common/mat4.h"

namespace hw2vr::stereo {
namespace {

enum class SpikeMode { Off, Yaw, Offset, YawAndOffset };
constexpr const char* kSpikeNames[] = {"off", "yaw", "offset", "yaw+offset"};

SpikeMode g_spike = SpikeMode::Off;
int g_spike_hotkey = 0x78;  // F9
float g_spike_yaw_radians = 0.0f;
float g_spike_offset_x = 0.0f;

bool g_vr = false;
bool g_debug = false;
unsigned g_generation = 1;
Frustum g_frustum = {};
float g_eye_full[16];      // E
float g_eye_rotation[16];  // E without translation, for skybox views
unsigned g_frames_in_mode = 0;
unsigned g_counts[4] = {};  // overrides per CameraUniform in the current frame
float g_game_tan_half_fov_x = 1.0f / 0.5978f;  // build 12653478 at 3440x1440 until the game tells us otherwise
float g_game_projection[16] = {};
bool g_have_game_projection = false;
unsigned g_scene_framebuffer = 0;

void set_eye(const float e[16]) {
    ++g_generation;
    std::memcpy(g_eye_full, e, sizeof g_eye_full);
    std::memcpy(g_eye_rotation, e, sizeof g_eye_rotation);
    g_eye_rotation[12] = g_eye_rotation[13] = g_eye_rotation[14] = 0.0f;
}

void rebuild_spike_transform() {
    const bool yaw = g_spike == SpikeMode::Yaw || g_spike == SpikeMode::YawAndOffset;
    const bool offset = g_spike == SpikeMode::Offset || g_spike == SpikeMode::YawAndOffset;
    float rotation[16], translation[16], e[16];
    mat4::rotation_y(yaw ? g_spike_yaw_radians : 0.0f, rotation);
    mat4::translation(offset ? -g_spike_offset_x : 0.0f, 0.0f, 0.0f, translation);  // camera moves +x
    mat4::multiply(rotation, translation, e);
    set_eye(e);
}

}  // namespace

void configure_spike(float yaw_degrees, float offset_x, int start_mode, int hotkey_vk) {
    g_spike_hotkey = hotkey_vk;
    g_spike_yaw_radians = yaw_degrees * 3.14159265f / 180.0f;
    g_spike_offset_x = offset_x;
    g_spike = static_cast<SpikeMode>(start_mode & 3);
    rebuild_spike_transform();
}

void on_frame() {
    if (!g_vr && g_spike != SpikeMode::Off && ++g_frames_in_mode == 1)
        HW2VR_LOG("spike [%s]: overrode inMatV x%u, inMatC x%u, inMatMV-as-view x%u, view-space inMatP x%u in one frame",
                  kSpikeNames[static_cast<int>(g_spike)], g_counts[0], g_counts[1], g_counts[2], g_counts[3]);
    std::memset(g_counts, 0, sizeof g_counts);

    if (hotkeys::pressed(g_spike_hotkey) && !g_vr) {
        g_spike = static_cast<SpikeMode>((static_cast<int>(g_spike) + 1) % 4);
        g_frames_in_mode = 0;
        rebuild_spike_transform();
        HW2VR_LOG("spike mode -> %s", kSpikeNames[static_cast<int>(g_spike)]);
    }
}

void set_vr_eye(const float eye_from_game_camera[16], const Frustum& frustum) {
    g_vr = true;
    g_frustum = frustum;
    set_eye(eye_from_game_camera);
}

void clear_vr_eye() {
    g_vr = false;
    rebuild_spike_transform();
}

bool vr_eye_active() { return g_vr; }

void set_debug_eye(const float* e) {
    g_debug = e != nullptr;
    if (e) set_eye(e);
    else rebuild_spike_transform();
}

unsigned camera_generation() { return g_generation; }

bool active() { return g_vr || g_debug || g_spike != SpikeMode::Off; }

void apply_view_override(const float in[16], float out[16], CameraUniform source) {
    ++g_counts[static_cast<int>(source)];
    const bool rotation_only = std::fabs(in[12]) < 1.0f && std::fabs(in[13]) < 1.0f && std::fabs(in[14]) < 1.0f;
    mat4::multiply(rotation_only ? g_eye_rotation : g_eye_full, in, out);
}

bool apply_projection_override(const float proj[16], float out[16], bool view_space_input) {
    float p[16];
    if (g_vr) eye_projection(proj, g_frustum, p);
    else std::memcpy(p, proj, sizeof p);
    if (view_space_input) {
        ++g_counts[static_cast<int>(CameraUniform::InMatP)];
        mat4::multiply(p, g_eye_full, out);
        return true;
    }
    if (!g_vr) return false;
    std::memcpy(out, p, sizeof p);
    return true;
}

void note_game_projection(const float proj[16], unsigned draw_framebuffer) {
    if (proj[0] > 0.0f) g_game_tan_half_fov_x = 1.0f / proj[0];
    std::memcpy(g_game_projection, proj, sizeof g_game_projection);
    g_have_game_projection = true;
    g_scene_framebuffer = draw_framebuffer;
}

unsigned scene_framebuffer() { return g_scene_framebuffer; }

float game_tan_half_fov_x() { return g_game_tan_half_fov_x; }
const float* game_projection() { return g_have_game_projection ? g_game_projection : nullptr; }

bool mode_indicator_color(float rgb[3]) {
    static constexpr float kColors[4][3] = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}, {0, 0.4f, 1}};  // -, yaw, offset, both
    if (g_vr) {
        rgb[0] = rgb[1] = rgb[2] = 1.0f;  // white: VR running
        return true;
    }
    if (g_spike == SpikeMode::Off) return false;
    std::memcpy(rgb, kColors[static_cast<int>(g_spike)], sizeof kColors[0]);
    return true;
}

}  // namespace hw2vr::stereo
