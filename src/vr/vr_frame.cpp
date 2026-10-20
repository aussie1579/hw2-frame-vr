#include "vr/vr_frame.h"

#include <cmath>
#include <cstring>

#include "common/hotkeys.h"
#include "common/log.h"
#include "common/mat4.h"
#include "hud/hud_layer.h"
#include "hud/hud_panel.h"
#include "hud/screen_windows.h"
#include "input/controls.h"
#include "proxy/engine_hooks.h"
#include "proxy/gl_api.h"
#include "proxy/gl_hooks.h"
#include "stereo/depth_probe.h"
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
// The game camera turns with the head (engine hook): its pose this frame, turned, and the inverse.
float g_game_pose[16], g_game_inverse[16];
float g_turn_yaw = 0.0f, g_turn_pitch = 0.0f;  // degrees, for the log
unsigned g_last_camera_updates = 0;
bool g_turn_test_on = false;  // [debug] camera_turn_test, flat screen
float g_logged_fov = 0.0f;
float g_held_turn[16];         // the game camera's turn, held while the gaze stays near it
bool g_have_held_turn = false;
// Screen mode: menus and loading screens have no 3D world (the game camera isn't updated); a flat full-screen image in
// each eye's own (off-centre) view would show up twice, side by side. Instead the eyes go dark and the game's whole
// screen appears on the panel in front of the player.
bool g_screen_mode = false;
unsigned g_last_camera_calls = 0;
int g_frames_without_scene = 0;

HWND g_window = nullptr;
int g_client_w = 0, g_client_h = 0;
bool g_window_checked = false;  // fit_window ran for this VR session
bool g_window_moved = false;
RECT g_window_rect_before = {};  // where it was, to put it back when VR stops
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
unsigned g_last_probes = 0, g_last_hits = 0, g_last_grids = 0, g_last_markers = 0, g_last_anchored = 0, g_last_held = 0;

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

// The laser moves the real cursor, which can't leave the desktop: a windowed game whose title bar pushes the client
// area past the screen edge has UI the laser can't reach. While VR runs, the window is moved so its client area
// starts at its monitor's top-left (the title bar goes off-screen); stop_vr puts it back.
void fit_window(HWND window) {
    g_window_checked = true;
    RECT client{}, frame{};
    if (!GetClientRect(window, &client) || !GetWindowRect(window, &frame)) return;
    POINT tl{0, 0};
    ClientToScreen(window, &tl);
    MONITORINFO mi{sizeof mi};
    if (!GetMonitorInfoW(MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST), &mi)) return;
    const RECT& m = mi.rcMonitor;
    const bool fits = tl.x >= m.left && tl.y >= m.top && tl.x + client.right <= m.right && tl.y + client.bottom <= m.bottom;
    if (fits) return;
    if (client.right > m.right - m.left || client.bottom > m.bottom - m.top) {
        HW2VR_LOG("vr: the game's client area %ldx%ld is bigger than its monitor - left where it is (the laser's mouse "
                  "events go straight to the window: [controls] mouse = auto)", client.right, client.bottom);
        return;
    }
    const int x = frame.left + (m.left - tl.x), y = frame.top + (m.top - tl.y);
    g_window_rect_before = frame;
    g_window_moved = SetWindowPos(window, nullptr, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_ASYNCWINDOWPOS) != 0;
    HW2VR_LOG("vr: client area was partly off-screen at (%ld, %ld) - window moved so it starts at the monitor's corner "
              "(%ld, %ld)%s", tl.x, tl.y, m.left, m.top, g_window_moved ? "" : " FAILED");
}

void restore_window() {
    if (g_window_moved && g_window && IsWindow(g_window))
        SetWindowPos(g_window, nullptr, g_window_rect_before.left, g_window_rect_before.top, 0, 0,
                     SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_ASYNCWINDOWPOS);
    g_window_moved = false;
    g_window_checked = false;
}

void stop_vr() {
    restore_window();
    engine::set_camera_turn(nullptr);
    engine::set_camera_fov(0.0f);
    controls::stop();
    panel::stop();
    depth::release();
    stereo::clear_vr_eye();
    hud::set_capture(false, g_settings.hud_premultiplied);
    hud::release();
    release_targets();
    vr::stop();
    g_have_pose = false;
}

void recentred() {
    g_have_held_turn = false;
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
    if (hotkeys::pressed(VK_F6)) hud::debug_dump(g_dll_dir);  // what the panel is fed and shows
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
    f.camera_pose = g_game_pose;
    f.game_projection = stereo::game_projection();
    f.world_scale = g_settings.world_scale;
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

void set_eye_camera(int eye) {
    if (g_screen_mode) return stereo::clear_vr_eye();  // the game draws its screen untouched
    stereo::set_vr_eye(g_eye_view[eye], g_eye_frustum[eye]);
}

// No camera update for two frames = a menu or loading screen (needs the camera hook; without it, never).
void update_screen_mode() {
    if (!engine::camera_hook_installed()) return;
    const unsigned calls = engine::camera_calls();
    g_frames_without_scene = calls != g_last_camera_calls ? 0 : g_frames_without_scene + 1;
    g_last_camera_calls = calls;
    const bool screen = g_frames_without_scene >= 2;
    if (screen == g_screen_mode) return;
    g_screen_mode = screen;
    hud::set_screen_mode(screen);
    panel::set_screen_mode(screen);
    screens::set_suspended(screen);
    if (screen) stereo::clear_vr_eye();
    HW2VR_LOG("vr: %s", screen ? "menu screen (no 3D world) - the game's screen is on the panel in front of you"
                               : "3D world - back in the battlefield");
}

void clear_eyes() {
    const gl::Api& a = gl::api();
    gl::StateGuard guard;
    gl::StateGuard::neutral();
    a.ClearColor(0.02f, 0.02f, 0.03f, 1.0f);
    for (int eye = 0; eye < 2; ++eye) {
        a.BindFramebuffer(gl::DRAW_FRAMEBUFFER, g_eye_fbo[eye]);
        a.Clear(gl::COLOR_BUFFER_BIT);
        g_eye_valid[eye] = true;
    }
}

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
    const depth::TestState d = depth::test_state();
    const hud::TestState h = hud::test_state();
    if (d.probes != g_last_probes || h.world_draws != g_last_markers)
        HW2VR_LOG("vr: laser depth: %u probes, %u touching something | world markers: %u drawn, %u on a ship, "
                  "%u depth jumps held back, %u depth grids (last 5 s)", d.probes - g_last_probes, d.hits - g_last_hits,
                  h.world_draws - g_last_markers, h.world_anchored - g_last_anchored,
                  hud::world_marker_jumps_held() - g_last_held, d.grids - g_last_grids);
    g_last_held = hud::world_marker_jumps_held();
    if (g_settings.turn_game_camera && engine::camera_hook_installed()) {
        const unsigned updates = engine::camera_updates();
        HW2VR_LOG("vr: game camera turned with the head (yaw %.0f, pitch %.0f deg) in %u camera updates (last 5 s)",
                  g_turn_yaw, g_turn_pitch, updates - g_last_camera_updates);
        g_last_camera_updates = updates;
    }
    g_last_probes = d.probes, g_last_hits = d.hits, g_last_grids = d.grids;
    g_last_markers = h.world_draws, g_last_anchored = h.world_anchored;
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

// game_fov = auto: the game camera's vertical FOV that covers both eyes' views around the gaze (the game culls to its
// own view and the laser can only aim inside it). The game's aspect sets the horizontal part.
float covering_fov() {
    if (g_client_w <= 0 || g_client_h <= 0) return 0.0f;
    const float aspect = static_cast<float>(g_client_w) / static_cast<float>(g_client_h);
    float tan_v = 0.0f, tan_h = 0.0f;
    for (int eye = 0; eye < 2; ++eye) {
        const vr::EyeFrustum& f = vr::frustum(eye);
        tan_v = std::fmax(tan_v, std::fmax(-f.down, f.up));
        tan_h = std::fmax(tan_h, std::fmax(-f.left, f.right));
    }
    // Plus the dead zone: the gaze can be that far off the game camera's axis.
    const float dz = g_settings.game_camera_deadzone * 0.01745329f + 0.02f;  // and ~1 degree for the turn's latency
    const float half_v = std::atan(tan_v) + dz;
    const float half_h = std::atan(std::tan(std::fmin(std::atan(tan_h) + dz, 1.5f)) / aspect);
    const float fov = std::fmin(2.0f * std::fmax(half_v, half_h) * 57.29578f, 160.0f);
    if (std::fabs(fov - g_logged_fov) > 0.5f) {
        HW2VR_LOG("vr: game field of view %.0f deg tall (aspect %.2f) to cover the headset's view", fov, aspect);
        g_logged_fov = fov;
    }
    return fov;
}

// The game camera turns to look where the head looks (yaw and pitch) when the engine's camera hook is in: the game
// then culls, picks and places its markers for the player's view direction, and the laser can click anything the
// player faces. The turn is set before the game's camera updates of the coming frame, and the eyes are placed
// relative to the turned camera, so the picture doesn't change.
void update_game_turn(const float hmd[16]) {
    float turn[16];
    if (g_settings.turn_game_camera && engine::camera_hook_installed()) {
        float head[16], gaze[16];
        mat4::multiply(g_recentre_inverse, hmd, head);
        stereo::game_camera_turn(head, gaze);
        // The game puts its markers (brackets, health bars) on whole pixels: a camera that turns with every tiny head
        // movement makes them shimmer against their ships. So it holds still until the gaze has moved
        // game_camera_deadzone degrees from where it points, then catches up.
        const float cos_dz = std::cos(g_settings.game_camera_deadzone * 0.01745329f);
        if (!g_have_held_turn || gaze[8] * g_held_turn[8] + gaze[9] * g_held_turn[9] + gaze[10] * g_held_turn[10] < cos_dz) {
            std::memcpy(g_held_turn, gaze, sizeof gaze);
            g_have_held_turn = true;
        }
        std::memcpy(turn, g_held_turn, sizeof turn);
        engine::set_camera_turn(turn);
        engine::set_camera_fov(g_settings.game_fov < 0.0f ? covering_fov() : g_settings.game_fov);
        g_turn_yaw = mat4::yaw_of(turn) * 57.29578f;
        g_turn_pitch = std::asin(-turn[9]) * 57.29578f;  // forward = -Z axis: its height is -turn[9]
    } else {
        mat4::identity(turn);
    }
    depth::set_game_turn(turn);
    mat4::multiply(g_camera_pose, turn, g_game_pose);
    mat4::rigid_inverse(g_game_pose, g_game_inverse);
}

// [debug] camera_turn_test (flat screen): Ctrl+Shift+F7 turns the game camera that many degrees to the right and the
// drawing back by the same amount. The picture should not change (world markers included) while the game culls and
// picks as if looking aside - a check of the camera hook without a headset.
void flat_turn_test() {
    if (g_settings.camera_turn_test == 0.0f) return;
    if (hotkeys::pressed(g_settings.key_turn_test)) {
        g_turn_test_on = !g_turn_test_on;
        HW2VR_LOG("vr: camera turn test %s (%.0f deg, hook %s)", g_turn_test_on ? "on" : "off", g_settings.camera_turn_test,
                  engine::camera_hook_installed() ? "installed" : "NOT installed");
        if (!g_turn_test_on) {
            engine::set_camera_turn(nullptr);
            stereo::set_debug_eye(nullptr);
        }
    }
    if (!g_turn_test_on) return;
    // Negative: the turn flips between right and left every frame - anything the game places with an older camera
    // than the frame's lands visibly off (2x the angle).
    // Under 1 degree: a slow sweep, that many degrees per frame back and forth over +-3 degrees (for seeing whether the
    // game's markers move smoothly or in steps as the camera turns).
    static bool flip = false;
    static float sweep = 0.0f, step = 0.0f;
    flip = !flip;
    float degrees = g_settings.camera_turn_test < 0.0f && flip ? g_settings.camera_turn_test : std::fabs(g_settings.camera_turn_test);
    if (std::fabs(g_settings.camera_turn_test) < 1.0f) {
        if (step == 0.0f) step = g_settings.camera_turn_test;
        sweep += step;
        if (std::fabs(sweep) > 3.0f) step = -step;
        degrees = sweep;
    }
    float turn[16];
    mat4::rotation_y(-degrees * 0.01745329f, turn);  // positive: to the right
    engine::set_camera_turn(turn);
    stereo::set_debug_eye(turn);  // E * turn with E = identity: undoes the turn in the drawing
}

}  // namespace

void configure(const Settings& settings, const std::wstring& dll_dir) {
    g_settings = settings;
    g_dll_dir = dll_dir;
    g_autostart_pending = settings.autostart;
    mat4::identity(g_recentre_inverse);
    mat4::identity(g_camera_pose);
    mat4::identity(g_game_pose);
    mat4::identity(g_game_inverse);
    QueryPerformanceFrequency(&g_qpf);
}

bool active() { return vr::running(); }
bool wants_recording() { return vr::running() && g_settings.sync_stereo && g_have_pose && !g_screen_mode; }

void before_swap(HDC dc) {
    handle_hotkeys();
    if (!vr::running()) return;

    RECT rc;
    g_window = WindowFromDC(dc);
    if (g_settings.fit_window && !g_window_checked) fit_window(g_window);
    if (!GetClientRect(g_window, &rc) || rc.right <= 0 || rc.bottom <= 0) return;
    g_client_w = rc.right;
    g_client_h = rc.bottom;
    if (!ensure_targets(rc.right, rc.bottom)) return;
    update_screen_mode();

    if (g_have_pose && g_screen_mode) {
        clear_eyes();
        replay::flush_deferred_deletes();  // a frame recorded before the switch
        for (int eye = 0; eye < 2; ++eye) vr::submit(eye, g_eye_tex[eye], g_frame_pose);
        telemetry(false, 0.0);
    } else if (g_have_pose) {
        bool replayed = false;
        double replay_ms = 0.0;
        if (g_settings.sync_stereo) {
            // The game just rendered the left eye; replay the same frame for the right eye.
            capture_eye(0);
            depth::run(g_width, g_height);  // the laser against the left eye's depth, before the replay redraws it
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
            depth::run(g_width, g_height);
        }
        for (int eye = 0; eye < 2; ++eye)
            if (g_eye_valid[eye]) vr::submit(eye, g_eye_tex[eye], g_pose_used[eye]);
        telemetry(replayed, replay_ms);
    }
    if (g_screen_mode) {
        hud::capture_screen();     // the game's finished screen -> the panel
    } else {
        hud::update_mask();        // where the screen UI is: menus up? build window open?
        screens::before_swap();
        panel::set_shown(screens::modal_open());  // the menu-only panel ([vr] hud = tablet)
    }
    panel::update_texture(g_frame_pose, static_cast<float>(g_client_h) / static_cast<float>(g_client_w));
    controls::before_swap();
}

void after_swap() {
    hud::end_frame();
    if (!vr::running()) return flat_turn_test();
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
    update_game_turn(hmd);
    panel::follow(g_game_pose, hmd, stereo::game_tan_half_fov_x());  // aligned layout: with the head, every frame
    for (int eye = 0; eye < 2; ++eye) {
        stereo::eye_from_game_camera(hmd, g_game_inverse, vr::eye_to_head(eye), g_settings.world_scale, g_eye_view[eye]);
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
    depth::poll_grid();  // where the scene is behind each part of the screen, for this frame's world markers
    // The laser is tested against the depth of the eye the game renders next.
    const int rendered = g_settings.sync_stereo ? 0 : g_eye;
    if (const float* game_proj = stereo::game_projection()) {
        float eye_proj[16];
        stereo::eye_projection(game_proj, g_eye_frustum[rendered], eye_proj);
        depth::set_camera(g_eye_view[rendered], eye_proj);
    }
    update_controls(hmd);
    g_have_pose = true;
}

}  // namespace hw2vr::vrframe
