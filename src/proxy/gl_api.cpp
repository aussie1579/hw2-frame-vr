#include "proxy/gl_api.h"

#include <windows.h>

#include <initializer_list>
#include <type_traits>

#include "common/log.h"
#include "proxy/real_gl.h"

namespace hw2vr::gl {
namespace {

Api g_api = {};
bool g_loaded = false;
bool g_attempted = false;  // one attempt only, so a failure is logged once rather than every frame

}  // namespace

const Api& api() { return g_api; }

bool load() {
    if (g_attempted) return g_loaded;
    g_attempted = true;
    HMODULE module = proxy::real_opengl32();
    using PFN_wglGetProcAddress = PROC(WINAPI*)(LPCSTR);
    auto wgl_get = reinterpret_cast<PFN_wglGetProcAddress>(GetProcAddress(module, "wglGetProcAddress"));
    bool ok = true;
    auto export_fn = [&](auto& fn, const char* name) {
        fn = reinterpret_cast<std::remove_reference_t<decltype(fn)>>(GetProcAddress(module, name));
        if (!fn) HW2VR_LOG("gl: missing export %s", name), ok = false;
    };
    auto ext_fn = [&](auto& fn, const char* name) {
        fn = reinterpret_cast<std::remove_reference_t<decltype(fn)>>(wgl_get(name));
        if (!fn) HW2VR_LOG("gl: wglGetProcAddress(%s) failed", name), ok = false;
    };
    Api& a = g_api;
    export_fn(a.GetIntegerv, "glGetIntegerv");
    export_fn(a.GetFloatv, "glGetFloatv");
    export_fn(a.GetBooleanv, "glGetBooleanv");
    export_fn(a.IsEnabled, "glIsEnabled");
    export_fn(a.Enable, "glEnable");
    export_fn(a.Disable, "glDisable");
    export_fn(a.Scissor, "glScissor");
    export_fn(a.Viewport, "glViewport");
    export_fn(a.ColorMask, "glColorMask");
    export_fn(a.ClearColor, "glClearColor");
    export_fn(a.Clear, "glClear");
    export_fn(a.PolygonMode, "glPolygonMode");
    export_fn(a.ReadBuffer, "glReadBuffer");
    export_fn(a.GenTextures, "glGenTextures");
    export_fn(a.DeleteTextures, "glDeleteTextures");
    export_fn(a.BindTexture, "glBindTexture");
    export_fn(a.TexImage2D, "glTexImage2D");
    export_fn(a.TexParameteri, "glTexParameteri");
    export_fn(a.DrawArrays, "glDrawArrays");
    export_fn(a.Flush, "glFlush");
    export_fn(a.GetError, "glGetError");
    ext_fn(a.ActiveTexture, "glActiveTexture");
    ext_fn(a.GenFramebuffers, "glGenFramebuffers");
    ext_fn(a.DeleteFramebuffers, "glDeleteFramebuffers");
    ext_fn(a.BindFramebuffer, "glBindFramebuffer");
    ext_fn(a.FramebufferTexture2D, "glFramebufferTexture2D");
    ext_fn(a.CheckFramebufferStatus, "glCheckFramebufferStatus");
    ext_fn(a.BlitFramebuffer, "glBlitFramebuffer");
    ext_fn(a.GenVertexArrays, "glGenVertexArrays");
    ext_fn(a.BindVertexArray, "glBindVertexArray");
    ext_fn(a.CreateShader, "glCreateShader");
    ext_fn(a.ShaderSource, "glShaderSource");
    ext_fn(a.CompileShader, "glCompileShader");
    ext_fn(a.GetShaderiv, "glGetShaderiv");
    ext_fn(a.GetShaderInfoLog, "glGetShaderInfoLog");
    ext_fn(a.DeleteShader, "glDeleteShader");
    ext_fn(a.CreateProgram, "glCreateProgram");
    ext_fn(a.AttachShader, "glAttachShader");
    ext_fn(a.LinkProgram, "glLinkProgram");
    ext_fn(a.GetProgramiv, "glGetProgramiv");
    ext_fn(a.UseProgram, "glUseProgram");
    ext_fn(a.GetUniformLocation, "glGetUniformLocation");
    ext_fn(a.Uniform1i, "glUniform1i");
    ext_fn(a.BindSampler, "glBindSampler");
    ext_fn(a.BlendFuncSeparate, "glBlendFuncSeparate");
    g_loaded = ok;
    return ok;
}

StateGuard::StateGuard() {
    const Api& a = g_api;
    a.GetIntegerv(DRAW_FRAMEBUFFER_BINDING, &draw_fbo_);
    a.GetIntegerv(READ_FRAMEBUFFER_BINDING, &read_fbo_);
    a.GetIntegerv(READ_BUFFER, &read_buffer_);
    a.GetIntegerv(CURRENT_PROGRAM, &program_);
    a.GetIntegerv(VERTEX_ARRAY_BINDING, &vao_);
    a.GetIntegerv(ACTIVE_TEXTURE, &active_texture_);
    a.ActiveTexture(TEXTURE0);
    a.GetIntegerv(TEXTURE_BINDING_2D, &texture0_);
    a.GetIntegerv(SAMPLER_BINDING, &sampler0_);
    a.GetIntegerv(VIEWPORT, viewport_);
    a.GetIntegerv(SCISSOR_BOX, scissor_box_);
    a.GetIntegerv(POLYGON_MODE, polygon_mode_);  // compatibility profile: front and back
    a.GetFloatv(COLOR_CLEAR_VALUE, clear_color_);
    a.GetBooleanv(COLOR_WRITEMASK, color_mask_);
    scissor_ = a.IsEnabled(SCISSOR_TEST);
    blend_ = a.IsEnabled(BLEND);
    depth_ = a.IsEnabled(DEPTH_TEST);
    cull_ = a.IsEnabled(CULL_FACE);
    stencil_ = a.IsEnabled(STENCIL_TEST);
    srgb_ = a.IsEnabled(FRAMEBUFFER_SRGB);
    discard_ = a.IsEnabled(RASTERIZER_DISCARD);
}

StateGuard::~StateGuard() {
    const Api& a = g_api;
    auto set = [&](GLenum cap, GLboolean on) { on ? a.Enable(cap) : a.Disable(cap); };
    set(SCISSOR_TEST, scissor_);
    set(BLEND, blend_);
    set(DEPTH_TEST, depth_);
    set(CULL_FACE, cull_);
    set(STENCIL_TEST, stencil_);
    set(FRAMEBUFFER_SRGB, srgb_);
    set(RASTERIZER_DISCARD, discard_);
    a.ColorMask(color_mask_[0], color_mask_[1], color_mask_[2], color_mask_[3]);
    a.ClearColor(clear_color_[0], clear_color_[1], clear_color_[2], clear_color_[3]);
    a.PolygonMode(FRONT_AND_BACK, static_cast<GLenum>(polygon_mode_[0]));
    a.Scissor(scissor_box_[0], scissor_box_[1], scissor_box_[2], scissor_box_[3]);
    a.Viewport(viewport_[0], viewport_[1], viewport_[2], viewport_[3]);
    a.ActiveTexture(TEXTURE0);
    a.BindTexture(TEXTURE_2D, static_cast<GLuint>(texture0_));
    a.BindSampler(0, static_cast<GLuint>(sampler0_));
    a.ActiveTexture(static_cast<GLenum>(active_texture_));
    a.BindVertexArray(static_cast<GLuint>(vao_));
    a.UseProgram(static_cast<GLuint>(program_));
    a.BindFramebuffer(READ_FRAMEBUFFER, static_cast<GLuint>(read_fbo_));
    a.ReadBuffer(static_cast<GLenum>(read_buffer_));
    a.BindFramebuffer(DRAW_FRAMEBUFFER, static_cast<GLuint>(draw_fbo_));
}

void StateGuard::neutral() {
    const Api& a = g_api;
    for (GLenum cap : {SCISSOR_TEST, BLEND, DEPTH_TEST, CULL_FACE, STENCIL_TEST, FRAMEBUFFER_SRGB, RASTERIZER_DISCARD})
        a.Disable(cap);
    a.ColorMask(1, 1, 1, 1);
    a.PolygonMode(FRONT_AND_BACK, FILL);
}

}  // namespace hw2vr::gl
