#include "vr/vr_frame.h"

#include <cstring>

#include "common/hotkeys.h"
#include "common/log.h"
#include "common/mat4.h"
#include "hud/hud_layer.h"
#include "hud/hud_panel.h"
#include "input/controls.h"
#include "proxy/gl_api.h"
#include "proxy/gl_hooks.h"
#include "stereo/eye_math.h"
#include "stereo/frame_replay.h"
#include "stereo/view_override.h"
#include "vr/openvr_session.h"

namespace hw2vr::vrframe {
namespace {

Settings g_settings;
std::wstring g_dll_dir;
bool g_autostart_pending = false;

int g_eye = 0;                    // eye the game is rendering this frame
bool g_have_pose = false;         // a camera was set for the current frame
float g_pose_used[2][16];         // HMD pose each eye's texture was rendered from
bool g_eye_valid[2] = {};
bool g_recentre = false;          // recentre on the next pose (F10, or the headset was just put on)
bool g_was_in_use = false;
float g_recentre_inverse[16];     // identity = SteamVR's seated origin until we recentre
float g_camera_pose[16];          // its inverse: the seated pose of the game camera

HWND g_window = nullptr;
int g_client_w = 0, g_client_h = 0;
long long g_last_input_qpc = 0;

int g_width = 0, g_height = 0;
gl::GLuint g_eye_tex[2] = {}, g_eye_fbo[2] = {};

float g_eye_view[2][16];          // this frame's camera for each eye (from one head pose)
stereo::Frustum g_eye_frustum[2] = {};
float g_frame_pose[16];           // the head pose this frame was rendered from

LARGE_INTEGER g_qpf = {};
long long g_window_start = 0;
unsigned g_window_frames = 0, g_window_replays = 0;
double g_window_replay_ms = 0.0;
const char* g_last_fallback = "";

void release_targets() {
    const gl::Api& a = gl::api();
    if (a.DeleteTextures && g_eye_tex[0]) {
        a.DeleteTextures(2, g_eye_tex);
        a.DeleteFramebuffers(2, g_eye_fbo);
    }
    g_eye_tex[0] = g_eye_tex[1] = g_eye_fbo[0] = g_eye_fbo[1] = 0;
    g_width = g_height = 0;
}

bool ensure_targets(int w, int h) {
    if (w == g_width && h == g_height && g_eye_tex[0]) return true;
    release_targets();
    const gl::Api& a = gl::api();
    gl::StateGuard guard;
    a.GenTextures(2, g_eye_tex);
    a.GenFramebuffers(2, g_eye_fbo);
    for (int i = 0; i < 2; ++i) {
        a.BindTexture(gl::TEXTURE_2D, g_eye_tex[i]);
        a.TexImage2D(gl::TEXTURE_2D, 0, gl::RGBA8, w, h, 0, gl::RGBA, gl::UNSIGNED_BYTE, nullptr);
        a.TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_MIN_FILTER, gl::LINEAR);
        a.TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_MAG_FILTER, gl::LINEAR);
        a.BindFramebuffer(gl::DRAW_FRAMEBUFFER, g_eye_fbo[i]);
        a.FramebufferTexture2D(gl::DRAW_FRAMEBUFFER, gl::COLOR_ATTACHMENT0, gl::TEXTURE_2D, g_eye_tex[i], 0);
        if (a.CheckFramebufferStatus(gl::DRAW_FRAMEBUFFER) != gl::FRAMEBUFFER_COMPLETE) {
            HW2VR_LOG("vr: eye target %dx%d incomplete", w, h);
            return false;
        }
    }
    g_width = w;
    g_height = h;
    g_eye_valid[0] = g_eye_valid[1] = false;
    HW2VR_LOG("vr: eye targets %dx%d (the game's back buffer)", w, h);
    return true;
}

void start_vr() {
    if (!gl::load()) {
        HW2VR_LOG("vr: GL entry points missing - staying flat");
        return;
    }
    HW2VR_LOG("vr: starting (world scale %.0f units/m). Single-player/skirmish only!", g_settings.world_scale);
    if (!vr::start(g_dll_dir)) return;
    hud::set_capture(true, g_settings.hud_premultiplied);
    panel::start();
    controls::start((g_dll_dir + L"hw2vr_bindings\\action_manifest.json").c_str());
    g_recentre = false;
    g_was_in_use = false;  // if the headset is already on, the first pose recentres
    g_have_pose = false;
    g_last_input_qpc = 0;
    g_eye_valid[0] = g_eye_valid[1] = false;
}

void stop_vr() {
    controls::stop();
    panel::stop();
    stereo::clear_vr_eye();
    hud::set_capture(false, g_settings.hud_premultiplied);
    hud::release();
    release_targets();
    vr::stop();
    g_have_pose = false;
}

void recentred() {
    mat4::rigid_inverse(g_recentre_inverse, g_camera_pose);
    panel::recentre(g_camera_pose);  // the panel sits in front of the game camera's direction
}

void change_world_scale(int steps) {
    for (; steps > 0; --steps) g_settings.world_scale *= 1.25f;
    for (; steps < 0; ++steps) g_settings.world_scale *= 0.8f;
    HW2VR_LOG("vr: world scale %.1f game units per metre", g_settings.world_scale);
}

void handle_hotkeys() {
    if (hotkeys::pressed(g_settings.key_toggle) || g_autostart_pending) {
        g_autostart_pending = false;
        vr::running() ? stop_vr() : start_vr();
    }
    if (!vr::running()) return;
    if (hotkeys::pressed(g_settings.key_recentre)) {
        g_recentre = true;
        HW2VR_LOG("vr: recentre");
    }
    const bool scale_up = hotkeys::pressed(g_settings.key_scale_up), scale_down = hotkeys::pressed(g_settings.key_scale_down);
    if (scale_up || scale_down) change_world_scale(scale_up ? 1 : -1);
}

// The controllers, once per frame with the new poses.
void update_controls(const float hmd[16]) {
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    const double dt = g_last_input_qpc ? double(now.QuadPart - g_last_input_qpc) / double(g_qpf.QuadPart) : 0.0;
    g_last_input_qpc = now.QuadPart;
    controls::Frame f;
    f.window = g_window;
    f.client_w = g_client_w;
    f.client_h = g_client_h;
    f.hmd = hmd;
    f.camera_pose = g_camera_pose;
    f.game_projection = stereo::game_projection();
    f.dt = dt > 0.1 ? 0.1 : dt;  // a hitch must not fling the camera
    const controls::Requests req = controls::update(f);
    if (req.recentre) {
        g_recentre = true;
        HW2VR_LOG("vr: recentre (controller)");
    }
    if (req.world_scale_steps) change_world_scale(req.world_scale_steps);
}


void capture_eye(int eye) {
    const gl::Api& a = gl::api();
    gl::StateGuard guard;
    gl::StateGuard::neutral();
    a.BindFramebuffer(gl::READ_FRAMEBUFFER, 0);
    a.ReadBuffer(gl::BACK);
    a.BindFramebuffer(gl::DRAW_FRAMEBUFFER, g_eye_fbo[eye]);
    a.BlitFramebuffer(0, 0, g_width, g_height, 0, 0, g_width, g_height, gl::COLOR_BUFFER_BIT, gl::NEAREST);
    g_eye_valid[eye] = true;
}

void set_eye_camera(int eye) { stereo::set_vr_eye(g_eye_view[eye], g_eye_frustum[eye]); }

// Every few seconds: how fast the game runs in VR and what synchronized stereo costs.
void telemetry(bool replayed, double replay_ms) {
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    if (g_window_start == 0) g_window_start = now.QuadPart;
    ++g_window_frames;
    if (replayed) {
        ++g_window_replays;
        g_window_replay_ms += replay_ms;
    }
    const double seconds = double(now.QuadPart - g_window_start) / double(g_qpf.QuadPart);
    if (seconds < 5.0) return;
    const replay::Stats s = replay::stats();
    if (g_settings.sync_stereo)
        HW2VR_LOG("vr: %.1f fps | synchronized stereo: %u/%u frames replayed, %.2f ms CPU per replay, %u GL calls and "
                  "%u KB per frame%s%s",
                  g_window_frames / seconds, g_window_replays, g_window_frames,
                  g_window_replays ? g_window_replay_ms / g_window_replays : 0.0, s.commands, s.bytes / 1024,
                  g_window_replays < g_window_frames ? " | last fallback: " : "",
                  g_window_replays < g_window_frames ? g_last_fallback : "");
    else
        HW2VR_LOG("vr: %.1f fps | alternating-frame stereo (%.1f updates per eye per second)", g_window_frames / seconds,
                  g_window_frames / seconds / 2.0);
    g_window_start = now.QuadPart;
    g_window_frames = g_window_replays = 0;
    g_window_replay_ms = 0.0;
}

}  // namespace

void configure(const Settings& settings, const std::wstring& dll_dir) {
    g_settings = settings;
    g_dll_dir = dll_dir;
    g_autostart_pending = settings.autostart;
    mat4::identity(g_recentre_inverse);
    mat4::identity(g_camera_pose);
    QueryPerformanceFrequency(&g_qpf);
}

bool active() { return vr::running(); }
bool wants_recording() { return vr::running() && g_settings.sync_stereo && g_have_pose; }

void before_swap(HDC dc) {
    handle_hotkeys();
    if (!vr::running()) return;

    RECT rc;
    g_window = WindowFromDC(dc);
    if (!GetClientRect(g_window, &rc) || rc.right <= 0 || rc.bottom <= 0) return;
    g_client_w = rc.right;
    g_client_h = rc.bottom;
    if (!ensure_targets(rc.right, rc.bottom)) return;

    if (g_have_pose) {
        bool replayed = false;
        double replay_ms = 0.0;
        if (g_settings.sync_stereo) {
            // The game just rendered the left eye; replay the same frame for the right eye.
            capture_eye(0);
            std::memcpy(g_pose_used[0], g_frame_pose, sizeof g_frame_pose);
            if (replay::replayable()) {
                LARGE_INTEGER t0, t1;
                QueryPerformanceCounter(&t0);
                set_eye_camera(1);
                proxy::replay_frame();
                capture_eye(1);
                QueryPerformanceCounter(&t1);
                std::memcpy(g_pose_used[1], g_frame_pose, sizeof g_frame_pose);
                replayed = true;
                replay_ms = 1000.0 * double(t1.QuadPart - t0.QuadPart) / double(g_qpf.QuadPart);
            } else {
                // The right eye keeps its previous image and pose; the compositor reprojects it.
                replay::note_skipped();
                g_last_fallback = replay::why_not();
            }
            replay::flush_deferred_deletes();
        } else {
            capture_eye(g_eye);
        }
        for (int eye = 0; eye < 2; ++eye)
            if (g_eye_valid[eye]) vr::submit(eye, g_eye_tex[eye], g_pose_used[eye]);
        telemetry(replayed, replay_ms);
    }
    panel::update_texture(g_frame_pose, static_cast<float>(g_client_h) / static_cast<float>(g_client_w));
    controls::before_swap();
}

void after_swap() {
    hud::end_frame();
    if (!vr::running()) return;
    float hmd[16];
    if (!vr::wait_poses(hmd)) {
        g_have_pose = false;  // tracking lost: keep rendering, skip submitting until it returns
        return;
    }
    const bool in_use = vr::hmd_in_use();
    if (in_use && !g_was_in_use) {
        g_recentre = true;
        HW2VR_LOG("vr: headset put on - recentring");
    }
    g_was_in_use = in_use;
    if (g_recentre) {
        stereo::recentre_inverse_from(hmd, g_recentre_inverse);
        g_recentre = false;
        recentred();
    } else if (!panel::placed()) {
        recentred();  // not recentred yet (headset not worn): SteamVR's seated origin
    }
    update_controls(hmd);
    for (int eye = 0; eye < 2; ++eye) {
        stereo::eye_from_game_camera(hmd, g_recentre_inverse, vr::eye_to_head(eye), g_settings.world_scale, g_eye_view[eye]);
        const vr::EyeFrustum& f = vr::frustum(eye);
        g_eye_frustum[eye] = stereo::Frustum{f.left, f.right, f.down, f.up};
    }
    std::memcpy(g_frame_pose, hmd, sizeof hmd);
    if (g_settings.sync_stereo) {
        set_eye_camera(0);  // the game renders the left eye; the right is replayed at the next swap
    } else {
        g_eye ^= 1;  // alternating-frame rendering
        set_eye_camera(g_eye);
        std::memcpy(g_pose_used[g_eye], hmd, sizeof hmd);
    }
    g_have_pose = true;
}

}  // namespace hw2vr::vrframe
