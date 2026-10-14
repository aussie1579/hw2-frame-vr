// Hooks for the two exports we implement (wglGetProcAddress, wglSwapBuffers) and the GL 2.0+ entry points the
// game fetches through wglGetProcAddress. Everything runs on the game's render thread.
//
// Where the camera enters a shader (docs/FINDINGS.md "Camera", all programs of build 12653478):
//   A. inMatV or inMatC present           -> that uniform is the camera view          (E * V)
//   B. only inMatMV + inMatP              -> inMatMV is model-view, if inMatP is perspective (E * MV)
//   C. only inMatP                        -> vertices are already in view space (CPU-transformed, e.g. the light
//                                            glows, prog "screenCorner"), if inMatP is perspective (P * E)
// B and C programs with an ortho inMatP are 2D HUD / post-processing and stay untouched. "Perspective" comes from
// the program's most recent inMatP upload. In VR every perspective inMatP also gets the eye frustum.
// Nothing is overridden while the game draws into its HUD framebuffer: the HUD layer keeps the game's own camera
// so it matches the 2D markers the game positions on the CPU (it is shown on an overlay fixed in that camera's frame).
#include "proxy/gl_hooks.h"

#include <windows.h>

#include <cmath>
#include <cstring>
#include <type_traits>
#include <unordered_map>

#include "common/hotkeys.h"
#include "common/log.h"
#include "common/mat4.h"
#include "hud/hud_layer.h"
#include "proxy/gl_api.h"
#include "proxy/real_gl.h"
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

PFN_wglGetProcAddress real_wglGetProcAddress = nullptr;
PFN_wglSwapBuffers real_wglSwapBuffers = nullptr;
PFN_glUniformMatrix4fv real_glUniformMatrix4fv = nullptr;
PFN_glGetUniformLocation real_glGetUniformLocation = nullptr;
PFN_glUseProgram real_glUseProgram = nullptr;
PFN_glLinkProgram real_glLinkProgram = nullptr;
PFN_glDeleteProgram real_glDeleteProgram = nullptr;
PFN_glBindFramebuffer real_glBindFramebuffer = nullptr;

struct ProgramInfo {
    GLint loc_view = -1;  // inMatV
    GLint loc_cam = -1;   // inMatC
    GLint loc_mv = -1;    // inMatMV
    GLint loc_proj = -1;  // inMatP
    bool proj_is_perspective = false;  // from this program's last inMatP upload

    bool has_camera_matrix() const { return loc_view >= 0 || loc_cam >= 0; }
    bool view_space_input() const { return !has_camera_matrix() && loc_mv < 0; }  // group C
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

// Does this upload carry the camera view (groups A and B in the file comment)?
bool camera_view_source(const ProgramInfo& info, GLint loc, hw2vr::stereo::CameraUniform* source) {
    using hw2vr::stereo::CameraUniform;
    if (loc == info.loc_view) *source = CameraUniform::InMatV;
    else if (loc == info.loc_cam) *source = CameraUniform::InMatC;
    else if (loc == info.loc_mv && !info.has_camera_matrix() && info.proj_is_perspective) *source = CameraUniform::InMatMV;
    else return false;
    return true;
}

void APIENTRY my_glUseProgram(GLuint program) {
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

void APIENTRY my_glUniformMatrix4fv(GLint loc, GLsizei count, GLboolean transpose, const GLfloat* value) {
    ProgramInfo* info = g_current;
    if (info && loc >= 0 && count == 1 && !transpose && value) {
        const bool hud_layer = hw2vr::hud::is_hud_fbo(g_draw_fbo);
        const bool override_camera = hw2vr::stereo::active() && !hud_layer;
        GLfloat replaced[16];
        if (loc == info->loc_proj) {
            info->proj_is_perspective = hw2vr::mat4::is_perspective(value);
            if (!info->proj_is_perspective) {
                hw2vr::hud::note_ortho_projection(g_draw_fbo);
            } else {
                if (!hud_layer && is_main_projection(value)) hw2vr::stereo::note_game_projection(value);
                if (override_camera &&
                    hw2vr::stereo::apply_projection_override(value, replaced, info->view_space_input())) {
                    real_glUniformMatrix4fv(loc, 1, transpose, replaced);
                    return;
                }
            }
        } else if (hw2vr::stereo::CameraUniform source; override_camera && camera_view_source(*info, loc, &source)) {
            hw2vr::stereo::apply_view_override(value, replaced, source);
            real_glUniformMatrix4fv(loc, 1, transpose, replaced);
            return;
        }
    }
    real_glUniformMatrix4fv(loc, count, transpose, value);
}

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
    return proc;
}

extern "C" BOOL WINAPI hook_wglSwapBuffers(HDC dc) {
    hw2vr::hotkeys::log_seen_keys();
    hw2vr::stereo::on_frame();
    hw2vr::vrframe::before_swap(dc);
    if (float rgb[3]; hw2vr::stereo::mode_indicator_color(rgb) && hw2vr::gl::load()) draw_mode_indicator(dc, rgb);
    const BOOL ok = real_wglSwapBuffers(dc);
    hw2vr::vrframe::after_swap();
    return ok;
}

namespace hw2vr::proxy {

bool init_hooks() {
    HMODULE gl = real_opengl32();
    real_wglGetProcAddress = reinterpret_cast<PFN_wglGetProcAddress>(GetProcAddress(gl, "wglGetProcAddress"));
    real_wglSwapBuffers = reinterpret_cast<PFN_wglSwapBuffers>(GetProcAddress(gl, "wglSwapBuffers"));
    if (!real_wglGetProcAddress || !real_wglSwapBuffers) {
        HW2VR_LOG("FATAL: real wglGetProcAddress/wglSwapBuffers not found");
        return false;
    }
    return true;
}

}  // namespace hw2vr::proxy
