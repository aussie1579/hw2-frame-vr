#pragma once

// Real GL entry points for the proxy's own rendering (eye capture, HUD conversion, mode square). These bypass our
// hooks, so the game-state tracking in gl_hooks.cpp never sees our calls. GL 1.1 comes from the real opengl32.dll
// exports; everything newer from the real wglGetProcAddress, so load() needs a current context.
namespace hw2vr::gl {

using GLenum = unsigned int;
using GLuint = unsigned int;
using GLint = int;
using GLsizei = int;
using GLboolean = unsigned char;
using GLfloat = float;
using GLbitfield = unsigned int;
using GLchar = char;

#define HW2VR_APIENTRY __stdcall
struct Api {
    // GL 1.1
    void(HW2VR_APIENTRY* GetIntegerv)(GLenum, GLint*);
    void(HW2VR_APIENTRY* GetFloatv)(GLenum, GLfloat*);
    void(HW2VR_APIENTRY* GetBooleanv)(GLenum, GLboolean*);
    GLboolean(HW2VR_APIENTRY* IsEnabled)(GLenum);
    void(HW2VR_APIENTRY* Enable)(GLenum);
    void(HW2VR_APIENTRY* Disable)(GLenum);
    void(HW2VR_APIENTRY* Scissor)(GLint, GLint, GLsizei, GLsizei);
    void(HW2VR_APIENTRY* Viewport)(GLint, GLint, GLsizei, GLsizei);
    void(HW2VR_APIENTRY* ColorMask)(GLboolean, GLboolean, GLboolean, GLboolean);
    void(HW2VR_APIENTRY* ClearColor)(GLfloat, GLfloat, GLfloat, GLfloat);
    void(HW2VR_APIENTRY* Clear)(GLbitfield);
    void(HW2VR_APIENTRY* PolygonMode)(GLenum, GLenum);
    void(HW2VR_APIENTRY* ReadBuffer)(GLenum);
    void(HW2VR_APIENTRY* GenTextures)(GLsizei, GLuint*);
    void(HW2VR_APIENTRY* DeleteTextures)(GLsizei, const GLuint*);
    void(HW2VR_APIENTRY* BindTexture)(GLenum, GLuint);
    void(HW2VR_APIENTRY* TexImage2D)(GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void*);
    void(HW2VR_APIENTRY* TexParameteri)(GLenum, GLenum, GLint);
    void(HW2VR_APIENTRY* DrawArrays)(GLenum, GLint, GLsizei);
    void(HW2VR_APIENTRY* Flush)();
    GLenum(HW2VR_APIENTRY* GetError)();
    // GL 2.0+
    void(HW2VR_APIENTRY* ActiveTexture)(GLenum);
    void(HW2VR_APIENTRY* GenFramebuffers)(GLsizei, GLuint*);
    void(HW2VR_APIENTRY* DeleteFramebuffers)(GLsizei, const GLuint*);
    void(HW2VR_APIENTRY* BindFramebuffer)(GLenum, GLuint);
    void(HW2VR_APIENTRY* FramebufferTexture2D)(GLenum, GLenum, GLenum, GLuint, GLint);
    GLenum(HW2VR_APIENTRY* CheckFramebufferStatus)(GLenum);
    void(HW2VR_APIENTRY* BlitFramebuffer)(GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLbitfield, GLenum);
    void(HW2VR_APIENTRY* GenVertexArrays)(GLsizei, GLuint*);
    void(HW2VR_APIENTRY* BindVertexArray)(GLuint);
    GLuint(HW2VR_APIENTRY* CreateShader)(GLenum);
    void(HW2VR_APIENTRY* ShaderSource)(GLuint, GLsizei, const GLchar* const*, const GLint*);
    void(HW2VR_APIENTRY* CompileShader)(GLuint);
    void(HW2VR_APIENTRY* GetShaderiv)(GLuint, GLenum, GLint*);
    void(HW2VR_APIENTRY* GetShaderInfoLog)(GLuint, GLsizei, GLsizei*, GLchar*);
    void(HW2VR_APIENTRY* DeleteShader)(GLuint);
    GLuint(HW2VR_APIENTRY* CreateProgram)();
    void(HW2VR_APIENTRY* AttachShader)(GLuint, GLuint);
    void(HW2VR_APIENTRY* LinkProgram)(GLuint);
    void(HW2VR_APIENTRY* GetProgramiv)(GLuint, GLenum, GLint*);
    void(HW2VR_APIENTRY* UseProgram)(GLuint);
    GLint(HW2VR_APIENTRY* GetUniformLocation)(GLuint, const GLchar*);
    void(HW2VR_APIENTRY* Uniform1i)(GLint, GLint);
    void(HW2VR_APIENTRY* BindSampler)(GLuint, GLuint);
    void(HW2VR_APIENTRY* BlendFuncSeparate)(GLenum, GLenum, GLenum, GLenum);
};
#undef HW2VR_APIENTRY

const Api& api();
// Resolves every entry point; call with the game's context current. Returns false (and logs) if any is missing.
bool load();

// GL enums we use.
constexpr GLenum TEXTURE_2D = 0x0DE1, TEXTURE0 = 0x84C0, RGBA = 0x1908, RGBA8 = 0x8058, UNSIGNED_BYTE = 0x1401,
                 TEXTURE_MIN_FILTER = 0x2801, TEXTURE_MAG_FILTER = 0x2800, LINEAR = 0x2601, TEXTURE_WRAP_S = 0x2802,
                 TEXTURE_WRAP_T = 0x2803, CLAMP_TO_EDGE = 0x812F, FRAMEBUFFER = 0x8D40, READ_FRAMEBUFFER = 0x8CA8,
                 DRAW_FRAMEBUFFER = 0x8CA9, COLOR_ATTACHMENT0 = 0x8CE0, FRAMEBUFFER_COMPLETE = 0x8CD5,
                 COLOR_BUFFER_BIT = 0x4000, NEAREST = 0x2600, BACK = 0x0405, TRIANGLES = 0x0004,
                 VERTEX_SHADER = 0x8B31, FRAGMENT_SHADER = 0x8B30, COMPILE_STATUS = 0x8B81, LINK_STATUS = 0x8B82,
                 DRAW_FRAMEBUFFER_BINDING = 0x8CA6, READ_FRAMEBUFFER_BINDING = 0x8CAA, CURRENT_PROGRAM = 0x8B8D,
                 VERTEX_ARRAY_BINDING = 0x85B5, ACTIVE_TEXTURE = 0x84E0, TEXTURE_BINDING_2D = 0x8069,
                 SAMPLER_BINDING = 0x8919, READ_BUFFER = 0x0C02, VIEWPORT = 0x0BA2, SCISSOR_BOX = 0x0C10,
                 COLOR_WRITEMASK = 0x0C23, COLOR_CLEAR_VALUE = 0x0C22, POLYGON_MODE = 0x0B40, FRONT_AND_BACK = 0x0408,
                 FILL = 0x1B02, SCISSOR_TEST = 0x0C11, BLEND = 0x0BE2, DEPTH_TEST = 0x0B71, CULL_FACE = 0x0B44,
                 STENCIL_TEST = 0x0B90, FRAMEBUFFER_SRGB = 0x8DB9, RASTERIZER_DISCARD = 0x8C89;

// Saves every piece of GL state the proxy's own passes touch and restores it on destruction, so our work can run
// between any two game calls.
class StateGuard {
public:
    StateGuard();
    ~StateGuard();
    StateGuard(const StateGuard&) = delete;
    StateGuard& operator=(const StateGuard&) = delete;

    // Neutral state for a full-target copy or draw: no scissor/blend/depth/cull/stencil/sRGB, full colour mask.
    static void neutral();

private:
    GLint draw_fbo_, read_fbo_, read_buffer_, program_, vao_, active_texture_, texture0_, sampler0_;
    GLint viewport_[4], scissor_box_[4], polygon_mode_[2];
    GLfloat clear_color_[4];
    GLboolean color_mask_[4];
    GLboolean scissor_, blend_, depth_, cull_, stencil_, srgb_, discard_;
};

}  // namespace hw2vr::gl
