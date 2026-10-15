// Hooks for the exports we implement (wglGetProcAddress, wglSwapBuffers, glDrawArrays, glDrawElements) and the
// GL 2.0+ entry points the game fetches through wglGetProcAddress. Everything runs on the game's render thread.
//
// Where the camera enters a shader (docs/FINDINGS.md "Camera", all programs of build 12653478):
//   A. inMatV or inMatC present           -> that uniform is the camera view          (E * V)
//   B. only inMatMV + inMatP              -> inMatMV is model-view, if inMatP is perspective at draw time (E * MV)
//   C. only inMatP                        -> vertices are already in view space (CPU-transformed, e.g. the light
//                                            glows, prog "screenCorner"), if inMatP is perspective (P * E)
// B and C programs with an ortho inMatP are 2D HUD / post-processing and stay untouched. "Perspective" comes from
// the program's most recent inMatP upload. In VR every perspective inMatP also gets the eye frustum.
// This includes the 3D overlay the game draws into its HUD framebuffer (selection circles, move lines): those stay
// in the eye images at their ships' depth, while the 2D draws there are redirected onto the VR overlay (hud_layer.h).
#include "proxy/gl_hooks.h"

#include <windows.h>

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>
#include <cstring>
#include <type_traits>
#include <unordered_map>

#include "common/hotkeys.h"
#include "common/log.h"
#include "common/mat4.h"
#include "hud/hud_layer.h"
#include "hud/marker_geometry.h"
#include "input/tablet.h"
#include "input/ui_canvas.h"
#include "proxy/engine_hooks.h"
#include "proxy/gl_api.h"
#include "proxy/gl_record_hooks.h"
#include "proxy/real_gl.h"
#include "stereo/depth_probe.h"
#include "stereo/frame_replay.h"
#include "stereo/gl_snapshot.h"
#include "stereo/view_override.h"
#include "vr/vr_frame.h"

using GLenum = unsigned int;
using GLint = int;
using GLuint = unsigned int;
using GLsizei = int;
using GLboolean = unsigned char;
using GLfloat = float;
using GLchar = char;

namespace {

using PFN_wglGetProcAddress = PROC(WINAPI*)(LPCSTR);
using PFN_wglSwapBuffers = BOOL(WINAPI*)(HDC);
using PFN_glUniformMatrix4fv = void(APIENTRY*)(GLint, GLsizei, GLboolean, const GLfloat*);
using PFN_glGetUniformLocation = GLint(APIENTRY*)(GLuint, const GLchar*);
using PFN_glUseProgram = void(APIENTRY*)(GLuint);
using PFN_glLinkProgram = void(APIENTRY*)(GLuint);
using PFN_glDeleteProgram = void(APIENTRY*)(GLuint);
using PFN_glBindFramebuffer = void(APIENTRY*)(GLenum, GLuint);
using PFN_glDrawArrays = void(APIENTRY*)(GLenum, GLint, GLsizei);
using PFN_glDrawElements = void(APIENTRY*)(GLenum, GLsizei, GLenum, const void*);

PFN_wglGetProcAddress real_wglGetProcAddress = nullptr;
PFN_wglSwapBuffers real_wglSwapBuffers = nullptr;
PFN_glUniformMatrix4fv real_glUniformMatrix4fv = nullptr;
PFN_glGetUniformLocation real_glGetUniformLocation = nullptr;
PFN_glUseProgram real_glUseProgram = nullptr;
PFN_glLinkProgram real_glLinkProgram = nullptr;
PFN_glDeleteProgram real_glDeleteProgram = nullptr;
PFN_glBindFramebuffer real_glBindFramebuffer = nullptr;
PFN_glDrawArrays real_glDrawArrays = nullptr;      // GL 1.1 exports, resolved in init_hooks
PFN_glDrawElements real_glDrawElements = nullptr;

struct ProgramInfo {
    GLint loc_view = -1;  // inMatV
    GLint loc_cam = -1;   // inMatC
    GLint loc_mv = -1;    // inMatMV
    GLint loc_proj = -1;  // inMatP
    bool proj_is_perspective = false;  // from this program's last inMatP upload
    GLfloat raw_proj[16] = {};         // the game's last inMatP for this program (world markers are re-projected)
    bool has_raw_proj = false;

    // Group B: whether inMatMV carries the camera depends on the projection *at draw time*. Some programs switch
    // between 2D and 3D within a frame and upload inMatMV before inMatP (prog 68: HUD lines vs. Sensors Manager
    // ship icons), so the raw value is kept and the right one is uploaded just before each draw.
    GLfloat raw_mv[16] = {};
    bool has_raw_mv = false;
    bool mv_overridden = false;          // what the driver currently holds for inMatMV
    unsigned raw_version = 0, override_generation = 0, override_version = 0;

    bool has_camera_matrix() const { return loc_view >= 0 || loc_cam >= 0; }
    bool view_space_input() const { return !has_camera_matrix() && loc_mv < 0; }  // group C
    bool mv_may_be_camera() const { return !has_camera_matrix() && loc_mv >= 0; }   // group B
};

struct TrackedUniform {
    const char* name;
    GLint ProgramInfo::*slot;
};
constexpr TrackedUniform kTracked[] = {
    {"inMatV", &ProgramInfo::loc_view},
    {"inMatC", &ProgramInfo::loc_cam},
    {"inMatMV", &ProgramInfo::loc_mv},
    {"inMatP", &ProgramInfo::loc_proj},
};

std::unordered_map<GLuint, ProgramInfo> g_programs;  // node-based: element pointers survive rehashing
GLuint g_current_program = 0;
ProgramInfo* g_current = nullptr;
GLuint g_draw_fbo = 0;

ProgramInfo* find_program(GLuint program) {
    auto it = g_programs.find(program);
    return it == g_programs.end() ? nullptr : &it->second;
}

void forget_program(GLuint program) {
    g_programs.erase(program);
    if (program == g_current_program) g_current = nullptr;
}

// The game's main camera projection has an infinite far plane (m[10] == -1); the skybox's is finite.
bool is_main_projection(const GLfloat* m) { return std::fabs(m[10] + 1.0f) < 1e-4f; }

// Does this upload carry the camera view (group A in the file comment)? Group B is decided at draw time.
bool camera_view_source(const ProgramInfo& info, GLint loc, hw2vr::stereo::CameraUniform* source) {
    using hw2vr::stereo::CameraUniform;
    if (loc == info.loc_view) *source = CameraUniform::InMatV;
    else if (loc == info.loc_cam) *source = CameraUniform::InMatC;
    else return false;
    return true;
}

// Group B, just before a draw: the driver must hold E * MV when the current projection is perspective (3D) and the
// game's raw MV otherwise (2D). Re-uploaded whenever the camera moves on (every eye, every frame).
void sync_model_view(ProgramInfo& info) {
    const bool want_override = hw2vr::stereo::active() && info.proj_is_perspective;
    const unsigned generation = hw2vr::stereo::camera_generation();
    if (want_override) {
        if (info.mv_overridden && info.override_generation == generation && info.override_version == info.raw_version)
            return;
        GLfloat eye_mv[16];
        hw2vr::stereo::apply_view_override(info.raw_mv, eye_mv, hw2vr::stereo::CameraUniform::InMatMV);
        real_glUniformMatrix4fv(info.loc_mv, 1, 0 /* no transpose */, eye_mv);
        info.mv_overridden = true;
        info.override_generation = generation;
        info.override_version = info.raw_version;
    } else if (info.mv_overridden) {
        real_glUniformMatrix4fv(info.loc_mv, 1, 0 /* no transpose */, info.raw_mv);
        info.mv_overridden = false;
    }
}

// The hooks below record themselves for the second-eye replay, which runs them again (with that eye's camera)
// through these function-pointer variables.
void APIENTRY my_glUseProgram(GLuint program);
void APIENTRY my_glBindFramebuffer(GLenum target, GLuint fbo);
PFN_glUseProgram p_use_program = &my_glUseProgram;
PFN_glBindFramebuffer p_bind_framebuffer = &my_glBindFramebuffer;

void APIENTRY my_glUseProgram(GLuint program) {
    if (hw2vr::replay::g_recording) hw2vr::replay::SigOf<p_use_program>::record<p_use_program>(program);
    g_current_program = program;
    g_current = find_program(program);
    real_glUseProgram(program);
}

void APIENTRY my_glLinkProgram(GLuint program) {
    forget_program(program);  // locations can change on relink; the game re-queries them afterwards
    real_glLinkProgram(program);
}

void APIENTRY my_glDeleteProgram(GLuint program) {
    forget_program(program);
    real_glDeleteProgram(program);
}

void APIENTRY my_glBindFramebuffer(GLenum target, GLuint fbo) {
    if (hw2vr::replay::g_recording) hw2vr::replay::SigOf<p_bind_framebuffer>::record<p_bind_framebuffer>(target, fbo);
    if (target == hw2vr::gl::FRAMEBUFFER || target == hw2vr::gl::DRAW_FRAMEBUFFER) {
        if (fbo != g_draw_fbo) hw2vr::hud::on_draw_framebuffer_change(g_draw_fbo, fbo);
        g_draw_fbo = fbo;
    }
    real_glBindFramebuffer(target, fbo);
}

GLint APIENTRY my_glGetUniformLocation(GLuint program, const GLchar* name) {
    const GLint loc = real_glGetUniformLocation(program, name);
    if (loc < 0 || !name) return loc;
    for (const TrackedUniform& u : kTracked) {
        if (std::strcmp(name, u.name) != 0) continue;
        ProgramInfo& info = g_programs[program];
        info.*u.slot = loc;
        if (program == g_current_program) g_current = &info;
        HW2VR_VLOG("program %u: %s at location %d", program, name, loc);
        break;
    }
    return loc;
}

void APIENTRY my_glUniformMatrix4fv(GLint loc, GLsizei count, GLboolean transpose, const GLfloat* value);
void replay_uniform_matrix4fv(const hw2vr::replay::Cmd& c) {
    my_glUniformMatrix4fv(static_cast<GLint>(c.w[0]), static_cast<GLsizei>(c.w[1]), static_cast<GLboolean>(c.w[2]),
                          static_cast<const GLfloat*>(hw2vr::replay::payload(c.w[3])));
}

void APIENTRY my_glUniformMatrix4fv(GLint loc, GLsizei count, GLboolean transpose, const GLfloat* value) {
    if (hw2vr::replay::g_recording)
        hw2vr::replay::push({&replay_uniform_matrix4fv,
                             {static_cast<uint32_t>(loc), static_cast<uint32_t>(count), transpose,
                              hw2vr::replay::store(value, sizeof(GLfloat) * 16 * count)}});
    ProgramInfo* info = g_current;
    if (info && loc >= 0 && count == 1 && !transpose && value) {
        const bool hud_layer = hw2vr::hud::is_hud_fbo(g_draw_fbo);
        const bool override_camera = hw2vr::stereo::active();
        GLfloat replaced[16];
        if (loc == info->loc_proj) {
            std::memcpy(info->raw_proj, value, sizeof info->raw_proj);
            info->has_raw_proj = true;
            info->proj_is_perspective = hw2vr::mat4::is_perspective(value);
            if (!info->proj_is_perspective) {
                hw2vr::hud::note_ortho_projection(g_draw_fbo);
            } else {
                if (!hud_layer && is_main_projection(value)) hw2vr::stereo::note_game_projection(value, g_draw_fbo);
                if (override_camera &&
                    hw2vr::stereo::apply_projection_override(value, replaced, info->view_space_input())) {
                    real_glUniformMatrix4fv(loc, 1, transpose, replaced);
                    return;
                }
            }
        } else if (loc == info->loc_mv && info->mv_may_be_camera()) {
            std::memcpy(info->raw_mv, value, sizeof info->raw_mv);  // decided per draw (sync_model_view)
            info->has_raw_mv = true;
            info->mv_overridden = false;
            ++info->raw_version;
        } else if (hw2vr::stereo::CameraUniform source; override_camera && camera_view_source(*info, loc, &source)) {
            hw2vr::stereo::apply_view_override(value, replaced, source);
            real_glUniformMatrix4fv(loc, 1, transpose, replaced);
            return;
        }
    }
    real_glUniformMatrix4fv(loc, count, transpose, value);
}

// What a game draw call draws, for working out where a world marker sits on the screen.
struct DrawCall {
    bool elements;
    std::ptrdiff_t first;  // DrawArrays: first vertex; DrawElements: byte offset into the element buffer
    GLsizei count;
    GLenum index_type;
};

// A world-space marker (2D draw before the game's PHASE_UIIn): drawn into the HUD layer on the plane at its ship's
// depth, through the current eye, then the game's own projection is put back.
template <class Draw>
void draw_world_marker(const DrawCall& call, Draw draw) {
    static constexpr GLfloat kIdentity[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    ProgramInfo* info = g_current;
    float bounds[4];
    bool have_bounds = false;
    if (info && info->has_raw_proj && !hw2vr::hud::replaying())
        have_bounds = hw2vr::markers::screen_bounds(call.elements, call.first, call.count, call.index_type,
                                                    info->has_raw_mv ? info->raw_mv : kIdentity, info->raw_proj, bounds);
    const float depth = hw2vr::hud::world_marker_depth(have_bounds ? bounds : nullptr);
    GLfloat placed[16];
    if (info && info->loc_proj >= 0 && info->has_raw_proj && hw2vr::stereo::place_at_depth(info->raw_proj, depth, placed)) {
        real_glUniformMatrix4fv(info->loc_proj, 1, 0, placed);
        draw();
        real_glUniformMatrix4fv(info->loc_proj, 1, 0, info->raw_proj);
    } else {
        draw();
    }
}

// Every game draw: 2D draws into the HUD framebuffer, and UI drawn to the screen after the composite (the mouse
// cursor), go onto the VR overlay instead (hud_layer.h); world-space markers go into the world. Untracked programs
// count as 2D.
template <class Draw>
void game_draw(const DrawCall& call, Draw draw) {
    if (g_current && g_current->has_raw_mv) sync_model_view(*g_current);
    const bool is_2d = !g_current || !g_current->proj_is_perspective;
    switch (hw2vr::hud::draw_action(g_draw_fbo, g_current_program, is_2d)) {
        case hw2vr::hud::DrawAction::Draw:
            draw();
            break;
        case hw2vr::hud::DrawAction::Redirect:
            hw2vr::hud::begin_redirect(g_draw_fbo);
            draw();
            hw2vr::hud::end_redirect(g_draw_fbo);
            break;
        case hw2vr::hud::DrawAction::RedirectCursor:
            if (!hw2vr::hud::begin_cursor_redirect()) {
                draw();
                break;
            }
            draw();
            hw2vr::hud::end_cursor_redirect(g_draw_fbo);
            break;
        case hw2vr::hud::DrawAction::PlaceAtDepth:
            draw_world_marker(call, draw);
            break;
        case hw2vr::hud::DrawAction::Skip:
            break;
    }
}

void APIENTRY draw_arrays(GLenum mode, GLint first, GLsizei count) {
    game_draw({false, first, count, 0}, [&] { real_glDrawArrays(mode, first, count); });
}
void APIENTRY draw_elements(GLenum mode, GLsizei count, GLenum type, const void* indices) {
    game_draw({true, reinterpret_cast<std::ptrdiff_t>(indices), count, type},
              [&] { real_glDrawElements(mode, count, type, indices); });
}
PFN_glDrawArrays p_draw_arrays = &draw_arrays;
PFN_glDrawElements p_draw_elements = &draw_elements;

// Paints a 24 px square in the window's top-left corner on the default framebuffer (spike / VR status).
void draw_mode_indicator(HDC dc, const float rgb[3]) {
    constexpr int kSize = 24, kMargin = 8;
    RECT rc;
    if (!GetClientRect(WindowFromDC(dc), &rc)) return;
    const hw2vr::gl::Api& a = hw2vr::gl::api();
    hw2vr::gl::StateGuard guard;
    hw2vr::gl::StateGuard::neutral();
    a.BindFramebuffer(hw2vr::gl::DRAW_FRAMEBUFFER, 0);
    a.Enable(hw2vr::gl::SCISSOR_TEST);
    a.Scissor(kMargin, (rc.bottom - rc.top) - kMargin - kSize, kSize, kSize);
    a.ClearColor(rgb[0], rgb[1], rgb[2], 1.0f);
    a.Clear(hw2vr::gl::COLOR_BUFFER_BIT);
}

struct ProcHook {
    const char* name;
    void** real;
    void* hook;
    bool logged;
};
ProcHook g_proc_hooks[] = {
    {"glUniformMatrix4fv", reinterpret_cast<void**>(&real_glUniformMatrix4fv), reinterpret_cast<void*>(&my_glUniformMatrix4fv)},
    {"glGetUniformLocation", reinterpret_cast<void**>(&real_glGetUniformLocation), reinterpret_cast<void*>(&my_glGetUniformLocation)},
    {"glUseProgram", reinterpret_cast<void**>(&real_glUseProgram), reinterpret_cast<void*>(&my_glUseProgram)},
    {"glLinkProgram", reinterpret_cast<void**>(&real_glLinkProgram), reinterpret_cast<void*>(&my_glLinkProgram)},
    {"glDeleteProgram", reinterpret_cast<void**>(&real_glDeleteProgram), reinterpret_cast<void*>(&my_glDeleteProgram)},
    {"glBindFramebuffer", reinterpret_cast<void**>(&real_glBindFramebuffer), reinterpret_cast<void*>(&my_glBindFramebuffer)},
};

}  // namespace

extern "C" PROC WINAPI hook_wglGetProcAddress(LPCSTR name) {
    PROC proc = real_wglGetProcAddress(name);
    if (!proc || !name) return proc;
    for (ProcHook& h : g_proc_hooks) {
        if (std::strcmp(name, h.name) != 0) continue;
        *h.real = reinterpret_cast<void*>(proc);
        if (!h.logged) HW2VR_LOG("hooked %s", name);
        h.logged = true;
        return reinterpret_cast<PROC>(h.hook);
    }
    return hw2vr::record::wrap_extension(name, proc);
}

extern "C" void WINAPI hook_glDrawArrays(GLenum mode, GLint first, GLsizei count) {
    if (hw2vr::replay::g_recording) hw2vr::replay::SigOf<p_draw_arrays>::record<p_draw_arrays>(mode, first, count);
    draw_arrays(mode, first, count);
}

extern "C" void WINAPI hook_glDrawElements(GLenum mode, GLsizei count, GLenum type, const void* indices) {
    if (hw2vr::replay::g_recording) {
        if (!hw2vr::record::element_buffer_bound()) hw2vr::replay::mark_unreplayable("client-side index array");
        hw2vr::replay::SigOf<p_draw_elements>::record<p_draw_elements>(mode, count, type, indices);
    }
    draw_elements(mode, count, type, indices);
}

extern "C" __declspec(dllexport) hw2vr::hud::TestState hw2vr_test_state() { return hw2vr::hud::test_state(); }
extern "C" __declspec(dllexport) hw2vr::replay::Stats hw2vr_replay_stats() { return hw2vr::replay::stats(); }
extern "C" __declspec(dllexport) void hw2vr_test_set_cursor(int x, int y) { hw2vr::hud::set_cursor_override(x, y); }
// The panel filter on its own, for tests: the current overlay filtered to width x height (GL texture, 0 = none).
extern "C" __declspec(dllexport) unsigned hw2vr_test_build_panel(int width, int height) {
    return hw2vr::hud::build_panel_texture(width, height, 0.0f);
}
extern "C" __declspec(dllexport) int hw2vr_test_ui_at(float u, float v) { return hw2vr::hud::ui_at(u, v); }
// World markers: pretend the UI phase hook is installed and whether the screen-space UI has started.
extern "C" __declspec(dllexport) void hw2vr_test_set_ui_phase(int installed, int screen) {
    hw2vr::engine::set_test_installed(installed != 0);
    hw2vr::engine::set_screen_ui_phase(screen != 0);
}
extern "C" __declspec(dllexport) unsigned hw2vr_test_find_ui_in(const wchar_t* exe_path) {
    return hw2vr::engine::test_find_ui_in(exe_path);
}
extern "C" __declspec(dllexport) float hw2vr_test_last_marker_depth() { return hw2vr::hud::last_world_marker_depth(); }
extern "C" __declspec(dllexport) float hw2vr_test_ui_coverage(float u0, float v0, float u1, float v1) {
    const float r[4] = {u0, v0, u1, v1};
    return hw2vr::hud::ui_coverage(r);
}
extern "C" __declspec(dllexport) unsigned hw2vr_test_crop(float u0, float v0, float u1, float v1, int* w, int* h) {
    const float r[4] = {u0, v0, u1, v1};
    return hw2vr::hud::crop_texture(0, r, w, h);
}
// The VR panels' images (tablet, order wheel) as raw files {int width, int height, BGRA rows top-down} in `dir`.
extern "C" __declspec(dllexport) int hw2vr_test_render_ui(const wchar_t* dir) {
    auto save = [&](const wchar_t* name, const hw2vr::sprites::Image& img) {
        FILE* f = nullptr;
        if (_wfopen_s(&f, (std::wstring(dir) + L"\\" + name).c_str(), L"wb") || !f) return false;
        std::fwrite(&img.width, 4, 1, f);
        std::fwrite(&img.height, 4, 1, f);
        std::fwrite(img.pixels.data(), 4, img.pixels.size(), f);
        std::fclose(f);
        return true;
    };
    const std::vector<std::wstring> orders = {L"Move", L"Attack", L"Guard", L"Dock", L"Stop", L"Repair", L"Harvest", L"Special"};
    int ok = 0;
    ok += save(L"ui_tablet.raw", hw2vr::tablet::test_render(-1, false));
    ok += save(L"ui_tablet_hover.raw", hw2vr::tablet::test_render(4, true));
    ok += save(L"ui_wheel.raw", hw2vr::ui::render_wheel(512, orders, L"cancel", -1));
    ok += save(L"ui_wheel_attack.raw", hw2vr::ui::render_wheel(512, orders, L"cancel", 1));
    return ok;
}
// The marker depth grid on its own: the scene framebuffer seen with the game's camera (identity eye), then the
// view depth behind a screen rectangle. Returns -1 = empty space, -2 = no grid within 1 s, -3 = no scene seen.
extern "C" __declspec(dllexport) float hw2vr_test_anchor_depth(float x0, float y0, float x1, float y1, int width,
                                                               int height) {
    const float* proj = hw2vr::stereo::game_projection();
    if (!proj || !hw2vr::gl::load()) return -3.0f;
    float eye[16];
    hw2vr::mat4::identity(eye);
    hw2vr::depth::set_camera(eye, proj);
    const unsigned before = hw2vr::depth::test_state().grids;
    hw2vr::depth::run(width, height);
    hw2vr::gl::api().Flush();
    for (int i = 0; i < 1000 && hw2vr::depth::test_state().grids == before; ++i) {
        hw2vr::depth::poll_grid();
        if (hw2vr::depth::test_state().grids == before) Sleep(1);
    }
    if (hw2vr::depth::test_state().grids == before) return -2.0f;
    const float bounds[4] = {x0, y0, x1, y1};
    float z = 0.0f;
    return hw2vr::depth::anchor_view_depth(bounds, &z) ? z : -1.0f;
}
// The laser depth probe on its own: the scene framebuffer seen with the game's camera (identity eye), one ray in
// game view space. Returns the distance along the ray, -1 = miss, -2 = no result within 1 s, -3 = no scene seen.
extern "C" __declspec(dllexport) float hw2vr_test_depth_probe(const float origin[3], const float dir[3], int width,
                                                              int height) {
    const float* proj = hw2vr::stereo::game_projection();
    if (!proj || !hw2vr::gl::load()) return -3.0f;
    float eye[16];
    hw2vr::mat4::identity(eye);
    hw2vr::depth::clear_ray();
    hw2vr::depth::set_camera(eye, proj);
    hw2vr::depth::set_ray({origin[0], origin[1], origin[2]}, {dir[0], dir[1], dir[2]});
    hw2vr::depth::run(width, height);
    hw2vr::gl::api().Flush();
    for (int i = 0; i < 1000; ++i) {
        float t = 0.0f;
        const hw2vr::depth::Result r = hw2vr::depth::latest(&t);
        if (r == hw2vr::depth::Result::Hit) return t;
        if (r == hw2vr::depth::Result::Miss) return -1.0f;
        Sleep(1);
    }
    return -2.0f;
}

namespace {

// [debug] replay_test_offset_x: replay every frame without VR, with a view-space x offset (tests/proxy_smoke.cpp).
float g_debug_replay_offset = 0.0f;

void debug_replay_test() {
    if (hw2vr::replay::replayable()) {
        float e[16];
        hw2vr::mat4::translation(g_debug_replay_offset, 0.0f, 0.0f, e);
        hw2vr::stereo::set_debug_eye(e);
        hw2vr::proxy::replay_frame();
        hw2vr::stereo::set_debug_eye(nullptr);
    } else {
        hw2vr::replay::note_skipped();
    }
    hw2vr::replay::flush_deferred_deletes();
}

}  // namespace

extern "C" BOOL WINAPI hook_wglSwapBuffers(HDC dc) {
    hw2vr::replay::end_recording();  // the frame's GL stream is complete
    hw2vr::gl::load();               // first call only; needs the game's context, which is current here
    static bool engine_tried = false;
    if (!engine_tried) engine_tried = true, hw2vr::engine::install();  // the game's code is running (and unpacked) now
    if (RECT rc; GetClientRect(WindowFromDC(dc), &rc)) hw2vr::hud::set_target_size(rc.right, rc.bottom);
    hw2vr::hud::set_window(WindowFromDC(dc));
    hw2vr::hotkeys::log_seen_keys();
    hw2vr::stereo::on_frame();
    hw2vr::vrframe::before_swap(dc);  // captures/replays/submits the eyes in VR
    const bool debug_replay = g_debug_replay_offset != 0.0f && !hw2vr::vrframe::active();
    if (debug_replay) debug_replay_test();
    if (float rgb[3]; hw2vr::stereo::mode_indicator_color(rgb) && hw2vr::gl::load()) draw_mode_indicator(dc, rgb);
    const BOOL ok = real_wglSwapBuffers(dc);
    hw2vr::engine::frame_end();  // the next frame starts with its world-space HUD
    hw2vr::vrframe::after_swap();
    hw2vr::replay::begin_frame(hw2vr::vrframe::wants_recording() || debug_replay);
    return ok;
}

namespace hw2vr::proxy {

void replay_frame() {
    // Shadow tracking follows the state restore that replay::run() starts with.
    g_current_program = replay::snapshot_program();
    g_current = find_program(g_current_program);
    g_draw_fbo = replay::snapshot_draw_fbo();
    hud::set_replaying(true);
    replay::run();
    hud::set_replaying(false);
}

void configure_replay_test(float offset_x) { g_debug_replay_offset = offset_x; }

bool init_hooks() {
    if (!record::init()) return false;
    HMODULE gl = real_opengl32();
    real_wglGetProcAddress = reinterpret_cast<PFN_wglGetProcAddress>(GetProcAddress(gl, "wglGetProcAddress"));
    real_wglSwapBuffers = reinterpret_cast<PFN_wglSwapBuffers>(GetProcAddress(gl, "wglSwapBuffers"));
    real_glDrawArrays = reinterpret_cast<PFN_glDrawArrays>(GetProcAddress(gl, "glDrawArrays"));
    real_glDrawElements = reinterpret_cast<PFN_glDrawElements>(GetProcAddress(gl, "glDrawElements"));
    if (!real_wglGetProcAddress || !real_wglSwapBuffers || !real_glDrawArrays || !real_glDrawElements) {
        HW2VR_LOG("FATAL: a hooked export (wglGetProcAddress/wglSwapBuffers/glDrawArrays/glDrawElements) is missing");
        return false;
    }
    return true;
}

}  // namespace hw2vr::proxy
