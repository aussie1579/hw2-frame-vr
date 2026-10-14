// Hooks for the two exports we implement (wglGetProcAddress, wglSwapBuffers) and the GL 2.0 entry points the
// game fetches through wglGetProcAddress. Everything runs on the game's render thread.
//
// Camera uniforms (docs/FINDINGS.md, build 12653478): every 3D shader computes either
//   gl_Position = inMatP * inMatV * inMatM  * pos   or   gl_Position = inMatP * inMatC * inMatMV * pos
// so the camera view arrives as inMatV or inMatC. A few programs (sky layers, world-space effects) have only
// inMatMV + inMatP; there inMatMV *is* the view, which we accept only when that program's inMatP is perspective
// (2D HUD programs have the same uniforms with an ortho inMatP and must stay untouched).
#include "proxy/gl_hooks.h"

#include <windows.h>

#include <cmath>
#include <cstring>
#include <unordered_map>

#include "common/log.h"
#include "proxy/real_gl.h"
#include "stereo/view_override.h"

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

PFN_wglGetProcAddress real_wglGetProcAddress = nullptr;
PFN_wglSwapBuffers real_wglSwapBuffers = nullptr;
PFN_glUniformMatrix4fv real_glUniformMatrix4fv = nullptr;
PFN_glGetUniformLocation real_glGetUniformLocation = nullptr;
PFN_glUseProgram real_glUseProgram = nullptr;
PFN_glLinkProgram real_glLinkProgram = nullptr;
PFN_glDeleteProgram real_glDeleteProgram = nullptr;

struct ProgramInfo {
    GLint loc_view = -1;  // inMatV
    GLint loc_cam = -1;   // inMatC
    GLint loc_mv = -1;    // inMatMV
    GLint loc_proj = -1;  // inMatP
    bool proj_is_perspective = false;  // from this program's last inMatP upload
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

ProgramInfo* find_program(GLuint program) {
    auto it = g_programs.find(program);
    return it == g_programs.end() ? nullptr : &it->second;
}

void forget_program(GLuint program) {
    g_programs.erase(program);
    if (program == g_current_program) g_current = nullptr;
}

bool is_perspective(const GLfloat* m) { return std::fabs(m[11] + 1.0f) < 1e-4f && std::fabs(m[15]) < 1e-4f; }

// Does this upload carry the camera view? See the file comment for the inMatMV rule.
bool camera_view_source(const ProgramInfo& info, GLint loc, hw2vr::stereo::ViewSource* source) {
    using hw2vr::stereo::ViewSource;
    if (loc == info.loc_view) *source = ViewSource::InMatV;
    else if (loc == info.loc_cam) *source = ViewSource::InMatC;
    else if (loc == info.loc_mv && info.loc_view < 0 && info.loc_cam < 0 && info.proj_is_perspective) *source = ViewSource::InMatMV;
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
        if (loc == info->loc_proj) {
            info->proj_is_perspective = is_perspective(value);
        } else if (hw2vr::stereo::ViewSource source;
                   hw2vr::stereo::override_active() && camera_view_source(*info, loc, &source)) {
            GLfloat eye_view[16];
            hw2vr::stereo::apply_view_override(value, eye_view, source);
            real_glUniformMatrix4fv(loc, 1, transpose, eye_view);
            return;
        }
    }
    real_glUniformMatrix4fv(loc, count, transpose, value);
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
    hw2vr::stereo::on_frame();
    return real_wglSwapBuffers(dc);
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
