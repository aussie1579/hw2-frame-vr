// Recording hooks for synchronized stereo. Scalar-argument entry points are one line each through Sig<>::hook
// (stereo/frame_replay.h); the few with pointer arguments copy their payload into the replay arena.
#include "proxy/gl_record_hooks.h"

#include <cstring>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <vector>

#include "common/log.h"
#include "proxy/real_gl.h"
#include "stereo/frame_replay.h"

using GLenum = unsigned int;
using GLuint = unsigned int;
using GLint = int;
using GLsizei = int;
using GLboolean = unsigned char;
using GLfloat = float;
using GLdouble = double;
using GLbitfield = unsigned int;
using GLsizeiptr = ptrdiff_t;
using GLintptr = ptrdiff_t;

namespace hw2vr::record {
namespace {

using replay::g_recording;
using replay::payload;
using replay::store;
template <auto& Fn>
using Sig = replay::SigOf<Fn>;

// ---- real entry points ---------------------------------------------------------------------------------------------
#define HW2VR_PFN(name, ret, ...) \
    using PFN_##name = ret(__stdcall*)(__VA_ARGS__); \
    PFN_##name real_##name = nullptr;

// GL 1.1 exports
HW2VR_PFN(glBindTexture, void, GLenum, GLuint)
HW2VR_PFN(glBlendFunc, void, GLenum, GLenum)
HW2VR_PFN(glClear, void, GLbitfield)
HW2VR_PFN(glClearColor, void, GLfloat, GLfloat, GLfloat, GLfloat)
HW2VR_PFN(glClearDepth, void, GLdouble)
HW2VR_PFN(glClearStencil, void, GLint)
HW2VR_PFN(glColorMask, void, GLboolean, GLboolean, GLboolean, GLboolean)
HW2VR_PFN(glCullFace, void, GLenum)
HW2VR_PFN(glDepthFunc, void, GLenum)
HW2VR_PFN(glDepthMask, void, GLboolean)
HW2VR_PFN(glDepthRange, void, GLdouble, GLdouble)
HW2VR_PFN(glDisable, void, GLenum)
HW2VR_PFN(glEnable, void, GLenum)
HW2VR_PFN(glDrawBuffer, void, GLenum)
HW2VR_PFN(glReadBuffer, void, GLenum)
HW2VR_PFN(glFrontFace, void, GLenum)
HW2VR_PFN(glLineWidth, void, GLfloat)
HW2VR_PFN(glPointSize, void, GLfloat)
HW2VR_PFN(glPolygonMode, void, GLenum, GLenum)
HW2VR_PFN(glPolygonOffset, void, GLfloat, GLfloat)
HW2VR_PFN(glScissor, void, GLint, GLint, GLsizei, GLsizei)
HW2VR_PFN(glViewport, void, GLint, GLint, GLsizei, GLsizei)
HW2VR_PFN(glStencilFunc, void, GLenum, GLint, GLuint)
HW2VR_PFN(glStencilMask, void, GLuint)
HW2VR_PFN(glStencilOp, void, GLenum, GLenum, GLenum)
HW2VR_PFN(glTexParameterf, void, GLenum, GLenum, GLfloat)
HW2VR_PFN(glTexParameteri, void, GLenum, GLenum, GLint)
HW2VR_PFN(glTexParameterfv, void, GLenum, GLenum, const GLfloat*)
HW2VR_PFN(glTexParameteriv, void, GLenum, GLenum, const GLint*)
HW2VR_PFN(glHint, void, GLenum, GLenum)
HW2VR_PFN(glCopyTexSubImage2D, void, GLenum, GLint, GLint, GLint, GLint, GLint, GLsizei, GLsizei)
HW2VR_PFN(glDeleteTextures, void, GLsizei, const GLuint*)

// Extensions (captured from wglGetProcAddress)
HW2VR_PFN(glBindVertexArray, void, GLuint)
HW2VR_PFN(glBindBuffer, void, GLenum, GLuint)
HW2VR_PFN(glActiveTexture, void, GLenum)
HW2VR_PFN(glUniform1i, void, GLint, GLint)
HW2VR_PFN(glUniform2i, void, GLint, GLint, GLint)
HW2VR_PFN(glUniform3i, void, GLint, GLint, GLint, GLint)
HW2VR_PFN(glUniform4i, void, GLint, GLint, GLint, GLint, GLint)
HW2VR_PFN(glUniform1f, void, GLint, GLfloat)
HW2VR_PFN(glUniform2f, void, GLint, GLfloat, GLfloat)
HW2VR_PFN(glUniform3f, void, GLint, GLfloat, GLfloat, GLfloat)
HW2VR_PFN(glUniform4f, void, GLint, GLfloat, GLfloat, GLfloat, GLfloat)
HW2VR_PFN(glUniform1fv, void, GLint, GLsizei, const GLfloat*)
HW2VR_PFN(glUniform2fv, void, GLint, GLsizei, const GLfloat*)
HW2VR_PFN(glUniform3fv, void, GLint, GLsizei, const GLfloat*)
HW2VR_PFN(glUniform4fv, void, GLint, GLsizei, const GLfloat*)
HW2VR_PFN(glUniform1iv, void, GLint, GLsizei, const GLint*)
HW2VR_PFN(glUniform2iv, void, GLint, GLsizei, const GLint*)
HW2VR_PFN(glUniform3iv, void, GLint, GLsizei, const GLint*)
HW2VR_PFN(glUniform4iv, void, GLint, GLsizei, const GLint*)
HW2VR_PFN(glUniformMatrix2fv, void, GLint, GLsizei, GLboolean, const GLfloat*)
HW2VR_PFN(glUniformMatrix3fv, void, GLint, GLsizei, GLboolean, const GLfloat*)
HW2VR_PFN(glBlendFuncSeparate, void, GLenum, GLenum, GLenum, GLenum)
HW2VR_PFN(glBlendEquationSeparate, void, GLenum, GLenum)
HW2VR_PFN(glBlendEquation, void, GLenum)
HW2VR_PFN(glBlendColor, void, GLfloat, GLfloat, GLfloat, GLfloat)
HW2VR_PFN(glFramebufferTexture2D, void, GLenum, GLenum, GLenum, GLuint, GLint)
HW2VR_PFN(glFramebufferRenderbuffer, void, GLenum, GLenum, GLenum, GLuint)
HW2VR_PFN(glBindRenderbuffer, void, GLenum, GLuint)
HW2VR_PFN(glBlitFramebuffer, void, GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLbitfield, GLenum)
HW2VR_PFN(glEnableVertexAttribArray, void, GLuint)
HW2VR_PFN(glDisableVertexAttribArray, void, GLuint)
HW2VR_PFN(glVertexAttribPointer, void, GLuint, GLint, GLenum, GLboolean, GLsizei, const void*)
HW2VR_PFN(glVertexAttribIPointer, void, GLuint, GLint, GLenum, GLsizei, const void*)
HW2VR_PFN(glVertexAttribDivisor, void, GLuint, GLuint)
HW2VR_PFN(glBindProgramPipeline, void, GLuint)
HW2VR_PFN(glBindSampler, void, GLuint, GLuint)
HW2VR_PFN(glBufferData, void, GLenum, GLsizeiptr, const void*, GLenum)
HW2VR_PFN(glBufferSubData, void, GLenum, GLintptr, GLsizeiptr, const void*)
HW2VR_PFN(glDeleteBuffers, void, GLsizei, const GLuint*)
HW2VR_PFN(glDeleteVertexArrays, void, GLsizei, const GLuint*)
HW2VR_PFN(glGenerateMipmap, void, GLenum)
HW2VR_PFN(glDrawBuffers, void, GLsizei, const GLenum*)
HW2VR_PFN(glStencilFuncSeparate, void, GLenum, GLenum, GLint, GLuint)
HW2VR_PFN(glStencilOpSeparate, void, GLenum, GLenum, GLenum, GLenum)
HW2VR_PFN(glStencilMaskSeparate, void, GLenum, GLuint)
HW2VR_PFN(glColorMaski, void, GLuint, GLboolean, GLboolean, GLboolean, GLboolean)
HW2VR_PFN(glEnablei, void, GLenum, GLuint)
HW2VR_PFN(glDisablei, void, GLenum, GLuint)
HW2VR_PFN(glClearBufferfv, void, GLenum, GLint, const GLfloat*)
HW2VR_PFN(glClearBufferfi, void, GLenum, GLint, GLfloat, GLint)
#undef HW2VR_PFN

// ---- binding tracking (always on: VAO element buffers are set up long before any recorded frame) -------------------
constexpr GLenum kArrayBuffer = 0x8892, kElementArrayBuffer = 0x8893;
GLuint g_vao = 0, g_array_buffer = 0;
std::unordered_map<GLuint, GLuint> g_vao_element_buffer;

void __stdcall my_glBindVertexArray(GLuint vao) {
    g_vao = vao;
    if (g_recording) Sig<real_glBindVertexArray>::record<real_glBindVertexArray>(vao);
    real_glBindVertexArray(vao);
}

void __stdcall my_glBindBuffer(GLenum target, GLuint buffer) {
    if (target == kArrayBuffer) g_array_buffer = buffer;
    else if (target == kElementArrayBuffer) g_vao_element_buffer[g_vao] = buffer;
    if (g_recording) Sig<real_glBindBuffer>::record<real_glBindBuffer>(target, buffer);
    real_glBindBuffer(target, buffer);
}

// ---- pointer payloads ----------------------------------------------------------------------------------------------
void replay_buffer_data(const replay::Cmd& c) {
    real_glBufferData(c.w[0], static_cast<GLsizeiptr>(c.w[1]), payload(c.w[2]), c.w[3]);
}
void __stdcall my_glBufferData(GLenum target, GLsizeiptr size, const void* data, GLenum usage) {
    if (g_recording) replay::push({&replay_buffer_data, {target, static_cast<uint32_t>(size), store(data, size), usage}});
    real_glBufferData(target, size, data, usage);
}

void replay_buffer_sub_data(const replay::Cmd& c) {
    real_glBufferSubData(c.w[0], static_cast<GLintptr>(c.w[1]), static_cast<GLsizeiptr>(c.w[2]), payload(c.w[3]));
}
void __stdcall my_glBufferSubData(GLenum target, GLintptr offset, GLsizeiptr size, const void* data) {
    if (g_recording)
        replay::push({&replay_buffer_sub_data,
                      {target, static_cast<uint32_t>(offset), static_cast<uint32_t>(size), store(data, size)}});
    real_glBufferSubData(target, offset, size, data);
}

// glUniform{1..4}{f,i}v: count * N components.
template <auto& Real, class T, int N>
struct UniformVector {
    static void replay(const replay::Cmd& c) {
        Real(static_cast<GLint>(c.w[0]), static_cast<GLsizei>(c.w[1]), static_cast<const T*>(payload(c.w[2])));
    }
    static void __stdcall hook(GLint loc, GLsizei count, const T* v) {
        if (g_recording)
            replay::push({&replay, {static_cast<uint32_t>(loc), static_cast<uint32_t>(count), store(v, sizeof(T) * N * count)}});
        Real(loc, count, v);
    }
};

template <auto& Real, int N>
struct UniformMatrix {
    static void replay(const replay::Cmd& c) {
        Real(static_cast<GLint>(c.w[0]), static_cast<GLsizei>(c.w[1]), static_cast<GLboolean>(c.w[2]),
             static_cast<const GLfloat*>(payload(c.w[3])));
    }
    static void __stdcall hook(GLint loc, GLsizei count, GLboolean transpose, const GLfloat* v) {
        if (g_recording)
            replay::push({&replay,
                          {static_cast<uint32_t>(loc), static_cast<uint32_t>(count), transpose,
                           store(v, sizeof(GLfloat) * N * N * count)}});
        Real(loc, count, transpose, v);
    }
};

// Vertex attribute pointers are buffer offsets when an array buffer is bound; client arrays can't be replayed.
void __stdcall my_glVertexAttribPointer(GLuint index, GLint size, GLenum type, GLboolean norm, GLsizei stride,
                                        const void* pointer) {
    if (g_recording) {
        if (g_array_buffer == 0) replay::mark_unreplayable("client-side vertex array");
        Sig<real_glVertexAttribPointer>::record<real_glVertexAttribPointer>(index, size, type, norm, stride, pointer);
    }
    real_glVertexAttribPointer(index, size, type, norm, stride, pointer);
}
void __stdcall my_glVertexAttribIPointer(GLuint index, GLint size, GLenum type, GLsizei stride, const void* pointer) {
    if (g_recording) {
        if (g_array_buffer == 0) replay::mark_unreplayable("client-side vertex array");
        Sig<real_glVertexAttribIPointer>::record<real_glVertexAttribIPointer>(index, size, type, stride, pointer);
    }
    real_glVertexAttribIPointer(index, size, type, stride, pointer);
}

void replay_draw_buffers(const replay::Cmd& c) {
    real_glDrawBuffers(static_cast<GLsizei>(c.w[0]), static_cast<const GLenum*>(payload(c.w[1])));
}
void __stdcall my_glDrawBuffers(GLsizei n, const GLenum* bufs) {
    if (g_recording) replay::push({&replay_draw_buffers, {static_cast<uint32_t>(n), store(bufs, sizeof(GLenum) * n)}});
    real_glDrawBuffers(n, bufs);
}

void replay_clear_buffer_fv(const replay::Cmd& c) {
    real_glClearBufferfv(c.w[0], static_cast<GLint>(c.w[1]), static_cast<const GLfloat*>(payload(c.w[2])));
}
void __stdcall my_glClearBufferfv(GLenum buffer, GLint drawbuffer, const GLfloat* value) {
    if (g_recording)
        replay::push({&replay_clear_buffer_fv,
                      {buffer, static_cast<uint32_t>(drawbuffer), store(value, sizeof(GLfloat) * (buffer == 0x1801 ? 1 : 4))}});
    real_glClearBufferfv(buffer, drawbuffer, value);
}

template <auto& Real, class T>
struct TexParameterVector {
    static void replay(const replay::Cmd& c) { Real(c.w[0], c.w[1], static_cast<const T*>(payload(c.w[2]))); }
    static void __stdcall hook(GLenum target, GLenum pname, const T* params) {
        if (g_recording) {
            // Border colour and swizzle take four values; everything else one.
            const int n = (pname == 0x1004 || pname == 0x8E46) ? 4 : 1;
            replay::push({&replay, {target, pname, store(params, sizeof(T) * n)}});
        }
        Real(target, pname, params);
    }
};

// Deletes during a recorded frame wait until after the replay.
template <auto& Real, replay::Kind K>
void __stdcall deferred_delete(GLsizei n, const GLuint* names) {
    if (K == replay::Kind::VertexArrays)
        for (GLsizei i = 0; i < n; ++i) g_vao_element_buffer.erase(names[i]);
    if (!replay::defer_delete(K, n, names)) Real(n, names);
}

// ---- export hooks (names come from opengl32.def) --------------------------------------------------------------------
}  // namespace
}  // namespace hw2vr::record

// Exports must be real functions (the .def names them), so each forwards to the template hook.
#define HW2VR_EXPORT_FN(name, params, args)                                                                       \
    extern "C" void __stdcall hook_##name params {                                                                 \
        hw2vr::replay::SigOf<hw2vr::record::real_##name>::hook<hw2vr::record::real_##name> args;                  \
    }
HW2VR_EXPORT_FN(glBindTexture, (GLenum a, GLuint b), (a, b))
HW2VR_EXPORT_FN(glBlendFunc, (GLenum a, GLenum b), (a, b))
HW2VR_EXPORT_FN(glClear, (GLbitfield a), (a))
HW2VR_EXPORT_FN(glClearColor, (GLfloat a, GLfloat b, GLfloat c, GLfloat d), (a, b, c, d))
HW2VR_EXPORT_FN(glClearDepth, (GLdouble a), (a))
HW2VR_EXPORT_FN(glClearStencil, (GLint a), (a))
HW2VR_EXPORT_FN(glColorMask, (GLboolean a, GLboolean b, GLboolean c, GLboolean d), (a, b, c, d))
HW2VR_EXPORT_FN(glCullFace, (GLenum a), (a))
HW2VR_EXPORT_FN(glDepthFunc, (GLenum a), (a))
HW2VR_EXPORT_FN(glDepthMask, (GLboolean a), (a))
HW2VR_EXPORT_FN(glDepthRange, (GLdouble a, GLdouble b), (a, b))
HW2VR_EXPORT_FN(glDisable, (GLenum a), (a))
HW2VR_EXPORT_FN(glEnable, (GLenum a), (a))
HW2VR_EXPORT_FN(glDrawBuffer, (GLenum a), (a))
HW2VR_EXPORT_FN(glReadBuffer, (GLenum a), (a))
HW2VR_EXPORT_FN(glFrontFace, (GLenum a), (a))
HW2VR_EXPORT_FN(glLineWidth, (GLfloat a), (a))
HW2VR_EXPORT_FN(glPointSize, (GLfloat a), (a))
HW2VR_EXPORT_FN(glPolygonMode, (GLenum a, GLenum b), (a, b))
HW2VR_EXPORT_FN(glPolygonOffset, (GLfloat a, GLfloat b), (a, b))
HW2VR_EXPORT_FN(glScissor, (GLint a, GLint b, GLsizei c, GLsizei d), (a, b, c, d))
HW2VR_EXPORT_FN(glViewport, (GLint a, GLint b, GLsizei c, GLsizei d), (a, b, c, d))
HW2VR_EXPORT_FN(glStencilFunc, (GLenum a, GLint b, GLuint c), (a, b, c))
HW2VR_EXPORT_FN(glStencilMask, (GLuint a), (a))
HW2VR_EXPORT_FN(glStencilOp, (GLenum a, GLenum b, GLenum c), (a, b, c))
HW2VR_EXPORT_FN(glTexParameterf, (GLenum a, GLenum b, GLfloat c), (a, b, c))
HW2VR_EXPORT_FN(glTexParameteri, (GLenum a, GLenum b, GLint c), (a, b, c))
HW2VR_EXPORT_FN(glHint, (GLenum a, GLenum b), (a, b))
HW2VR_EXPORT_FN(glCopyTexSubImage2D, (GLenum a, GLint b, GLint c, GLint d, GLint e, GLint f, GLsizei g, GLsizei h),
                (a, b, c, d, e, f, g, h))
#undef HW2VR_EXPORT_FN

extern "C" void __stdcall hook_glTexParameterfv(GLenum target, GLenum pname, const GLfloat* params) {
    hw2vr::record::TexParameterVector<hw2vr::record::real_glTexParameterfv, GLfloat>::hook(target, pname, params);
}
extern "C" void __stdcall hook_glTexParameteriv(GLenum target, GLenum pname, const GLint* params) {
    hw2vr::record::TexParameterVector<hw2vr::record::real_glTexParameteriv, GLint>::hook(target, pname, params);
}
extern "C" void __stdcall hook_glDeleteTextures(GLsizei n, const GLuint* names) {
    hw2vr::record::deferred_delete<hw2vr::record::real_glDeleteTextures, hw2vr::replay::Kind::Textures>(n, names);
}

namespace hw2vr::record {
namespace {

// ---- extension table ------------------------------------------------------------------------------------------------
struct ExtHook {
    const char* name;
    void** real;
    void* hook;
};
#define HW2VR_EXT_SCALAR(name) {#name, reinterpret_cast<void**>(&real_##name), reinterpret_cast<void*>(&Sig<real_##name>::hook<real_##name>)}
#define HW2VR_EXT(name, fn) {#name, reinterpret_cast<void**>(&real_##name), reinterpret_cast<void*>(fn)}
const ExtHook kExtHooks[] = {
    HW2VR_EXT(glBindVertexArray, &my_glBindVertexArray),
    HW2VR_EXT(glBindBuffer, &my_glBindBuffer),
    HW2VR_EXT_SCALAR(glActiveTexture),
    HW2VR_EXT_SCALAR(glUniform1i),
    HW2VR_EXT_SCALAR(glUniform2i),
    HW2VR_EXT_SCALAR(glUniform3i),
    HW2VR_EXT_SCALAR(glUniform4i),
    HW2VR_EXT_SCALAR(glUniform1f),
    HW2VR_EXT_SCALAR(glUniform2f),
    HW2VR_EXT_SCALAR(glUniform3f),
    HW2VR_EXT_SCALAR(glUniform4f),
    HW2VR_EXT(glUniform1fv, (&UniformVector<real_glUniform1fv, GLfloat, 1>::hook)),
    HW2VR_EXT(glUniform2fv, (&UniformVector<real_glUniform2fv, GLfloat, 2>::hook)),
    HW2VR_EXT(glUniform3fv, (&UniformVector<real_glUniform3fv, GLfloat, 3>::hook)),
    HW2VR_EXT(glUniform4fv, (&UniformVector<real_glUniform4fv, GLfloat, 4>::hook)),
    HW2VR_EXT(glUniform1iv, (&UniformVector<real_glUniform1iv, GLint, 1>::hook)),
    HW2VR_EXT(glUniform2iv, (&UniformVector<real_glUniform2iv, GLint, 2>::hook)),
    HW2VR_EXT(glUniform3iv, (&UniformVector<real_glUniform3iv, GLint, 3>::hook)),
    HW2VR_EXT(glUniform4iv, (&UniformVector<real_glUniform4iv, GLint, 4>::hook)),
    HW2VR_EXT(glUniformMatrix2fv, (&UniformMatrix<real_glUniformMatrix2fv, 2>::hook)),
    HW2VR_EXT(glUniformMatrix3fv, (&UniformMatrix<real_glUniformMatrix3fv, 3>::hook)),
    HW2VR_EXT_SCALAR(glBlendFuncSeparate),
    HW2VR_EXT_SCALAR(glBlendEquationSeparate),
    HW2VR_EXT_SCALAR(glBlendEquation),
    HW2VR_EXT_SCALAR(glBlendColor),
    HW2VR_EXT_SCALAR(glFramebufferTexture2D),
    HW2VR_EXT_SCALAR(glFramebufferRenderbuffer),
    HW2VR_EXT_SCALAR(glBindRenderbuffer),
    HW2VR_EXT_SCALAR(glBlitFramebuffer),
    HW2VR_EXT_SCALAR(glEnableVertexAttribArray),
    HW2VR_EXT_SCALAR(glDisableVertexAttribArray),
    HW2VR_EXT(glVertexAttribPointer, &my_glVertexAttribPointer),
    HW2VR_EXT(glVertexAttribIPointer, &my_glVertexAttribIPointer),
    HW2VR_EXT_SCALAR(glVertexAttribDivisor),
    HW2VR_EXT_SCALAR(glBindProgramPipeline),
    HW2VR_EXT_SCALAR(glBindSampler),
    HW2VR_EXT(glBufferData, &my_glBufferData),
    HW2VR_EXT(glBufferSubData, &my_glBufferSubData),
    HW2VR_EXT(glDeleteBuffers, (&deferred_delete<real_glDeleteBuffers, replay::Kind::Buffers>)),
    HW2VR_EXT(glDeleteVertexArrays, (&deferred_delete<real_glDeleteVertexArrays, replay::Kind::VertexArrays>)),
    HW2VR_EXT_SCALAR(glGenerateMipmap),
    HW2VR_EXT(glDrawBuffers, &my_glDrawBuffers),
    HW2VR_EXT_SCALAR(glStencilFuncSeparate),
    HW2VR_EXT_SCALAR(glStencilOpSeparate),
    HW2VR_EXT_SCALAR(glStencilMaskSeparate),
    HW2VR_EXT_SCALAR(glColorMaski),
    HW2VR_EXT_SCALAR(glEnablei),
    HW2VR_EXT_SCALAR(glDisablei),
    HW2VR_EXT(glClearBufferfv, &my_glClearBufferfv),
    HW2VR_EXT_SCALAR(glClearBufferfi),
};
#undef HW2VR_EXT
#undef HW2VR_EXT_SCALAR

// Setup, queries and object creation: they don't change what a replay of the frame draws.
bool is_benign_extension(const char* n) {
    static const char* const kPrefixes[] = {"glGet", "glIs", "glGen", "wgl", "glCheckFramebufferStatus",
                                            "glCreateShader", "glShaderSource", "glCompileShader", "glAttachShader",
                                            "glDetachShader", "glDeleteShader", "glCreateProgram",
                                            "glBindAttribLocation", "glBindFragDataLocation", "glValidateProgram",
                                            "glRenderbufferStorage", "glCompressedTex", "glTexImage3D",
                                            "glTexSubImage3D", "glTexStorage", "glDebugMessage", "glObjectLabel",
                                            "glPushDebugGroup", "glPopDebugGroup", "glFlush", "glFinish"};
    for (const char* p : kPrefixes)
        if (std::strncmp(n, p, std::strlen(p)) == 0) return true;
    return false;
}

// Counting thunks for unrecorded extension functions:  inc [counter]; mov [last], code; jmp [slot]  (+ slot).
constexpr long kExtensionCodeBase = 1000000;
std::vector<std::string> g_thunk_names;
std::unordered_map<std::string, uint8_t*> g_thunks;
uint8_t* g_thunk_page = nullptr;
size_t g_thunk_used = 0;
constexpr size_t kThunkSize = 32, kPageSize = 65536;

PROC counting_thunk(const char* name, PROC real) {
    auto it = g_thunks.find(name);
    uint8_t* t;
    if (it != g_thunks.end()) {
        t = it->second;
    } else {
        if (!g_thunk_page || g_thunk_used + kThunkSize > kPageSize) {
            g_thunk_page = static_cast<uint8_t*>(VirtualAlloc(nullptr, kPageSize, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
            g_thunk_used = 0;
            if (!g_thunk_page) return real;  // no thunk: the call goes unnoticed, but the game keeps working
        }
        t = g_thunk_page + g_thunk_used;
        g_thunk_used += kThunkSize;
        const long code = kExtensionCodeBase + static_cast<long>(g_thunk_names.size());
        g_thunk_names.emplace_back(name);
        g_thunks.emplace(name, t);
        auto put32 = [](uint8_t* p, uint32_t v) { std::memcpy(p, &v, 4); };
        t[0] = 0xFF; t[1] = 0x05; put32(t + 2, reinterpret_cast<uint32_t>(&g_hw2vr_unrecorded_calls));          // inc [counter]
        t[6] = 0xC7; t[7] = 0x05; put32(t + 8, reinterpret_cast<uint32_t>(&g_hw2vr_last_unrecorded)); put32(t + 12, code);  // mov [last], code
        t[16] = 0xFF; t[17] = 0x25; put32(t + 18, reinterpret_cast<uint32_t>(t + 24));                           // jmp [slot]
    }
    std::memcpy(t + 24, &real, 4);
    FlushInstructionCache(GetCurrentProcess(), t, kThunkSize);
    return reinterpret_cast<PROC>(t);
}

}  // namespace

bool init() {
    HMODULE m = proxy::real_opengl32();
    bool ok = true;
    auto get = [&](auto& fn, const char* name) {
        fn = reinterpret_cast<std::remove_reference_t<decltype(fn)>>(GetProcAddress(m, name));
        if (!fn) HW2VR_LOG("FATAL: real opengl32.dll lacks %s", name), ok = false;
    };
#define HW2VR_GET(name) get(real_##name, #name);
    HW2VR_GET(glBindTexture) HW2VR_GET(glBlendFunc) HW2VR_GET(glClear) HW2VR_GET(glClearColor) HW2VR_GET(glClearDepth)
    HW2VR_GET(glClearStencil) HW2VR_GET(glColorMask) HW2VR_GET(glCullFace) HW2VR_GET(glDepthFunc) HW2VR_GET(glDepthMask)
    HW2VR_GET(glDepthRange) HW2VR_GET(glDisable) HW2VR_GET(glEnable) HW2VR_GET(glDrawBuffer) HW2VR_GET(glReadBuffer)
    HW2VR_GET(glFrontFace) HW2VR_GET(glLineWidth) HW2VR_GET(glPointSize) HW2VR_GET(glPolygonMode)
    HW2VR_GET(glPolygonOffset) HW2VR_GET(glScissor) HW2VR_GET(glViewport) HW2VR_GET(glStencilFunc)
    HW2VR_GET(glStencilMask) HW2VR_GET(glStencilOp) HW2VR_GET(glTexParameterf) HW2VR_GET(glTexParameteri)
    HW2VR_GET(glTexParameterfv) HW2VR_GET(glTexParameteriv) HW2VR_GET(glHint) HW2VR_GET(glCopyTexSubImage2D)
    HW2VR_GET(glDeleteTextures)
#undef HW2VR_GET
    return ok;
}

PROC wrap_extension(const char* name, PROC real) {
    for (const ExtHook& h : kExtHooks)
        if (std::strcmp(name, h.name) == 0) {
            *h.real = reinterpret_cast<void*>(real);
            return reinterpret_cast<PROC>(h.hook);
        }
    if (is_benign_extension(name)) return real;
    return counting_thunk(name, real);
}

bool element_buffer_bound() {
    auto it = g_vao_element_buffer.find(g_vao);
    return it != g_vao_element_buffer.end() && it->second != 0;
}

const char* unrecorded_call_name(long code) {
    if (code >= kExtensionCodeBase) {
        const size_t i = static_cast<size_t>(code - kExtensionCodeBase);
        return i < g_thunk_names.size() ? g_thunk_names[i].c_str() : "?";
    }
    return proxy::forwarded_export_name(code);
}

}  // namespace hw2vr::record
