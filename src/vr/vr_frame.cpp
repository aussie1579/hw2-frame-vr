#include "vr/vr_frame.h"

#include <cstring>

#include "common/hotkeys.h"
#include "common/log.h"
#include "common/mat4.h"
#include "hud/hud_layer.h"
#include "proxy/gl_api.h"
#include "stereo/eye_math.h"
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
bool g_recentre = true;
float g_recentre_inverse[16];
bool g_hud_placed = false;
float g_hud_tan = 0.0f;

int g_width = 0, g_height = 0;
gl::GLuint g_eye_tex[2] = {}, g_eye_fbo[2] = {};

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
    HW2VR_LOG("vr: starting (world scale %.0f units/m, HUD %.1f m, HUD scale %.2f). Single-player/skirmish only!",
              g_settings.world_scale, g_settings.hud_distance, g_settings.hud_scale);
    if (!vr::start(g_dll_dir)) return;
    vr::hud_create(g_settings.hud_premultiplied);
    hud::set_capture(true, g_settings.hud_premultiplied);
    g_recentre = true;
    g_have_pose = false;
    g_hud_placed = false;
    g_eye_valid[0] = g_eye_valid[1] = false;
}

void stop_vr() {
    stereo::clear_vr_eye();
    hud::set_capture(false, g_settings.hud_premultiplied);
    hud::release();
    release_targets();
    vr::stop();
    g_have_pose = false;
}

void place_hud() {
    // Fixed in the game camera's frame (= the recentre pose), sized to the game's own horizontal field of view so
    // CPU-positioned markers line up with their ships from the centre of the head.
    float game_camera[16], offset[16], hud_pose[16];
    mat4::rigid_inverse(g_recentre_inverse, game_camera);
    mat4::translation(0.0f, 0.0f, -g_settings.hud_distance, offset);
    mat4::multiply(game_camera, offset, hud_pose);
    g_hud_tan = stereo::game_tan_half_fov_x();
    vr::hud_place(hud_pose, g_settings.hud_scale * 2.0f * g_settings.hud_distance * g_hud_tan);
    vr::hud_visible(true);
    g_hud_placed = true;
}

void handle_hotkeys() {
    if (hotkeys::pressed(VK_F8) || g_autostart_pending) {
        g_autostart_pending = false;
        vr::running() ? stop_vr() : start_vr();
    }
    if (!vr::running()) return;
    if (hotkeys::pressed(VK_F10)) {
        g_recentre = true;
        HW2VR_LOG("vr: recentre");
    }
    const bool scale_up = hotkeys::pressed(VK_PRIOR), scale_down = hotkeys::pressed(VK_NEXT);
    if (scale_up || scale_down) {
        g_settings.world_scale *= scale_up ? 1.25f : 0.8f;
        HW2VR_LOG("vr: world scale %.1f game units per metre", g_settings.world_scale);
    }
}

}  // namespace

void configure(const Settings& settings, const std::wstring& dll_dir) {
    g_settings = settings;
    g_dll_dir = dll_dir;
    g_autostart_pending = settings.autostart;
    mat4::identity(g_recentre_inverse);
}

void before_swap(HDC dc) {
    handle_hotkeys();
    if (!vr::running()) return;

    RECT rc;
    if (!GetClientRect(WindowFromDC(dc), &rc) || rc.right <= 0 || rc.bottom <= 0) return;
    hud::set_target_size(rc.right, rc.bottom);
    if (!ensure_targets(rc.right, rc.bottom)) return;

    if (g_have_pose) {
        const gl::Api& a = gl::api();
        {
            gl::StateGuard guard;
            gl::StateGuard::neutral();
            a.BindFramebuffer(gl::READ_FRAMEBUFFER, 0);
            a.ReadBuffer(gl::BACK);
            a.BindFramebuffer(gl::DRAW_FRAMEBUFFER, g_eye_fbo[g_eye]);
            a.BlitFramebuffer(0, 0, g_width, g_height, 0, 0, g_width, g_height, gl::COLOR_BUFFER_BIT, gl::NEAREST);
        }
        g_eye_valid[g_eye] = true;
        for (int eye = 0; eye < 2; ++eye)
            if (g_eye_valid[eye]) vr::submit(eye, g_eye_tex[eye], g_pose_used[eye]);
    }
    if (hud::captured_this_frame()) vr::hud_set_texture(hud::overlay_texture());
    if (!g_hud_placed || g_hud_tan != stereo::game_tan_half_fov_x()) place_hud();
}

void after_swap() {
    hud::end_frame();
    if (!vr::running()) return;
    float hmd[16];
    if (!vr::wait_poses(hmd)) {
        g_have_pose = false;  // tracking lost: keep rendering, skip submitting until it returns
        return;
    }
    if (g_recentre) {
        stereo::recentre_inverse_from(hmd, g_recentre_inverse);
        g_recentre = false;
        g_hud_placed = false;
    }
    g_eye ^= 1;
    float e[16];
    stereo::eye_from_game_camera(hmd, g_recentre_inverse, vr::eye_to_head(g_eye), g_settings.world_scale, e);
    const vr::EyeFrustum& f = vr::frustum(g_eye);
    stereo::set_vr_eye(e, stereo::Frustum{f.left, f.right, f.down, f.up});
    std::memcpy(g_pose_used[g_eye], hmd, sizeof hmd);
    g_have_pose = true;
}

}  // namespace hw2vr::vrframe
