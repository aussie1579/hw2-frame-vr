#include "stereo/gl_snapshot.h"

#include <windows.h>

#include <iterator>
#include <type_traits>

#include "proxy/gl_api.h"
#include "proxy/real_gl.h"

namespace hw2vr::replay {
namespace {

using namespace hw2vr::gl;

// Entry points the snapshot needs beyond gl::Api (all real, never our hooks).
struct Extra {
    void(__stdcall* BlendFuncSeparate)(GLenum, GLenum, GLenum, GLenum);
    void(__stdcall* BlendEquationSeparate)(GLenum, GLenum);
    void(__stdcall* DepthFunc)(GLenum);
    void(__stdcall* DepthMask)(GLboolean);
    void(__stdcall* DepthRange)(double, double);
    void(__stdcall* ClearDepth)(double);
    void(__stdcall* CullFace)(GLenum);
    void(__stdcall* FrontFace)(GLenum);
    void(__stdcall* PolygonOffset)(GLfloat, GLfloat);
    void(__stdcall* PointSize)(GLfloat);
    void(__stdcall* LineWidth)(GLfloat);
    void(__stdcall* StencilFuncSeparate)(GLenum, GLenum, GLint, GLuint);
    void(__stdcall* StencilOpSeparate)(GLenum, GLenum, GLenum, GLenum);
    void(__stdcall* StencilMaskSeparate)(GLenum, GLuint);
    void(__stdcall* BindBuffer)(GLenum, GLuint);
    void(__stdcall* DrawBuffer)(GLenum);
    void(__stdcall* GetDoublev)(GLenum, double*);
} g_x = {};
bool g_loaded = false;

bool load() {
    if (g_loaded) return true;
    HMODULE m = proxy::real_opengl32();
    using PFN_wglGetProcAddress = PROC(WINAPI*)(LPCSTR);
    auto wgl = reinterpret_cast<PFN_wglGetProcAddress>(GetProcAddress(m, "wglGetProcAddress"));
    bool ok = true;
    auto exp = [&](auto& fn, const char* n) {
        fn = reinterpret_cast<std::remove_reference_t<decltype(fn)>>(GetProcAddress(m, n));
        ok = ok && fn;
    };
    auto ext = [&](auto& fn, const char* n) {
        fn = reinterpret_cast<std::remove_reference_t<decltype(fn)>>(wgl(n));
        ok = ok && fn;
    };
    ext(g_x.BlendFuncSeparate, "glBlendFuncSeparate");
    ext(g_x.BlendEquationSeparate, "glBlendEquationSeparate");
    exp(g_x.DepthFunc, "glDepthFunc");
    exp(g_x.DepthMask, "glDepthMask");
    exp(g_x.DepthRange, "glDepthRange");
    exp(g_x.ClearDepth, "glClearDepth");
    exp(g_x.CullFace, "glCullFace");
    exp(g_x.FrontFace, "glFrontFace");
    exp(g_x.PolygonOffset, "glPolygonOffset");
    exp(g_x.PointSize, "glPointSize");
    exp(g_x.LineWidth, "glLineWidth");
    ext(g_x.StencilFuncSeparate, "glStencilFuncSeparate");
    ext(g_x.StencilOpSeparate, "glStencilOpSeparate");
    ext(g_x.StencilMaskSeparate, "glStencilMaskSeparate");
    ext(g_x.BindBuffer, "glBindBuffer");
    exp(g_x.DrawBuffer, "glDrawBuffer");
    exp(g_x.GetDoublev, "glGetDoublev");
    g_loaded = ok;
    return ok;
}

constexpr int kUnits = 8;
constexpr GLenum kCaps[] = {0x0BE2 /*BLEND*/,        0x0B71 /*DEPTH_TEST*/,       0x0B44 /*CULL_FACE*/,
                            0x0C11 /*SCISSOR_TEST*/, 0x0B90 /*STENCIL_TEST*/,     0x8037 /*POLYGON_OFFSET_FILL*/,
                            0x3000 /*CLIP_DISTANCE0*/, 0x3001, 0x3002, 0x3003,    0x8642 /*PROGRAM_POINT_SIZE*/,
                            0x8DB9 /*FRAMEBUFFER_SRGB*/, 0x809D /*MULTISAMPLE*/,  0x8C89 /*RASTERIZER_DISCARD*/,
                            0x8D69 /*PRIMITIVE_RESTART_FIXED_INDEX*/, 0x864F /*DEPTH_CLAMP*/};

struct State {
    GLint draw_fbo, read_fbo, read_buffer, draw_buffer, program, vao, array_buffer, active_texture;
    GLint tex2d[kUnits], cube[kUnits];
    GLint viewport[4], scissor[4];
    GLboolean caps[std::size(kCaps)];
    GLint blend[4], blend_eq[2];
    GLint depth_func;
    GLboolean depth_mask;
    double depth_range[2], clear_depth;
    GLboolean color_mask[4];
    GLfloat clear_color[4];
    GLint cull_mode, front_face, polygon_mode[2];
    GLfloat polygon_offset[2], point_size, line_width;
    GLint stencil_front[3], stencil_back[3];      // func, ref, value mask
    GLint stencil_op_front[3], stencil_op_back[3];  // fail, zfail, zpass
    GLint stencil_wmask[2];
} g_s;
bool g_valid = false;

}  // namespace

void snapshot_frame_start() {
    g_valid = false;
    if (!load()) return;
    const Api& a = api();
    State& s = g_s;
    a.GetIntegerv(DRAW_FRAMEBUFFER_BINDING, &s.draw_fbo);
    a.GetIntegerv(READ_FRAMEBUFFER_BINDING, &s.read_fbo);
    a.GetIntegerv(READ_BUFFER, &s.read_buffer);
    a.GetIntegerv(0x0C01 /*DRAW_BUFFER*/, &s.draw_buffer);
    a.GetIntegerv(CURRENT_PROGRAM, &s.program);
    a.GetIntegerv(VERTEX_ARRAY_BINDING, &s.vao);
    a.GetIntegerv(0x8894 /*ARRAY_BUFFER_BINDING*/, &s.array_buffer);
    a.GetIntegerv(ACTIVE_TEXTURE, &s.active_texture);
    for (int u = 0; u < kUnits; ++u) {
        a.ActiveTexture(TEXTURE0 + u);
        a.GetIntegerv(TEXTURE_BINDING_2D, &s.tex2d[u]);
        a.GetIntegerv(0x8514 /*TEXTURE_BINDING_CUBE_MAP*/, &s.cube[u]);
    }
    a.ActiveTexture(static_cast<GLenum>(s.active_texture));
    a.GetIntegerv(VIEWPORT, s.viewport);
    a.GetIntegerv(SCISSOR_BOX, s.scissor);
    for (size_t i = 0; i < std::size(kCaps); ++i) s.caps[i] = a.IsEnabled(kCaps[i]);
    a.GetIntegerv(0x80C9, &s.blend[0]);  // BLEND_SRC_RGB
    a.GetIntegerv(0x80C8, &s.blend[1]);  // BLEND_DST_RGB
    a.GetIntegerv(0x80CB, &s.blend[2]);  // BLEND_SRC_ALPHA
    a.GetIntegerv(0x80CA, &s.blend[3]);  // BLEND_DST_ALPHA
    a.GetIntegerv(0x8009, &s.blend_eq[0]);  // BLEND_EQUATION_RGB
    a.GetIntegerv(0x883D, &s.blend_eq[1]);  // BLEND_EQUATION_ALPHA
    a.GetIntegerv(0x0B74, &s.depth_func);
    a.GetBooleanv(0x0B72, &s.depth_mask);
    g_x.GetDoublev(0x0B70, s.depth_range);
    g_x.GetDoublev(0x0B73, &s.clear_depth);
    a.GetBooleanv(COLOR_WRITEMASK, s.color_mask);
    a.GetFloatv(COLOR_CLEAR_VALUE, s.clear_color);
    a.GetIntegerv(0x0B45, &s.cull_mode);
    a.GetIntegerv(0x0B46, &s.front_face);
    a.GetIntegerv(POLYGON_MODE, s.polygon_mode);
    a.GetFloatv(0x8038, &s.polygon_offset[0]);  // POLYGON_OFFSET_FACTOR
    a.GetFloatv(0x2A00, &s.polygon_offset[1]);  // POLYGON_OFFSET_UNITS
    a.GetFloatv(0x0B11, &s.point_size);
    a.GetFloatv(0x0B21, &s.line_width);
    a.GetIntegerv(0x0B92, &s.stencil_front[0]);  // STENCIL_FUNC
    a.GetIntegerv(0x0B97, &s.stencil_front[1]);  // STENCIL_REF
    a.GetIntegerv(0x0B93, &s.stencil_front[2]);  // STENCIL_VALUE_MASK
    a.GetIntegerv(0x8800, &s.stencil_back[0]);
    a.GetIntegerv(0x8CA3, &s.stencil_back[1]);
    a.GetIntegerv(0x8CA4, &s.stencil_back[2]);
    a.GetIntegerv(0x0B94, &s.stencil_op_front[0]);
    a.GetIntegerv(0x0B95, &s.stencil_op_front[1]);
    a.GetIntegerv(0x0B96, &s.stencil_op_front[2]);
    a.GetIntegerv(0x8801, &s.stencil_op_back[0]);
    a.GetIntegerv(0x8802, &s.stencil_op_back[1]);
    a.GetIntegerv(0x8803, &s.stencil_op_back[2]);
    a.GetIntegerv(0x0B98, &s.stencil_wmask[0]);
    a.GetIntegerv(0x8CA5, &s.stencil_wmask[1]);
    g_valid = true;
}

void restore_frame_start() {
    if (!g_valid) return;
    const Api& a = api();
    const State& s = g_s;
    for (size_t i = 0; i < std::size(kCaps); ++i) s.caps[i] ? a.Enable(kCaps[i]) : a.Disable(kCaps[i]);
    g_x.BlendFuncSeparate(s.blend[0], s.blend[1], s.blend[2], s.blend[3]);
    g_x.BlendEquationSeparate(s.blend_eq[0], s.blend_eq[1]);
    g_x.DepthFunc(s.depth_func);
    g_x.DepthMask(s.depth_mask);
    g_x.DepthRange(s.depth_range[0], s.depth_range[1]);
    g_x.ClearDepth(s.clear_depth);
    a.ColorMask(s.color_mask[0], s.color_mask[1], s.color_mask[2], s.color_mask[3]);
    a.ClearColor(s.clear_color[0], s.clear_color[1], s.clear_color[2], s.clear_color[3]);
    g_x.CullFace(s.cull_mode);
    g_x.FrontFace(s.front_face);
    a.PolygonMode(FRONT_AND_BACK, static_cast<GLenum>(s.polygon_mode[0]));
    g_x.PolygonOffset(s.polygon_offset[0], s.polygon_offset[1]);
    g_x.PointSize(s.point_size);
    g_x.LineWidth(s.line_width);
    g_x.StencilFuncSeparate(0x0404 /*FRONT*/, s.stencil_front[0], s.stencil_front[1], s.stencil_front[2]);
    g_x.StencilFuncSeparate(0x0405 /*BACK*/, s.stencil_back[0], s.stencil_back[1], s.stencil_back[2]);
    g_x.StencilOpSeparate(0x0404, s.stencil_op_front[0], s.stencil_op_front[1], s.stencil_op_front[2]);
    g_x.StencilOpSeparate(0x0405, s.stencil_op_back[0], s.stencil_op_back[1], s.stencil_op_back[2]);
    g_x.StencilMaskSeparate(0x0404, s.stencil_wmask[0]);
    g_x.StencilMaskSeparate(0x0405, s.stencil_wmask[1]);
    a.Viewport(s.viewport[0], s.viewport[1], s.viewport[2], s.viewport[3]);
    a.Scissor(s.scissor[0], s.scissor[1], s.scissor[2], s.scissor[3]);
    for (int u = 0; u < kUnits; ++u) {
        a.ActiveTexture(TEXTURE0 + u);
        a.BindTexture(TEXTURE_2D, s.tex2d[u]);
        a.BindTexture(0x8513 /*TEXTURE_CUBE_MAP*/, s.cube[u]);
    }
    a.ActiveTexture(static_cast<GLenum>(s.active_texture));
    a.BindVertexArray(s.vao);
    g_x.BindBuffer(0x8892 /*ARRAY_BUFFER*/, s.array_buffer);
    a.UseProgram(s.program);
    a.BindFramebuffer(READ_FRAMEBUFFER, s.read_fbo);
    a.ReadBuffer(static_cast<GLenum>(s.read_buffer));
    a.BindFramebuffer(DRAW_FRAMEBUFFER, s.draw_fbo);
    // An FBO's draw buffers are its own (possibly several) state; only the default framebuffer's is global.
    if (s.draw_fbo == 0) g_x.DrawBuffer(static_cast<GLenum>(s.draw_buffer));
}

unsigned snapshot_program() { return static_cast<unsigned>(g_s.program); }
unsigned snapshot_draw_fbo() { return static_cast<unsigned>(g_s.draw_fbo); }

}  // namespace hw2vr::replay
