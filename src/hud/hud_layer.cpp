#include "hud/hud_layer.h"

#include <unordered_map>
#include <unordered_set>

#include "common/log.h"
#include "proxy/gl_api.h"

namespace hw2vr::hud {
namespace {

constexpr unsigned kMinOrthoUploads = 3;  // fewer than this in a frame: no HUD framebuffer this frame
constexpr unsigned kStableFrames = 10;    // same winner this many frames in a row before we trust it
constexpr unsigned kLoseAfterFrames = 120;

std::unordered_map<unsigned, unsigned> g_ortho_counts;  // draw fbo -> ortho inMatP uploads this frame
unsigned g_hud_fbo = 0;                                 // 0 = not identified
unsigned g_candidate = 0, g_candidate_streak = 0, g_frames_without = 0;

bool g_capture = false, g_premultiplied = true, g_captured = false;
int g_width = 0, g_height = 0;
std::unordered_set<unsigned> g_ui_programs;  // drew into the HUD framebuffer this frame
unsigned g_captures = 0, g_redirected = 0;
gl::GLint g_saved_blend[4];
gl::GLboolean g_saved_blend_on = 0;
gl::GLuint g_raw_tex = 0, g_raw_fbo = 0, g_out_tex = 0, g_out_fbo = 0, g_program = 0, g_vao = 0;
int g_tex_width = 0, g_tex_height = 0;

const char* kVertexShader = R"(#version 330
out vec2 uv;
void main() {
    vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);  // fullscreen triangle
    uv = p;
    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
})";

// The game's HUD target holds premultiplied colour with *transmittance* in alpha (cleared to alpha 1, blended with
// dstAlpha *= 1 - srcAlpha). The overlay wants coverage alpha.
const char* kFragmentShader = R"(#version 330
uniform sampler2D hud;
uniform int premultiplied;
in vec2 uv;
out vec4 color;
void main() {
    vec4 h = texture(hud, uv);
    float a = 1.0 - h.a;
    color = premultiplied != 0 ? vec4(h.rgb, a) : vec4(a > 0.0 ? h.rgb / a : vec3(0.0), a);
})";

gl::GLuint compile(gl::GLenum type, const char* source) {
    const gl::Api& a = gl::api();
    gl::GLuint s = a.CreateShader(type);
    a.ShaderSource(s, 1, &source, nullptr);
    a.CompileShader(s);
    gl::GLint ok = 0;
    a.GetShaderiv(s, gl::COMPILE_STATUS, &ok);
    if (!ok) {
        char log[512] = {};
        a.GetShaderInfoLog(s, sizeof log, nullptr, log);
        HW2VR_LOG("hud: shader compile failed: %s", log);
    }
    return s;
}

bool ensure_program() {
    if (g_program) return true;
    const gl::Api& a = gl::api();
    const gl::GLuint vs = compile(gl::VERTEX_SHADER, kVertexShader), fs = compile(gl::FRAGMENT_SHADER, kFragmentShader);
    g_program = a.CreateProgram();
    a.AttachShader(g_program, vs);
    a.AttachShader(g_program, fs);
    a.LinkProgram(g_program);
    a.DeleteShader(vs);
    a.DeleteShader(fs);
    gl::GLint linked = 0;
    a.GetProgramiv(g_program, gl::LINK_STATUS, &linked);
    if (!linked) {
        HW2VR_LOG("hud: conversion program failed to link - HUD capture disabled");
        g_capture = false;
        return false;
    }
    a.GenVertexArrays(1, &g_vao);
    return true;
}

void make_target(gl::GLuint& tex, gl::GLuint& fbo, int w, int h) {
    const gl::Api& a = gl::api();
    if (!tex) a.GenTextures(1, &tex);
    a.BindTexture(gl::TEXTURE_2D, tex);
    a.TexImage2D(gl::TEXTURE_2D, 0, gl::RGBA8, w, h, 0, gl::RGBA, gl::UNSIGNED_BYTE, nullptr);
    a.TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_MIN_FILTER, gl::LINEAR);
    a.TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_MAG_FILTER, gl::LINEAR);
    a.TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_WRAP_S, gl::CLAMP_TO_EDGE);
    a.TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_WRAP_T, gl::CLAMP_TO_EDGE);
    if (!fbo) a.GenFramebuffers(1, &fbo);
    a.BindFramebuffer(gl::DRAW_FRAMEBUFFER, fbo);
    a.FramebufferTexture2D(gl::DRAW_FRAMEBUFFER, gl::COLOR_ATTACHMENT0, gl::TEXTURE_2D, tex, 0);
    if (a.CheckFramebufferStatus(gl::DRAW_FRAMEBUFFER) != gl::FRAMEBUFFER_COMPLETE)
        HW2VR_LOG("hud: our %dx%d target is incomplete", w, h);
}

void capture(unsigned hud_fbo) {
    if (!ensure_program()) return;
    const gl::Api& a = gl::api();
    gl::StateGuard guard;
    gl::StateGuard::neutral();
    if (g_tex_width != g_width || g_tex_height != g_height) {
        make_target(g_raw_tex, g_raw_fbo, g_width, g_height);
        make_target(g_out_tex, g_out_fbo, g_width, g_height);
        g_tex_width = g_width;
        g_tex_height = g_height;
        HW2VR_LOG("hud: capture targets %dx%d", g_width, g_height);
    }
    // 1. Copy the game's HUD.
    a.BindFramebuffer(gl::READ_FRAMEBUFFER, hud_fbo);
    a.ReadBuffer(gl::COLOR_ATTACHMENT0);
    a.BindFramebuffer(gl::DRAW_FRAMEBUFFER, g_raw_fbo);
    a.BlitFramebuffer(0, 0, g_width, g_height, 0, 0, g_width, g_height, gl::COLOR_BUFFER_BIT, gl::NEAREST);
    // 2. Convert transmittance -> coverage for the overlay.
    a.BindFramebuffer(gl::DRAW_FRAMEBUFFER, g_out_fbo);
    a.Viewport(0, 0, g_width, g_height);
    a.UseProgram(g_program);
    a.Uniform1i(a.GetUniformLocation(g_program, "premultiplied"), g_premultiplied ? 1 : 0);
    a.BindVertexArray(g_vao);
    a.ActiveTexture(gl::TEXTURE0);
    a.BindTexture(gl::TEXTURE_2D, g_raw_tex);
    a.BindSampler(0, 0);
    a.DrawArrays(gl::TRIANGLES, 0, 3);
    // 3. Empty the game's HUD so the final composite (and so the eye images) carry none.
    a.BindFramebuffer(gl::DRAW_FRAMEBUFFER, hud_fbo);
    a.ClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    a.Clear(gl::COLOR_BUFFER_BIT);
    g_captured = true;
    ++g_captures;
}

}  // namespace

void note_ortho_projection(unsigned draw_fbo) { ++g_ortho_counts[draw_fbo]; }

bool is_hud_fbo(unsigned fbo) { return fbo != 0 && fbo == g_hud_fbo; }

void set_capture(bool enabled, bool premultiplied) {
    g_capture = enabled;
    g_premultiplied = premultiplied;
}

void set_target_size(int width, int height) {
    g_width = width;
    g_height = height;
}

void on_draw_framebuffer_change(unsigned from, unsigned to) {
    if (g_capture && !g_captured && is_hud_fbo(from) && to != from && g_width > 0 && g_height > 0) capture(from);
}

void end_frame() {
    unsigned winner = 0, best = 0;
    for (const auto& [fbo, count] : g_ortho_counts)
        if (fbo != 0 && count > best) winner = fbo, best = count;
    g_ortho_counts.clear();
    g_ui_programs.clear();
    g_captured = false;

    if (best < kMinOrthoUploads) winner = 0;
    if (winner == 0) {
        if (g_hud_fbo && ++g_frames_without >= kLoseAfterFrames) {
            HW2VR_LOG("hud: lost the HUD framebuffer (%u)", g_hud_fbo);
            g_hud_fbo = 0;
        }
        return;
    }
    g_frames_without = 0;
    g_candidate_streak = (winner == g_candidate) ? g_candidate_streak + 1 : 1;
    g_candidate = winner;
    if (winner != g_hud_fbo && g_candidate_streak >= kStableFrames) {
        HW2VR_LOG("hud: HUD framebuffer is %u (%u ortho uploads per frame)", winner, best);
        g_hud_fbo = winner;
    }
}

bool captured_this_frame() { return g_captured; }
unsigned overlay_texture() { return g_out_tex; }

void note_draw(unsigned draw_fbo, unsigned program) {
    if (is_hud_fbo(draw_fbo)) g_ui_programs.insert(program);
}

bool should_redirect(unsigned draw_fbo, unsigned program) {
    return g_capture && g_captured && g_premultiplied && draw_fbo == 0 && g_ui_programs.count(program) != 0;
}

void begin_redirect() {
    constexpr gl::GLenum kSrcRgb = 0x80C9, kDstRgb = 0x80C8, kSrcAlpha = 0x80CB, kDstAlpha = 0x80CA, kOne = 1,
                         kOneMinusSrcAlpha = 0x0303;
    const gl::Api& a = gl::api();
    a.GetIntegerv(kSrcRgb, &g_saved_blend[0]);
    a.GetIntegerv(kDstRgb, &g_saved_blend[1]);
    a.GetIntegerv(kSrcAlpha, &g_saved_blend[2]);
    a.GetIntegerv(kDstAlpha, &g_saved_blend[3]);
    g_saved_blend_on = a.IsEnabled(gl::BLEND);
    a.BindFramebuffer(gl::DRAW_FRAMEBUFFER, g_out_fbo);
    // The game's alpha factors write transmittance; the overlay holds coverage.
    a.BlendFuncSeparate(static_cast<gl::GLenum>(g_saved_blend[0]), static_cast<gl::GLenum>(g_saved_blend[1]), kOne,
                        kOneMinusSrcAlpha);
    ++g_redirected;
}

void end_redirect() {
    const gl::Api& a = gl::api();
    a.BlendFuncSeparate(static_cast<gl::GLenum>(g_saved_blend[0]), static_cast<gl::GLenum>(g_saved_blend[1]),
                        static_cast<gl::GLenum>(g_saved_blend[2]), static_cast<gl::GLenum>(g_saved_blend[3]));
    g_saved_blend_on ? a.Enable(gl::BLEND) : a.Disable(gl::BLEND);
    a.BindFramebuffer(gl::DRAW_FRAMEBUFFER, 0);
}

TestState test_state() { return {g_hud_fbo, g_out_tex, g_captures, g_redirected}; }

void release() {
    const gl::Api& a = gl::api();
    if (!a.DeleteTextures) return;
    gl::GLuint tex[2] = {g_raw_tex, g_out_tex}, fbo[2] = {g_raw_fbo, g_out_fbo};
    a.DeleteTextures(2, tex);
    a.DeleteFramebuffers(2, fbo);
    g_raw_tex = g_out_tex = g_raw_fbo = g_out_fbo = 0;
    g_tex_width = g_tex_height = 0;
}

}  // namespace hw2vr::hud
