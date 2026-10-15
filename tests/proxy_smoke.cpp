// Smoke test for the opengl32.dll proxy, without the game. Built next to the proxy and statically importing
// opengl32, so the loader picks our DLL from the exe directory exactly like HomeworldRM.exe does. Creates the same
// GL 3.3 compatibility context as the game, then checks forwarding, the wglGetProcAddress hooks, and which
// uniforms the camera override touches. It reads the hw2vr.ini next to it (as the proxy does) to know whether the
// spike override starts on; tools/smoke-test.ps1 runs it with the override off and on.
#include <windows.h>
#include <GL/gl.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

// Exported by opengl32.dll (and hooked by the proxy) but not declared in the SDK headers.
extern "C" __declspec(dllimport) BOOL WINAPI wglSwapBuffers(HDC);

namespace {

using GLchar = char;
using PFNCREATECONTEXTATTRIBS = HGLRC(WINAPI*)(HDC, HGLRC, const int*);
using PFNCREATESHADER = GLuint(APIENTRY*)(GLenum);
using PFNSHADERSOURCE = void(APIENTRY*)(GLuint, GLsizei, const GLchar* const*, const GLint*);
using PFNCOMPILESHADER = void(APIENTRY*)(GLuint);
using PFNCREATEPROGRAM = GLuint(APIENTRY*)();
using PFNATTACHSHADER = void(APIENTRY*)(GLuint, GLuint);
using PFNLINKPROGRAM = void(APIENTRY*)(GLuint);
using PFNGETPROGRAMIV = void(APIENTRY*)(GLuint, GLenum, GLint*);
using PFNUSEPROGRAM = void(APIENTRY*)(GLuint);
using PFNGETUNIFORMLOCATION = GLint(APIENTRY*)(GLuint, const GLchar*);
using PFNUNIFORMMATRIX4FV = void(APIENTRY*)(GLint, GLsizei, GLboolean, const GLfloat*);
using PFNGETUNIFORMFV = void(APIENTRY*)(GLuint, GLint, GLfloat*);
using PFNDELETEPROGRAM = void(APIENTRY*)(GLuint);

constexpr int WGL_CONTEXT_MAJOR_VERSION_ARB = 0x2091;
constexpr int WGL_CONTEXT_MINOR_VERSION_ARB = 0x2092;
constexpr int WGL_CONTEXT_PROFILE_MASK_ARB = 0x9126;
constexpr int WGL_CONTEXT_COMPATIBILITY_PROFILE_BIT_ARB = 0x2;
constexpr GLenum GL_VERTEX_SHADER = 0x8B31;
constexpr GLenum GL_LINK_STATUS = 0x8B82;

// Matrices taken from the gameplay trace (docs/FINDINGS.md), column-major.
constexpr GLfloat kView[16] = {-0.9991f, 0.01545f, -0.03923f, 0, 0, 0.9304f, 0.3665f, 0,
                               0.04216f, 0.3662f, -0.9296f, 0, -2479, -6475, 6649, 1};
constexpr GLfloat kSkyView[16] = {-0.9991f, 0.01545f, -0.03923f, 0, 0, 0.9304f, 0.3665f, 0,
                                  0.04216f, 0.3662f, -0.9296f, 0, 0, 0, 0, 1};
constexpr GLfloat kPerspective[16] = {0.5978f, 0, 0, 0, 0, 1.428f, 0, 0, 0, 0, -1, -1, 0, 0, -6, 0};
constexpr GLfloat kOrtho[16] = {2, 0, 0, 0, 0, 2, 0, 0, 0, 0, -1, 0, -1, -1, 0, 1};
constexpr GLfloat kModel[16] = {342.2f, 0, 0, 0, 0, 342.2f, 0, 0, 0, 0, 342.2f, 0, 88, 1865, 13250, 1};

int g_failures = 0;
void check(bool ok, const char* what) {
    std::printf("[%s] %s\n", ok ? " OK " : "FAIL", what);
    if (!ok) ++g_failures;
}

HMODULE module_of(const void* p) {
    HMODULE m = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       static_cast<LPCWSTR>(p), &m);
    return m;
}

template <class T>
T proc(const char* name) {
    return reinterpret_cast<T>(wglGetProcAddress(name));
}

void multiply(const float a[16], const float b[16], float out[16]) {
    for (int c = 0; c < 4; ++c)
        for (int r = 0; r < 4; ++r) {
            float s = 0;
            for (int k = 0; k < 4; ++k) s += a[k * 4 + r] * b[c * 4 + k];
            out[c * 4 + r] = s;
        }
}

bool nearly_equal(const float a[16], const float b[16]) {
    for (int i = 0; i < 16; ++i)
        if (std::fabs(a[i] - b[i]) > 1e-3f * (1.0f + std::fabs(b[i]))) return false;
    return true;
}

// What the proxy should upload for a camera matrix in the given spike mode: Ry(yaw) * T(-offset) * view,
// rotation only when the view has no translation (skybox).
struct Expectation {
    int mode = 0;
    float yaw_degrees = 10, offset_x = 150;

    void eye(bool with_translation, float out[16]) const {
        const float a = (mode & 1) ? yaw_degrees * 3.14159265f / 180.0f : 0.0f;
        const float x = (mode & 2) && with_translation ? offset_x : 0.0f;
        const float c = std::cos(a), s = std::sin(a);
        const float rotation[16] = {c, 0, -s, 0, 0, 1, 0, 0, s, 0, c, 0, 0, 0, 0, 1};
        const float translation[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, -x, 0, 0, 1};
        multiply(rotation, translation, out);
    }
    void camera(const float in[16], float out[16]) const {
        if (mode == 0) return void(std::memcpy(out, in, 64));
        const bool sky = std::fabs(in[12]) < 1 && std::fabs(in[13]) < 1 && std::fabs(in[14]) < 1;
        float e[16];
        eye(!sky, e);
        multiply(e, in, out);
    }
    // Projection for programs whose vertices are already in view space: P * E.
    void view_space_projection(const float in[16], float out[16]) const {
        if (mode == 0) return void(std::memcpy(out, in, 64));
        float e[16];
        eye(true, e);
        multiply(in, e, out);
    }
};

std::wstring ini_next_to_exe() {
    wchar_t path[MAX_PATH];
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    std::wstring ini(path);
    return ini.substr(0, ini.find_last_of(L'\\') + 1) + L"hw2vr.ini";
}

bool hud_test_requested() { return GetPrivateProfileIntW(L"debug", L"hud_capture", 0, ini_next_to_exe().c_str()) != 0; }

Expectation read_ini_next_to_exe() {
    wchar_t path[MAX_PATH];
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    std::wstring ini(path);
    ini = ini.substr(0, ini.find_last_of(L'\\') + 1) + L"hw2vr.ini";
    Expectation e;
    wchar_t buf[32];
    GetPrivateProfileStringW(L"spike", L"start_mode", L"off", buf, 32, ini.c_str());
    const wchar_t* modes[] = {L"off", L"yaw", L"offset", L"yaw+offset"};
    for (int i = 0; i < 4; ++i)
        if (_wcsicmp(buf, modes[i]) == 0) e.mode = i;
    GetPrivateProfileStringW(L"spike", L"yaw_degrees", L"10", buf, 32, ini.c_str());
    e.yaw_degrees = std::wcstof(buf, nullptr);
    GetPrivateProfileStringW(L"spike", L"offset_x", L"150", buf, 32, ini.c_str());
    e.offset_x = std::wcstof(buf, nullptr);
    return e;
}

struct Gl {
    PFNCREATESHADER create_shader = proc<PFNCREATESHADER>("glCreateShader");
    PFNSHADERSOURCE shader_source = proc<PFNSHADERSOURCE>("glShaderSource");
    PFNCOMPILESHADER compile_shader = proc<PFNCOMPILESHADER>("glCompileShader");
    PFNCREATEPROGRAM create_program = proc<PFNCREATEPROGRAM>("glCreateProgram");
    PFNATTACHSHADER attach_shader = proc<PFNATTACHSHADER>("glAttachShader");
    PFNLINKPROGRAM link_program = proc<PFNLINKPROGRAM>("glLinkProgram");
    PFNGETPROGRAMIV get_programiv = proc<PFNGETPROGRAMIV>("glGetProgramiv");
    PFNUSEPROGRAM use_program = proc<PFNUSEPROGRAM>("glUseProgram");
    PFNGETUNIFORMLOCATION get_uniform_location = proc<PFNGETUNIFORMLOCATION>("glGetUniformLocation");
    PFNUNIFORMMATRIX4FV uniform_matrix4fv = proc<PFNUNIFORMMATRIX4FV>("glUniformMatrix4fv");
    PFNGETUNIFORMFV get_uniformfv = proc<PFNGETUNIFORMFV>("glGetUniformfv");
    PFNDELETEPROGRAM delete_program = proc<PFNDELETEPROGRAM>("glDeleteProgram");

    GLuint build(const char* body) const {
        const std::string src = std::string("#version 330 compatibility\n"
                                            "uniform mat4 inMatP, inMatV, inMatM, inMatC, inMatMV;\n"
                                            "in vec3 inPos;\n") + body;
        const char* text = src.c_str();
        GLuint shader = create_shader(GL_VERTEX_SHADER);
        shader_source(shader, 1, &text, nullptr);
        compile_shader(shader);
        GLuint program = create_program();
        attach_shader(program, shader);
        link_program(program);
        GLint linked = 0;
        get_programiv(program, GL_LINK_STATUS, &linked);
        return linked ? program : 0;
    }

    // Upload through the hooked path, then read back what the driver actually received.
    bool upload_reads_back(GLuint program, const char* name, const float value[16], const float expected[16]) const {
        const GLint loc = get_uniform_location(program, name);
        if (loc < 0) return false;
        uniform_matrix4fv(loc, 1, GL_FALSE, value);
        GLfloat back[16] = {};
        get_uniformfv(program, loc, back);
        return nearly_equal(back, expected);
    }
    // Same, with an (empty) draw in between: group B's inMatMV is only decided at draw time.
    bool upload_draw_reads_back(GLuint program, const char* name, const float value[16], const float expected[16]) const {
        const GLint loc = get_uniform_location(program, name);
        if (loc < 0) return false;
        uniform_matrix4fv(loc, 1, GL_FALSE, value);
        return draw_reads_back(program, name, expected);
    }
    bool draw_reads_back(GLuint program, const char* name, const float expected[16]) const {
        glDrawArrays(GL_POINTS, 0, 0);  // through the proxy's draw hook
        GLfloat back[16] = {};
        get_uniformfv(program, get_uniform_location(program, name), back);
        return nearly_equal(back, expected);
    }
};

// --- HUD pipeline ([debug] hud_capture = 1): capture, transmittance -> coverage, clear, cursor redirect ----------
struct HudTestState {
    unsigned hud_fbo, overlay_texture, overlay_frames, redirected_draws, cursor_texture, cursor_draws;  // hud::TestState
    unsigned world_draws, world_anchored;
};
using PFNTESTSTATE = HudTestState (*)();
using PFNTESTSETCURSOR = void (*)(int, int);
using PFNTESTBUILDPANEL = unsigned (*)(int, int);
using PFNTESTUIAT = int (*)(float, float);
using PFNBINDFRAMEBUFFER = void(APIENTRY*)(GLenum, GLuint);
using PFNGENFRAMEBUFFERS = void(APIENTRY*)(GLsizei, GLuint*);
using PFNFRAMEBUFFERTEXTURE2D = void(APIENTRY*)(GLenum, GLenum, GLenum, GLuint, GLint);
using PFNGENVERTEXARRAYS = void(APIENTRY*)(GLsizei, GLuint*);
using PFNBINDVERTEXARRAY = void(APIENTRY*)(GLuint);
using PFNGENBUFFERS = void(APIENTRY*)(GLsizei, GLuint*);
using PFNBINDBUFFER = void(APIENTRY*)(GLenum, GLuint);
using PFNBUFFERDATA = void(APIENTRY*)(GLenum, ptrdiff_t, const void*, GLenum);
using PFNVERTEXATTRIBPOINTER = void(APIENTRY*)(GLuint, GLint, GLenum, GLboolean, GLsizei, const void*);
using PFNENABLEVERTEXATTRIBARRAY = void(APIENTRY*)(GLuint);
using PFNUNIFORM4FV = void(APIENTRY*)(GLint, GLsizei, const GLfloat*);
using PFNBLENDFUNCSEPARATE = void(APIENTRY*)(GLenum, GLenum, GLenum, GLenum);
constexpr GLenum GL_FRAMEBUFFER = 0x8D40, GL_COLOR_ATTACHMENT0 = 0x8CE0, GL_ARRAY_BUFFER = 0x8892,
                 GL_STATIC_DRAW = 0x88E4, GL_FRAGMENT_SHADER = 0x8B30, GL_RGBA8_ = 0x8058;
std::wstring ini_next_to_exe();

GLuint build_program(const Gl& gl, const char* vs, const char* fs) {
    auto attach = [&](GLuint program, GLenum type, const char* text) {
        GLuint s = gl.create_shader(type);
        gl.shader_source(s, 1, &text, nullptr);
        gl.compile_shader(s);
        gl.attach_shader(program, s);
    };
    GLuint program = gl.create_program();
    attach(program, GL_VERTEX_SHADER, vs);
    attach(program, GL_FRAGMENT_SHADER, fs);
    gl.link_program(program);
    GLint linked = 0;
    gl.get_programiv(program, GL_LINK_STATUS, &linked);
    return linked ? program : 0;
}

bool pixel_near(const unsigned char* p, int r, int g, int b, int a) {
    return std::abs(p[0] - r) <= 3 && std::abs(p[1] - g) <= 3 && std::abs(p[2] - b) <= 3 && std::abs(p[3] - a) <= 3;
}

// Plays the game's HUD pattern for 16 frames, screen split in thirds: into the HUD FBO (cleared to (0,0,0,1),
// transmittance alpha blend, 3 ortho inMatP uploads) a 2D element (left third) and a world-anchored 3D element with a
// perspective inMatP (middle third); then into the default framebuffer a "composite" by a non-UI program and a
// "cursor" by the UI program (right third). Checks what the proxy made of it.
void hud_pipeline_test(const Gl& gl, HDC dc, HMODULE proxy) {
    RECT rc;
    GetClientRect(WindowFromDC(dc), &rc);
    const int w = rc.right, h = rc.bottom;
    auto bind_fbo = proc<PFNBINDFRAMEBUFFER>("glBindFramebuffer");  // the hooked one, as the game gets it
    auto blend_separate = proc<PFNBLENDFUNCSEPARATE>("glBlendFuncSeparate");
    auto uniform4fv = proc<PFNUNIFORM4FV>("glUniform4fv");

    GLuint hud_tex, hud_fbo, vao, vbo;
    glGenTextures(1, &hud_tex);
    glBindTexture(GL_TEXTURE_2D, hud_tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8_, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    proc<PFNGENFRAMEBUFFERS>("glGenFramebuffers")(1, &hud_fbo);
    bind_fbo(GL_FRAMEBUFFER, hud_fbo);
    proc<PFNFRAMEBUFFERTEXTURE2D>("glFramebufferTexture2D")(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, hud_tex, 0);
    bind_fbo(GL_FRAMEBUFFER, 0);

    auto quad = [](float x0, float x1, float* out) {
        const float v[12] = {x0, 0, x1, 0, x1, 1, x0, 0, x1, 1, x0, 1};
        std::memcpy(out, v, sizeof v);
    };
    float quads[36];
    quad(0.0f, 1.0f / 3, quads);        // 2D HUD element
    quad(1.0f / 3, 2.0f / 3, quads + 12);  // 3D overlay element
    quad(2.0f / 3, 1.0f, quads + 24);   // "cursor"
    proc<PFNGENVERTEXARRAYS>("glGenVertexArrays")(1, &vao);
    proc<PFNBINDVERTEXARRAY>("glBindVertexArray")(vao);
    proc<PFNGENBUFFERS>("glGenBuffers")(1, &vbo);
    proc<PFNBINDBUFFER>("glBindBuffer")(GL_ARRAY_BUFFER, vbo);
    proc<PFNBUFFERDATA>("glBufferData")(GL_ARRAY_BUFFER, sizeof quads, quads, GL_STATIC_DRAW);
    proc<PFNVERTEXATTRIBPOINTER>("glVertexAttribPointer")(0, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
    proc<PFNENABLEVERTEXATTRIBARRAY>("glEnableVertexAttribArray")(0);

    const char* vs = "#version 330 compatibility\nlayout(location = 0) in vec2 inPos;\nuniform mat4 inMatP, inMatMV;\n"
                     "void main() { gl_Position = inMatP * inMatMV * vec4(inPos, 0.0, 1.0); }\n";
    const char* fs = "#version 330\nuniform vec4 inTint;\nout vec4 color;\nvoid main() { color = inTint; }\n";
    const GLuint ui = build_program(gl, vs, fs), world = build_program(gl, vs, fs), other = build_program(gl, vs, fs);
    check(ui && world && other, "HUD test programs linked");
    const float identity[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    // World element: view space x in [-1/3, 1/3], y in [-1, 1] at z = -1, through a 90-degree perspective.
    const float world_mv[16] = {2, 0, 0, 0, 0, 2, 0, 0, 0, 0, 1, 0, -1, -1, -1, 1};
    const float perspective[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, -1, -1, 0, 0, -0.2f, 0};
    const float red[4] = {0.5f, 0, 0, 0.5f}, green[4] = {0, 0.5f, 0, 0.5f};  // -> rgb 0.25, alpha 0.5
    auto setup = [&](GLuint program, const float* p, int p_uploads, const float* mv, const float* tint) {
        gl.use_program(program);
        for (int i = 0; i < p_uploads; ++i) gl.uniform_matrix4fv(gl.get_uniform_location(program, "inMatP"), 1, GL_FALSE, p);
        gl.uniform_matrix4fv(gl.get_uniform_location(program, "inMatMV"), 1, GL_FALSE, mv);
        uniform4fv(gl.get_uniform_location(program, "inTint"), 1, tint);
    };

    // The cursor texture is centred on the cursor's hotspot: pretend it is at the left edge of the "cursor" quad, at
    // the top of the window, so the quad fills only the texture's right half below the centre.
    auto set_cursor = reinterpret_cast<PFNTESTSETCURSOR>(GetProcAddress(proxy, "hw2vr_test_set_cursor"));
    check(set_cursor != nullptr, "proxy exports hw2vr_test_set_cursor");
    if (set_cursor) set_cursor(2 * w / 3, 0);

    glEnable(GL_BLEND);
    for (int frame = 0; frame < 16; ++frame) {
        bind_fbo(GL_FRAMEBUFFER, hud_fbo);
        glViewport(0, 0, w, h);
        glClearColor(0, 0, 0, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        blend_separate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ZERO, GL_ONE_MINUS_SRC_ALPHA);  // the game's HUD blend
        setup(ui, kOrtho, 3, identity, red);
        glDrawArrays(GL_TRIANGLES, 0, 6);    // 2D element: must go onto the overlay
        setup(world, perspective, 1, world_mv, green);
        glDrawArrays(GL_TRIANGLES, 6, 6);    // 3D overlay element: must stay in the game's HUD layer (eye images)
        bind_fbo(GL_FRAMEBUFFER, 0);
        setup(other, kOrtho, 1, identity, red);
        glDrawArrays(GL_TRIANGLES, 0, 6);    // "composite" by a non-UI program: must stay on screen
        setup(ui, kOrtho, 1, identity, red);
        glDrawArrays(GL_TRIANGLES, 12, 6);   // "cursor" by the UI program: must go into the cursor texture
        wglSwapBuffers(dc);
    }

    auto state_fn = reinterpret_cast<PFNTESTSTATE>(GetProcAddress(proxy, "hw2vr_test_state"));
    const HudTestState s = state_fn ? state_fn() : HudTestState{};
    std::printf("HUD state: hud_fbo %u (ours %u), overlay tex %u, overlay frames %u, redirected draws %u, cursor draws %u\n",
                s.hud_fbo, hud_fbo, s.overlay_texture, s.overlay_frames, s.redirected_draws, s.cursor_draws);
    check(s.hud_fbo == hud_fbo, "HUD framebuffer identified from its ortho uploads");
    check(s.overlay_frames >= 3 && s.redirected_draws == s.overlay_frames && s.cursor_draws == s.overlay_frames,
          "per frame: the 2D element to the overlay, the cursor to the cursor texture, the 3D element stays");

    std::vector<unsigned char> px(static_cast<size_t>(w) * h * 4);
    auto at = [&](int third) { return &px[(static_cast<size_t>(h / 2) * w + (2 * third + 1) * w / 6) * 4]; };
    auto print = [](const char* what, const unsigned char* p) { std::printf("  %-26s %3d,%3d,%3d,%3d\n", what, p[0], p[1], p[2], p[3]); };
    glBindTexture(GL_TEXTURE_2D, s.overlay_texture);
    glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
    print("overlay  2D element:", at(0));
    print("overlay  3D element:", at(1));
    print("overlay  cursor:", at(2));
    check(pixel_near(at(0), 64, 0, 0, 128), "overlay: 2D element, premultiplied colour + coverage alpha");
    check(pixel_near(at(1), 0, 0, 0, 0), "overlay: no 3D element (it belongs in the eye images)");
    check(pixel_near(at(2), 0, 0, 0, 0), "overlay: no cursor (it has its own texture)");
    glBindTexture(GL_TEXTURE_2D, hud_tex);
    glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
    print("game HUD 2D element:", at(0));
    print("game HUD 3D element:", at(1));
    check(pixel_near(at(0), 0, 0, 0, 255), "game's HUD layer: no 2D element (it went to the overlay)");
    check(pixel_near(at(1), 0, 64, 0, 128), "game's HUD layer: 3D element kept, game's own alpha convention");

    // Cursor texture (128 x 128, rows bottom-up): hotspot in the centre, the quad right of it and below the window top.
    std::vector<unsigned char> cur(128 * 128 * 4);
    glBindTexture(GL_TEXTURE_2D, s.cursor_texture);
    glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, cur.data());
    auto cur_at = [&](int x, int row) { return &cur[(static_cast<size_t>(row) * 128 + x) * 4]; };
    const int right = 64 + std::min(w - 2 * w / 3, 64) / 2, below = 64 - std::min(h, 64) / 2;  // inside the quad
    print("cursor right, below:", cur_at(right, below));
    print("cursor left, below:", cur_at(32, below));
    print("cursor right, above top:", cur_at(right, 96));
    check(s.cursor_texture && pixel_near(cur_at(right, below), 64, 0, 0, 128) && pixel_near(cur_at(32, below), 0, 0, 0, 0) &&
              pixel_near(cur_at(right, 96), 0, 0, 0, 0),
          "cursor texture: drawn round the hotspot (premultiplied colour + coverage alpha)");

    // Panel filter + UI mask: the overlay filtered to a quarter size keeps flat areas exact; the mask (one frame
    // late, so built twice) marks where the UI is.
    auto build_panel = reinterpret_cast<PFNTESTBUILDPANEL>(GetProcAddress(proxy, "hw2vr_test_build_panel"));
    auto ui_at = reinterpret_cast<PFNTESTUIAT>(GetProcAddress(proxy, "hw2vr_test_ui_at"));
    check(build_panel && ui_at, "proxy exports the panel test hooks");
    if (build_panel && ui_at) {
        const int pw = w / 4, ph = h / 4;
        build_panel(pw, ph);
        const unsigned panel = build_panel(pw, ph);
        std::vector<unsigned char> pan(static_cast<size_t>(pw) * ph * 4);
        glBindTexture(GL_TEXTURE_2D, panel);
        glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, pan.data());
        auto pan_at = [&](int third) { return &pan[(static_cast<size_t>(ph / 2) * pw + (2 * third + 1) * pw / 6) * 4]; };
        print("panel 2D element:", pan_at(0));
        print("panel 3D element:", pan_at(1));
        check(panel && pixel_near(pan_at(0), 64, 0, 0, 128) && pixel_near(pan_at(1), 0, 0, 0, 0),
              "panel texture: the overlay filtered down to a quarter, flat areas unchanged");
        std::printf("  UI mask at the 2D element %d, at the 3D element %d, at the cursor %d\n", ui_at(1.0f / 6, 0.5f),
                    ui_at(0.5f, 0.5f), ui_at(5.0f / 6, 0.5f));
        check(ui_at(1.0f / 6, 0.5f) == 1 && ui_at(0.5f, 0.5f) == 0 && ui_at(5.0f / 6, 0.5f) == 0,
              "UI mask: set over the 2D element only (the laser points through the rest)");

        // Game windows: how much of a region the UI covers (is the build window open?) and a crop of it at native
        // resolution (what the VR window shows).
        using PFNCOVERAGE = float (*)(float, float, float, float);
        using PFNCROP = unsigned (*)(float, float, float, float, int*, int*);
        auto coverage = reinterpret_cast<PFNCOVERAGE>(GetProcAddress(proxy, "hw2vr_test_ui_coverage"));
        auto crop = reinterpret_cast<PFNCROP>(GetProcAddress(proxy, "hw2vr_test_crop"));
        if (coverage && crop) {
            const float left = coverage(0.0f, 0.0f, 0.3f, 1.0f), middle = coverage(0.4f, 0.0f, 0.6f, 1.0f);
            std::printf("  UI coverage: left third %.2f, middle %.2f\n", left, middle);
            check(left > 0.9f && middle < 0.05f, "UI coverage: the 2D element's region full, the 3D element's empty");
            int cw = 0, ch = 0;
            const unsigned tex = crop(0.0f, 0.0f, 1.0f / 3, 1.0f, &cw, &ch);
            std::vector<unsigned char> cp(static_cast<size_t>(std::max(cw, 1)) * std::max(ch, 1) * 4);
            glBindTexture(GL_TEXTURE_2D, tex);
            glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, cp.data());
            const unsigned char* mid = &cp[(static_cast<size_t>(ch / 2) * cw + cw / 2) * 4];
            std::printf("  crop of the left third: %dx%d, middle %d,%d,%d,%d\n", cw, ch, mid[0], mid[1], mid[2], mid[3]);
            check(tex && cw == w / 3 && ch == h && pixel_near(mid, 64, 0, 0, 128),
                  "crop: a screen region at native resolution, the 2D element in it");
        } else {
            check(false, "proxy exports the coverage / crop test hooks");
        }
    }
    if (set_cursor) set_cursor(-1, -1);
    check(glGetError() == GL_NO_ERROR, "no GL errors in the HUD pipeline");
}

// --- Laser depth probe: the game's multisampled scene depth, resolved and ray-marched ----------------------------
// A 4x MSAA scene framebuffer (colour + 32-bit depth renderbuffers, like the game's 16x fbo 1) gets a quad at view
// depth 200 through a program with the game's main projection (infinite far plane), uploaded through the hooks so
// the proxy learns the scene framebuffer. Rays in game view space must touch the quad at the right distance.
using PFNDEPTHPROBE = float (*)(const float*, const float*, int, int);
using PFNGENRENDERBUFFERS = void(APIENTRY*)(GLsizei, GLuint*);
using PFNBINDRENDERBUFFER = void(APIENTRY*)(GLenum, GLuint);
using PFNRENDERBUFFERSTORAGEMS = void(APIENTRY*)(GLenum, GLsizei, GLenum, GLsizei, GLsizei);
using PFNFRAMEBUFFERRENDERBUFFER = void(APIENTRY*)(GLenum, GLenum, GLenum, GLuint);
using PFNDRAWBUFFERS = void(APIENTRY*)(GLsizei, const GLenum*);

void depth_probe_test(const Gl& gl, HMODULE proxy) {
    constexpr GLenum kRenderbuffer = 0x8D41, kDepthAttachment = 0x8D00, kDepth32 = 0x81A7, kRgba8 = 0x8058;
    constexpr int kW = 256, kH = 128;
    auto probe = reinterpret_cast<PFNDEPTHPROBE>(GetProcAddress(proxy, "hw2vr_test_depth_probe"));
    check(probe != nullptr, "proxy exports hw2vr_test_depth_probe");
    if (!probe) return;
    auto bind_fbo = proc<PFNBINDFRAMEBUFFER>("glBindFramebuffer");
    auto gen_rb = proc<PFNGENRENDERBUFFERS>("glGenRenderbuffers");
    auto bind_rb = proc<PFNBINDRENDERBUFFER>("glBindRenderbuffer");
    auto storage = proc<PFNRENDERBUFFERSTORAGEMS>("glRenderbufferStorageMultisample");
    auto attach_rb = proc<PFNFRAMEBUFFERRENDERBUFFER>("glFramebufferRenderbuffer");
    GLuint rb[2], fbo;
    gen_rb(2, rb);
    bind_rb(kRenderbuffer, rb[0]);
    storage(kRenderbuffer, 4, kRgba8, kW, kH);
    bind_rb(kRenderbuffer, rb[1]);
    storage(kRenderbuffer, 4, kDepth32, kW, kH);
    proc<PFNGENFRAMEBUFFERS>("glGenFramebuffers")(1, &fbo);
    bind_fbo(GL_FRAMEBUFFER, fbo);
    attach_rb(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, kRenderbuffer, rb[0]);
    attach_rb(GL_FRAMEBUFFER, kDepthAttachment, kRenderbuffer, rb[1]);

    // Quad: view space x, y in [-50, 50] at z = -200; 90-degree perspective (2:1 aspect), near 3, infinite far.
    const char* vs = "#version 330 compatibility\nlayout(location = 0) in vec2 inPos;\nuniform mat4 inMatP, inMatMV;\n"
                     "void main() { gl_Position = inMatP * inMatMV * vec4(inPos * 50.0, -200.0, 1.0); }\n";
    const char* fs = "#version 330\nout vec4 color;\nvoid main() { color = vec4(1.0); }\n";
    const GLuint program = build_program(gl, vs, fs);
    const float proj[16] = {0.5f, 0, 0, 0, 0, 1, 0, 0, 0, 0, -1, -1, 0, 0, -6, 0};
    const float identity[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    gl.use_program(program);
    gl.uniform_matrix4fv(gl.get_uniform_location(program, "inMatP"), 1, GL_FALSE, proj);
    gl.uniform_matrix4fv(gl.get_uniform_location(program, "inMatMV"), 1, GL_FALSE, identity);
    const float quad[12] = {-1, -1, 1, -1, 1, 1, -1, -1, 1, 1, -1, 1};
    GLuint vao, vbo;
    proc<PFNGENVERTEXARRAYS>("glGenVertexArrays")(1, &vao);
    proc<PFNBINDVERTEXARRAY>("glBindVertexArray")(vao);
    proc<PFNGENBUFFERS>("glGenBuffers")(1, &vbo);
    proc<PFNBINDBUFFER>("glBindBuffer")(GL_ARRAY_BUFFER, vbo);
    proc<PFNBUFFERDATA>("glBufferData")(GL_ARRAY_BUFFER, sizeof quad, quad, GL_STATIC_DRAW);
    proc<PFNVERTEXATTRIBPOINTER>("glVertexAttribPointer")(0, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
    proc<PFNENABLEVERTEXATTRIBARRAY>("glEnableVertexAttribArray")(0);
    glViewport(0, 0, kW, kH);
    glDisable(GL_BLEND);
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glClearDepth(1.0);
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glDisable(GL_DEPTH_TEST);
    bind_fbo(GL_FRAMEBUFFER, 0);

    struct Case {
        float origin[3], dir[3], expected;  // expected < 0: miss
        const char* what;
    };
    const float s = 1.0f / std::sqrt(1.0f + 0.0625f);
    const Case cases[] = {
        {{0, 0, 0}, {0, 0, -1}, 200.0f, "ray straight ahead touches the quad at 200"},
        {{0, -40, 0}, {0, 0.25f * s, -s}, 200.0f / s, "ray from below, angled up, touches it along its own length"},
        {{30, 20, -100}, {0, 0, -1}, 100.0f, "ray starting part-way (a hand in front of the camera) measures from its origin"},
        {{0, 0, 0}, {1, 0, -1}, -1.0f, "ray past the quad's edge (45 degrees right) misses"},
        {{0, 0, -300}, {0, 0, -1}, -1.0f, "ray starting behind the quad misses (nothing further away)"},
    };
    for (const Case& c : cases) {
        float dir[3] = {c.dir[0], c.dir[1], c.dir[2]};
        const float n = std::sqrt(dir[0] * dir[0] + dir[1] * dir[1] + dir[2] * dir[2]);
        for (float& v : dir) v /= n;
        const float t = probe(c.origin, dir, kW, kH);
        std::printf("  depth probe: %-75s -> %.2f\n", c.what, t);
        const bool ok = c.expected < 0.0f ? t == -1.0f : std::fabs(t - c.expected) <= 0.01f * c.expected;
        check(ok, c.what);
    }
    check(glGetError() == GL_NO_ERROR, "no GL errors in the depth probe test");

    // Marker depth grid: the same scene seen through the game camera, one ray per 20-pixel cell. A marker over the
    // quad (screen centre) gets its view depth 200; one over empty space gets nothing.
    using PFNANCHOR = float (*)(float, float, float, float, int, int);
    auto anchor = reinterpret_cast<PFNANCHOR>(GetProcAddress(proxy, "hw2vr_test_anchor_depth"));
    check(anchor != nullptr, "proxy exports hw2vr_test_anchor_depth");
    if (anchor) {
        const float centre = anchor(0.45f, 0.4f, 0.55f, 0.6f, kW, kH), corner = anchor(0.0f, 0.0f, 0.08f, 0.1f, kW, kH);
        // A ring round the quad: its middle half is the quad.
        const float ring = anchor(0.3f, 0.2f, 0.7f, 0.8f, kW, kH);
        std::printf("  marker grid: centre %.2f, ring round it %.2f, corner %.2f\n", centre, ring, corner);
        check(std::fabs(centre - 200.0f) < 2.0f && std::fabs(ring - 200.0f) < 2.0f,
              "marker grid: markers over / round the quad get its view depth (200)");
        check(corner == -1.0f, "marker grid: a marker over empty space gets nothing");
    }
}

// --- World markers: 2D HUD draws before the game's PHASE_UIIn stay in the game's HUD layer at their ship's depth;
// the ones after it go onto the VR overlay. Uses the HUD framebuffer identified by the HUD pipeline test and the
// depth scene of the depth probe test (quad at view depth 200 in the middle of the screen).
using PFNSETUIPHASE = void (*)(int, int);
using PFNMARKERDEPTH = float (*)();
void world_marker_test(const Gl& gl, HDC dc, HMODULE proxy) {
    auto state_fn = reinterpret_cast<PFNTESTSTATE>(GetProcAddress(proxy, "hw2vr_test_state"));
    auto set_phase = reinterpret_cast<PFNSETUIPHASE>(GetProcAddress(proxy, "hw2vr_test_set_ui_phase"));
    auto marker_depth = reinterpret_cast<PFNMARKERDEPTH>(GetProcAddress(proxy, "hw2vr_test_last_marker_depth"));
    check(state_fn && set_phase && marker_depth, "proxy exports the world marker test hooks");
    if (!state_fn || !set_phase || !marker_depth) return;
    const HudTestState before = state_fn();
    const GLuint hud_fbo = before.hud_fbo;
    RECT rc;
    GetClientRect(WindowFromDC(dc), &rc);
    const int w = rc.right, h = rc.bottom;
    auto bind_fbo = proc<PFNBINDFRAMEBUFFER>("glBindFramebuffer");
    auto uniform4fv = proc<PFNUNIFORM4FV>("glUniform4fv");
    auto blend_separate = proc<PFNBLENDFUNCSEPARATE>("glBlendFuncSeparate");

    // Two quads in the ortho [0,1] screen space: a "selection marker" round the screen centre (over the depth
    // scene's quad) and a "taskbar button" in the bottom-left corner.
    const float quads[24] = {0.4f, 0.4f, 0.6f, 0.4f, 0.6f, 0.6f, 0.4f, 0.4f, 0.6f, 0.6f, 0.4f, 0.6f,
                             0.0f, 0.0f, 0.2f, 0.0f, 0.2f, 0.1f, 0.0f, 0.0f, 0.2f, 0.1f, 0.0f, 0.1f};
    GLuint vao, vbo;
    proc<PFNGENVERTEXARRAYS>("glGenVertexArrays")(1, &vao);
    proc<PFNBINDVERTEXARRAY>("glBindVertexArray")(vao);
    proc<PFNGENBUFFERS>("glGenBuffers")(1, &vbo);
    proc<PFNBINDBUFFER>("glBindBuffer")(GL_ARRAY_BUFFER, vbo);
    proc<PFNBUFFERDATA>("glBufferData")(GL_ARRAY_BUFFER, sizeof quads, quads, GL_STATIC_DRAW);
    proc<PFNVERTEXATTRIBPOINTER>("glVertexAttribPointer")(0, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
    proc<PFNENABLEVERTEXATTRIBARRAY>("glEnableVertexAttribArray")(0);
    const char* vs = "#version 330 compatibility\nlayout(location = 0) in vec2 inPos;\nuniform mat4 inMatP, inMatMV;\n"
                     "void main() { gl_Position = inMatP * inMatMV * vec4(inPos, 0.0, 1.0); }\n";
    const char* fs = "#version 330\nuniform vec4 inTint;\nout vec4 color;\nvoid main() { color = inTint; }\n";
    const GLuint ui = build_program(gl, vs, fs);
    const float identity[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    const float blue[4] = {0, 0, 0.5f, 0.5f};

    set_phase(1, 0);
    glEnable(GL_BLEND);
    for (int frame = 0; frame < 4; ++frame) {
        bind_fbo(GL_FRAMEBUFFER, hud_fbo);
        glViewport(0, 0, w, h);
        glClearColor(0, 0, 0, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        blend_separate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ZERO, GL_ONE_MINUS_SRC_ALPHA);
        gl.use_program(ui);
        for (int i = 0; i < 3; ++i) gl.uniform_matrix4fv(gl.get_uniform_location(ui, "inMatP"), 1, GL_FALSE, kOrtho);
        gl.uniform_matrix4fv(gl.get_uniform_location(ui, "inMatMV"), 1, GL_FALSE, identity);
        uniform4fv(gl.get_uniform_location(ui, "inTint"), 1, blue);
        set_phase(1, 0);
        glDrawArrays(GL_TRIANGLES, 0, 6);  // world-space layer: the marker
        set_phase(1, 1);                   // the game runs PHASE_UIIn
        glDrawArrays(GL_TRIANGLES, 6, 6);  // screen-space layer: the button
        bind_fbo(GL_FRAMEBUFFER, 0);
        wglSwapBuffers(dc);
    }
    set_phase(0, 0);
    const HudTestState after = state_fn();
    std::printf("  world markers: %u placed (%u anchored), last depth %.2f\n", after.world_draws - before.world_draws,
                after.world_anchored - before.world_anchored, marker_depth());
    check(after.world_draws - before.world_draws == 4 && after.world_anchored - before.world_anchored == 4,
          "world markers: one per frame, each anchored on the scene behind it");
    check(std::fabs(marker_depth() - 200.0f) < 2.0f, "world markers: anchored at the depth scene's quad (200)");
    check(after.redirected_draws - before.redirected_draws == 4, "screen-space UI after PHASE_UIIn still goes to the overlay");

    std::vector<unsigned char> px(static_cast<size_t>(w) * h * 4);
    auto at = [&](float x, float y) { return &px[(static_cast<size_t>(y * h) * w + static_cast<size_t>(x * w)) * 4]; };
    auto print = [](const char* what, const unsigned char* p) { std::printf("  %-26s %3d,%3d,%3d,%3d\n", what, p[0], p[1], p[2], p[3]); };
    glBindTexture(GL_TEXTURE_2D, after.overlay_texture);
    glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
    print("overlay  marker:", at(0.5f, 0.5f));
    print("overlay  button:", at(0.1f, 0.05f));
    check(pixel_near(at(0.5f, 0.5f), 0, 0, 0, 0) && pixel_near(at(0.1f, 0.05f), 0, 0, 64, 128),
          "overlay: the button only (the marker belongs in the world)");
    check(glGetError() == GL_NO_ERROR, "no GL errors in the world marker test");
}

// The UI phase hook's signature, against the real HomeworldRM.exe (mapped as an image, not run). HW2VR_GAME_EXE is
// set by tools/smoke-test.ps1 when the game is installed.
// The VR panels' images, written next to the exe for a look (tools/ui_preview.py turns them into PNGs).
void ui_render_test(HMODULE proxy) {
    using PFNRENDERUI = int (*)(const wchar_t*);
    auto render = reinterpret_cast<PFNRENDERUI>(GetProcAddress(proxy, "hw2vr_test_render_ui"));
    wchar_t exe[MAX_PATH];
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    std::wstring dir(exe);
    dir = dir.substr(0, dir.find_last_of(L"\\/"));
    const int written = render ? render(dir.c_str()) : 0;
    check(written == 4, "tablet and order wheel images render (ui_*.raw next to the smoke test)");
}

void ui_phase_signature_test(HMODULE proxy) {
    wchar_t exe[MAX_PATH] = {};
    if (!GetEnvironmentVariableW(L"HW2VR_GAME_EXE", exe, MAX_PATH)) {
        std::printf("  (HW2VR_GAME_EXE not set - UI phase signature not checked)\n");
        return;
    }
    using PFNFIND = unsigned (*)(const wchar_t*);
    auto find = reinterpret_cast<PFNFIND>(GetProcAddress(proxy, "hw2vr_test_find_ui_in"));
    const unsigned rva = find ? find(exe) : 0;
    std::printf("  PHASE_UIIn call in %ls: RVA 0x%X (Ghidra 0x%08X)\n", exe, rva, rva + 0x400000);
    check(rva == 0x3161FA, "UI phase hook finds the PHASE_UIIn call (build 12653478: Ghidra 0x007161FA)");
}

// --- Second-eye replay ([debug] replay_test_offset_x): record, replay with an offset camera, deferred deletes -------
struct ReplayStats {
    unsigned replays, skipped, commands, bytes;  // mirrors hw2vr::replay::Stats
};
using PFNREPLAYSTATS = ReplayStats (*)();

float replay_test_offset() {
    wchar_t buf[32];
    GetPrivateProfileStringW(L"debug", L"replay_test_offset_x", L"0", buf, 32, ini_next_to_exe().c_str());
    return std::wcstof(buf, nullptr);
}

// Each frame: clear an FBO, draw two quads through one streaming buffer (re-uploaded before each draw), delete the
// buffer mid-frame. The proxy replays the frame at the swap with the view shifted by `offset`, so afterwards the FBO
// must hold the *shifted* quads only - proving the clear and both draws were replayed with the new camera, each
// draw got its own buffer data back, and the deferred delete kept the buffer alive for the replay.
void replay_pipeline_test(const Gl& gl, HDC dc, HMODULE proxy, float offset) {
    RECT rc;
    GetClientRect(WindowFromDC(dc), &rc);
    const int w = rc.right, h = rc.bottom;
    auto bind_fbo = proc<PFNBINDFRAMEBUFFER>("glBindFramebuffer");
    auto gen_buffers = proc<PFNGENBUFFERS>("glGenBuffers");
    auto bind_buffer = proc<PFNBINDBUFFER>("glBindBuffer");
    auto buffer_data = proc<PFNBUFFERDATA>("glBufferData");
    auto attrib_pointer = proc<PFNVERTEXATTRIBPOINTER>("glVertexAttribPointer");
    auto enable_attrib = proc<PFNENABLEVERTEXATTRIBARRAY>("glEnableVertexAttribArray");
    auto bind_vao = proc<PFNBINDVERTEXARRAY>("glBindVertexArray");
    using PFNDELETEBUFFERS = void(APIENTRY*)(GLsizei, const GLuint*);
    auto delete_buffers = proc<PFNDELETEBUFFERS>("glDeleteBuffers");

    GLuint tex, fbo, vao;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8_, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    proc<PFNGENFRAMEBUFFERS>("glGenFramebuffers")(1, &fbo);
    bind_fbo(GL_FRAMEBUFFER, fbo);
    proc<PFNFRAMEBUFFERTEXTURE2D>("glFramebufferTexture2D")(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
    bind_fbo(GL_FRAMEBUFFER, 0);
    proc<PFNGENVERTEXARRAYS>("glGenVertexArrays")(1, &vao);

    const char* vs = "#version 330 compatibility\nlayout(location = 0) in vec2 inPos;\nuniform mat4 inMatP, inMatV, inMatM;\n"
                     "void main() { gl_Position = inMatP * inMatV * inMatM * vec4(inPos, 0.0, 1.0); }\n";
    const char* fs = "#version 330\nout vec4 color;\nvoid main() { color = vec4(1.0); }\n";
    const GLuint scene = build_program(gl, vs, fs);
    check(scene != 0, "replay test program linked");
    const float identity[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    // A camera away from the origin, like the game's (a translation-free view is treated as a skybox and only
    // rotated), and a projection that flattens z so the translated quads stay inside the clip volume.
    const float view[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, -5, 1};
    const float flat_projection[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1};
    auto quad = [](float x0, float x1, float* v) {
        const float q[12] = {x0, -1, x1, -1, x1, 1, x0, -1, x1, 1, x0, 1};
        std::memcpy(v, q, sizeof q);
    };
    float quad_a[12], quad_b[12];
    quad(-0.9f, -0.5f, quad_a);
    quad(0.3f, 0.5f, quad_b);

    for (int frame = 0; frame < 6; ++frame) {
        bind_fbo(GL_FRAMEBUFFER, fbo);
        glViewport(0, 0, w, h);
        glDisable(GL_BLEND);
        glClearColor(0, 0, 0, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        gl.use_program(scene);
        gl.uniform_matrix4fv(gl.get_uniform_location(scene, "inMatP"), 1, GL_FALSE, flat_projection);
        gl.uniform_matrix4fv(gl.get_uniform_location(scene, "inMatV"), 1, GL_FALSE, view);
        gl.uniform_matrix4fv(gl.get_uniform_location(scene, "inMatM"), 1, GL_FALSE, identity);
        bind_vao(vao);
        GLuint vbo;
        gen_buffers(1, &vbo);
        bind_buffer(GL_ARRAY_BUFFER, vbo);
        attrib_pointer(0, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
        enable_attrib(0);
        buffer_data(GL_ARRAY_BUFFER, sizeof quad_a, quad_a, GL_STATIC_DRAW);
        glDrawArrays(GL_TRIANGLES, 0, 6);
        buffer_data(GL_ARRAY_BUFFER, sizeof quad_b, quad_b, GL_STATIC_DRAW);  // same buffer, new data
        glDrawArrays(GL_TRIANGLES, 0, 6);
        delete_buffers(1, &vbo);  // mid-frame delete: must wait for the replay
        bind_fbo(GL_FRAMEBUFFER, 0);
        wglSwapBuffers(dc);  // the proxy replays this frame with the shifted camera
    }

    auto stats_fn = reinterpret_cast<PFNREPLAYSTATS>(GetProcAddress(proxy, "hw2vr_replay_stats"));
    const ReplayStats s = stats_fn ? stats_fn() : ReplayStats{};
    std::printf("replay stats: %u replayed, %u skipped, %u GL calls / %u payload bytes in the last frame\n", s.replays,
                s.skipped, s.commands, s.bytes);
    check(s.replays >= 5, "every recorded frame was replayed");

    std::vector<unsigned char> px(static_cast<size_t>(w) * h * 4);
    glBindTexture(GL_TEXTURE_2D, tex);
    glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
    auto lit = [&](float x_ndc) {
        const int x = static_cast<int>((x_ndc + 1.0f) * 0.5f * w);
        return px[(static_cast<size_t>(h / 2) * w + x) * 4] > 128;
    };
    std::printf("  quad A: original x=-0.8 %s, shifted x=%.2f %s;  quad B: original x=0.35 %s, shifted x=%.2f %s\n",
                lit(-0.8f) ? "lit" : "dark", -0.8f + offset + 0.1f, lit(-0.7f + offset) ? "lit" : "dark",
                lit(0.35f) ? "lit" : "dark", 0.4f + offset, lit(0.4f + offset) ? "lit" : "dark");
    check(!lit(-0.8f) && lit(-0.7f + offset), "replay redrew quad A with the shifted camera (and replayed the clear)");
    check(!lit(0.35f) && lit(0.4f + offset), "replay redrew quad B from its own buffer data, after the deferred delete");
    check(glGetError() == GL_NO_ERROR, "no GL errors in the replay test");
}

}  // namespace

int main() {
    const Expectation expect = read_ini_next_to_exe();
    static const char* kModeNames[] = {"off", "yaw", "offset", "yaw+offset"};
    std::printf("spike start_mode from hw2vr.ini: %s\n", kModeNames[expect.mode]);

    HMODULE proxy = GetModuleHandleW(L"opengl32.dll");
    wchar_t proxy_path[MAX_PATH], exe_path[MAX_PATH];
    GetModuleFileNameW(proxy, proxy_path, MAX_PATH);
    GetModuleFileNameW(nullptr, exe_path, MAX_PATH);
    *wcsrchr(exe_path, L'\\') = 0;
    std::printf("opengl32.dll in use: %ls\n", proxy_path);
    check(_wcsnicmp(proxy_path, exe_path, wcslen(exe_path)) == 0, "loader picked the proxy from the exe directory");

    WNDCLASSW wc{};
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"hw2vr_smoke";
    RegisterClassW(&wc);
    HWND wnd = CreateWindowW(wc.lpszClassName, L"hw2vr smoke", WS_OVERLAPPEDWINDOW, 0, 0, 64, 64, nullptr, nullptr,
                             wc.hInstance, nullptr);
    HDC dc = GetDC(wnd);
    PIXELFORMATDESCRIPTOR pfd{sizeof pfd, 1, PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER, PFD_TYPE_RGBA, 32};
    pfd.cDepthBits = 32;
    check(SetPixelFormat(dc, ChoosePixelFormat(dc, &pfd), &pfd) == TRUE, "pixel format set");

    HGLRC legacy = wglCreateContext(dc);
    check(legacy && wglMakeCurrent(dc, legacy), "legacy context created and current (forwarded wgl*)");
    auto create_attribs = proc<PFNCREATECONTEXTATTRIBS>("wglCreateContextAttribsARB");
    const int attribs[] = {WGL_CONTEXT_MAJOR_VERSION_ARB, 3, WGL_CONTEXT_MINOR_VERSION_ARB, 3,
                           WGL_CONTEXT_PROFILE_MASK_ARB, WGL_CONTEXT_COMPATIBILITY_PROFILE_BIT_ARB, 0};
    HGLRC ctx = create_attribs ? create_attribs(dc, nullptr, attribs) : nullptr;
    wglMakeCurrent(nullptr, nullptr);
    wglDeleteContext(legacy);
    check(ctx && wglMakeCurrent(dc, ctx), "3.3 compatibility context created like the game's");
    std::printf("GL_VERSION  %s\nGL_RENDERER %s\n", glGetString(GL_VERSION), glGetString(GL_RENDERER));
    check(glGetString(GL_VERSION) != nullptr, "glGetString forwarded");

    const Gl gl;
    check(module_of(gl.use_program) == proxy && module_of(gl.get_uniform_location) == proxy &&
              module_of(gl.uniform_matrix4fv) == proxy && module_of(gl.link_program) == proxy &&
              module_of(gl.delete_program) == proxy && module_of(wglGetProcAddress("glBindFramebuffer")) == proxy,
          "wglGetProcAddress returns proxy hooks for the 6 tracked functions");
    check(gl.create_shader && module_of(gl.create_shader) != proxy, "untracked functions (glCreateShader) pass through");

    // The shader conventions seen in the game (docs/FINDINGS.md, groups A-C).
    const GLuint scene_v = gl.build("void main() { gl_Position = inMatP * inMatV * inMatM * vec4(inPos, 1.0); }\n");
    const GLuint scene_c = gl.build("void main() { gl_Position = inMatP * inMatC * inMatMV * vec4(inPos, 1.0); }\n");
    const GLuint hud = gl.build("void main() { gl_Position = inMatP * inMatMV * vec4(inPos, 1.0); }\n");
    const GLuint mv_as_view = gl.build("void main() { gl_Position = inMatP * inMatMV * vec4(inPos, 1.0); }\n");
    const GLuint view_space = gl.build("void main() { gl_Position = inMatP * vec4(inPos, 1.0); }\n");
    const GLuint hud_p_only = gl.build("void main() { gl_Position = inMatP * vec4(inPos, 1.0); }\n");
    check(scene_v && scene_c && hud && mv_as_view && view_space && hud_p_only,
          "6 test programs linked (through hooked glLinkProgram)");

    float want[16];
    gl.use_program(scene_v);
    expect.camera(kView, want);
    check(gl.upload_reads_back(scene_v, "inMatV", kView, want), "P*V*M program: inMatV gets the camera override");
    expect.camera(kSkyView, want);
    check(gl.upload_reads_back(scene_v, "inMatV", kSkyView, want), "rotation-only inMatV (sky) gets rotation only");
    check(gl.upload_reads_back(scene_v, "inMatM", kModel, kModel), "inMatM untouched");
    check(gl.upload_reads_back(scene_v, "inMatP", kPerspective, kPerspective), "inMatP untouched (spike is view-only)");

    gl.use_program(scene_c);
    expect.camera(kView, want);
    check(gl.upload_reads_back(scene_c, "inMatC", kView, want), "P*C*MV program: inMatC gets the camera override");
    gl.upload_reads_back(scene_c, "inMatP", kPerspective, kPerspective);
    check(gl.upload_reads_back(scene_c, "inMatMV", kModel, kModel), "inMatMV next to inMatC (model->world) untouched");

    gl.use_program(hud);
    gl.upload_reads_back(hud, "inMatP", kOrtho, kOrtho);
    check(gl.upload_draw_reads_back(hud, "inMatMV", kView, kView), "HUD program (ortho inMatP): inMatMV untouched");

    gl.use_program(mv_as_view);
    gl.upload_reads_back(mv_as_view, "inMatP", kPerspective, kPerspective);
    expect.camera(kView, want);
    check(gl.upload_draw_reads_back(mv_as_view, "inMatMV", kView, want),
          "P*MV program with perspective inMatP: inMatMV treated as camera");

    // A program used for 2D and 3D in the same frame, uploading inMatMV before inMatP (prog 68: HUD lines vs.
    // Sensors Manager ship icons). Only the projection at draw time may decide.
    const float identity[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    const GLuint dual = gl.build("void main() { gl_Position = inMatP * inMatMV * vec4(inPos, 1.0); }\n");
    gl.use_program(dual);
    gl.upload_reads_back(dual, "inMatP", kOrtho, kOrtho);
    check(gl.upload_draw_reads_back(dual, "inMatMV", identity, identity), "dual-use program, 2D draw: inMatMV raw");
    gl.uniform_matrix4fv(gl.get_uniform_location(dual, "inMatMV"), 1, GL_FALSE, kView);  // MV first...
    gl.upload_reads_back(dual, "inMatP", kPerspective, kPerspective);                      // ...then P
    expect.camera(kView, want);
    check(gl.draw_reads_back(dual, "inMatMV", want), "dual-use program, 3D draw (MV uploaded before P): camera override");
    gl.uniform_matrix4fv(gl.get_uniform_location(dual, "inMatMV"), 1, GL_FALSE, identity);
    gl.upload_reads_back(dual, "inMatP", kOrtho, kOrtho);
    check(gl.draw_reads_back(dual, "inMatMV", identity), "dual-use program, back to 2D: inMatMV raw again");
    gl.delete_program(dual);

    gl.use_program(view_space);
    expect.view_space_projection(kPerspective, want);
    check(gl.upload_reads_back(view_space, "inMatP", kPerspective, want),
          "P-only program, perspective (view-space input, e.g. light glows): inMatP becomes P*E");
    gl.use_program(hud_p_only);
    check(gl.upload_reads_back(hud_p_only, "inMatP", kOrtho, kOrtho), "P-only program, ortho (2D HUD): untouched");
    check(glGetError() == GL_NO_ERROR, "no GL errors");

    check(wglSwapBuffers(dc) == TRUE, "wglSwapBuffers (hooked export; draws the mode square when on)");
    check(glGetError() == GL_NO_ERROR, "no GL errors after the mode square");
    check(SwapBuffers(dc) == TRUE, "gdi32 SwapBuffers");

    if (hud_test_requested()) {
        hud_pipeline_test(gl, dc, proxy);
        depth_probe_test(gl, proxy);
        world_marker_test(gl, dc, proxy);
        ui_phase_signature_test(proxy);
        ui_render_test(proxy);
    }
    if (const float offset = replay_test_offset(); offset != 0.0f) replay_pipeline_test(gl, dc, proxy, offset);

    for (GLuint p : {scene_v, scene_c, hud, mv_as_view, view_space, hud_p_only}) gl.delete_program(p);
    wglMakeCurrent(nullptr, nullptr);
    wglDeleteContext(ctx);
    ReleaseDC(wnd, dc);
    DestroyWindow(wnd);

    std::printf("%s: %d failure(s)\n", g_failures ? "FAILED" : "PASSED", g_failures);
    return g_failures ? 1 : 0;
}
