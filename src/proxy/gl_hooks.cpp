// Hooks for the two exports we implement (wglGetProcAddress, wglSwapBuffers) and the GL 2.0 entry points the
// game fetches through wglGetProcAddress. Everything runs on the game's render thread.
//
// Where the camera enters a shader (docs/FINDINGS.md "Camera", all 93 programs of build 12653478):
//   A. inMatV or inMatC present           -> that uniform is the camera view          (E * V)
//   B. only inMatMV + inMatP              -> inMatMV is model-view, if inMatP is perspective (E * MV)
//   C. only inMatP                        -> vertices are already in view space (CPU-transformed, e.g. the light
//                                            glows, prog "screenCorner"), if inMatP is perspective (P * E)
// B and C programs with an ortho inMatP are 2D HUD / post-processing and stay untouched. "Perspective" comes from
// the program's most recent inMatP upload.
#include "proxy/gl_hooks.h"

#include <windows.h>

#include <cmath>
#include <cstring>
#include <type_traits>
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

ProgramInfo* find_program(GLuint program) {
    auto it = g_programs.find(program);
    return it == g_programs.end() ? nullptr : &it->second;
}

void forget_program(GLuint program) {
    g_programs.erase(program);
    if (program == g_current_program) g_current = nullptr;
}

bool is_perspective(const GLfloat* m) { return std::fabs(m[11] + 1.0f) < 1e-4f && std::fabs(m[15]) < 1e-4f; }

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
            if (hw2vr::stereo::override_active() && info->proj_is_perspective && info->view_space_input()) {
                GLfloat eye_proj[16];
                hw2vr::stereo::apply_projection_override(value, eye_proj);
                real_glUniformMatrix4fv(loc, 1, transpose, eye_proj);
                return;
            }
        } else if (hw2vr::stereo::CameraUniform source;
                   hw2vr::stereo::override_active() && camera_view_source(*info, loc, &source)) {
            GLfloat eye_view[16];
            hw2vr::stereo::apply_view_override(value, eye_view, source);
            real_glUniformMatrix4fv(loc, 1, transpose, eye_view);
            return;
        }
    }
    real_glUniformMatrix4fv(loc, count, transpose, value);
}

// GL 1.1 entry points (plain opengl32 exports) for the spike's on-screen mode square.
struct Gl11 {
    void(APIENTRY* GetIntegerv)(unsigned, GLint*);
    void(APIENTRY* GetFloatv)(unsigned, GLfloat*);
    void(APIENTRY* GetBooleanv)(unsigned, GLboolean*);
    GLboolean(APIENTRY* IsEnabled)(unsigned);
    void(APIENTRY* Enable)(unsigned);
    void(APIENTRY* Disable)(unsigned);
    void(APIENTRY* Scissor)(GLint, GLint, GLsizei, GLsizei);
    void(APIENTRY* ColorMask)(GLboolean, GLboolean, GLboolean, GLboolean);
    void(APIENTRY* ClearColor)(GLfloat, GLfloat, GLfloat, GLfloat);
    void(APIENTRY* Clear)(unsigned);
} g_gl11 = {};

// Paints a 24 px square in the window's top-left corner on the default framebuffer, restoring all state it touches.
void draw_mode_indicator(HDC dc, const float rgb[3]) {
    constexpr unsigned kDrawFramebufferBinding = 0x8CA6, kScissorTest = 0x0C11, kScissorBox = 0x0C10,
                       kColorClearValue = 0x0C22, kColorWritemask = 0x0C23, kColorBufferBit = 0x4000;
    constexpr int kSize = 24, kMargin = 8;
    GLint fbo = 0;
    g_gl11.GetIntegerv(kDrawFramebufferBinding, &fbo);
    RECT rc;
    if (fbo != 0 || !GetClientRect(WindowFromDC(dc), &rc)) return;

    const GLboolean scissor_on = g_gl11.IsEnabled(kScissorTest);
    GLint box[4];
    GLfloat clear[4];
    GLboolean mask[4];
    g_gl11.GetIntegerv(kScissorBox, box);
    g_gl11.GetFloatv(kColorClearValue, clear);
    g_gl11.GetBooleanv(kColorWritemask, mask);

    g_gl11.Enable(kScissorTest);
    g_gl11.Scissor(kMargin, (rc.bottom - rc.top) - kMargin - kSize, kSize, kSize);
    g_gl11.ColorMask(1, 1, 1, 1);
    g_gl11.ClearColor(rgb[0], rgb[1], rgb[2], 1.0f);
    g_gl11.Clear(kColorBufferBit);

    g_gl11.ClearColor(clear[0], clear[1], clear[2], clear[3]);
    g_gl11.ColorMask(mask[0], mask[1], mask[2], mask[3]);
    g_gl11.Scissor(box[0], box[1], box[2], box[3]);
    if (!scissor_on) g_gl11.Disable(kScissorTest);
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
    if (float rgb[3]; hw2vr::stereo::mode_indicator_color(rgb)) draw_mode_indicator(dc, rgb);
    return real_wglSwapBuffers(dc);
}

namespace hw2vr::proxy {

bool init_hooks() {
    HMODULE gl = real_opengl32();
    auto get = [gl](auto& fn, const char* name) {
        fn = reinterpret_cast<std::remove_reference_t<decltype(fn)>>(GetProcAddress(gl, name));
        return fn != nullptr;
    };
    const bool ok = get(real_wglGetProcAddress, "wglGetProcAddress") && get(real_wglSwapBuffers, "wglSwapBuffers") &&
                    get(g_gl11.GetIntegerv, "glGetIntegerv") && get(g_gl11.GetFloatv, "glGetFloatv") &&
                    get(g_gl11.GetBooleanv, "glGetBooleanv") && get(g_gl11.IsEnabled, "glIsEnabled") &&
                    get(g_gl11.Enable, "glEnable") && get(g_gl11.Disable, "glDisable") &&
                    get(g_gl11.Scissor, "glScissor") && get(g_gl11.ColorMask, "glColorMask") &&
                    get(g_gl11.ClearColor, "glClearColor") && get(g_gl11.Clear, "glClear");
    if (!ok) HW2VR_LOG("FATAL: a wgl/GL 1.1 export the hooks need is missing from the real opengl32.dll");
    return ok;
}

}  // namespace hw2vr::proxy
