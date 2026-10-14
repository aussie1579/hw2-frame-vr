// Smoke test for the opengl32.dll proxy, without the game. Built next to the proxy and statically importing
// opengl32, so the loader picks our DLL from the exe directory exactly like HomeworldRM.exe does. Creates the same
// GL 3.3 compatibility context as the game, then checks forwarding, the wglGetProcAddress hooks, and which
// uniforms the camera override touches. It reads the hw2vr.ini next to it (as the proxy does) to know whether the
// spike override starts on; tools/smoke-test.ps1 runs it with the override off and on.
#include <windows.h>
#include <GL/gl.h>

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
};

// --- HUD pipeline ([debug] hud_capture = 1): capture, transmittance -> coverage, clear, cursor redirect ----------
struct HudTestState {
    unsigned hud_fbo, overlay_texture, captures, redirected_draws;  // mirrors hw2vr::hud::TestState
};
using PFNTESTSTATE = HudTestState (*)();
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

// Plays the game's HUD pattern for 16 frames: HUD drawn into its own FBO (cleared to (0,0,0,1), transmittance
// alpha blend, 3 ortho inMatP uploads), then the default framebuffer: a "composite" by a non-UI program and a
// "cursor" by the UI program. Checks what the proxy made of it.
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

    const float quads[] = {0, 0, 0.5f, 0, 0.5f, 1, 0, 0, 0.5f, 1, 0, 1,                // left half: HUD element
                           0.5f, 0, 1, 0, 1, 1, 0.5f, 0, 1, 1, 0.5f, 1};             // right half: "cursor"
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
    const GLuint ui = build_program(gl, vs, fs), other = build_program(gl, vs, fs);
    check(ui && other, "HUD test programs linked");
    const float identity[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    const float tint[4] = {0.5f, 0, 0, 0.5f};  // HUD target expects rgb 0.25, transmittance 0.5
    auto setup = [&](GLuint program, int ortho_uploads) {
        gl.use_program(program);
        for (int i = 0; i < ortho_uploads; ++i) gl.uniform_matrix4fv(gl.get_uniform_location(program, "inMatP"), 1, GL_FALSE, kOrtho);
        gl.uniform_matrix4fv(gl.get_uniform_location(program, "inMatMV"), 1, GL_FALSE, identity);
        uniform4fv(gl.get_uniform_location(program, "inTint"), 1, tint);
    };

    glEnable(GL_BLEND);
    for (int frame = 0; frame < 16; ++frame) {
        bind_fbo(GL_FRAMEBUFFER, hud_fbo);
        glViewport(0, 0, w, h);
        glClearColor(0, 0, 0, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        blend_separate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ZERO, GL_ONE_MINUS_SRC_ALPHA);  // the game's HUD blend
        setup(ui, 3);
        glDrawArrays(GL_TRIANGLES, 0, 6);  // HUD element, left half
        bind_fbo(GL_FRAMEBUFFER, 0);       // leaving the HUD FBO: the proxy captures here
        setup(other, 1);
        glDrawArrays(GL_TRIANGLES, 0, 6);  // "composite" by a non-UI program: must stay on screen
        setup(ui, 1);
        glDrawArrays(GL_TRIANGLES, 6, 6);  // "cursor" by the UI program: must go onto the overlay
        wglSwapBuffers(dc);
    }

    auto state_fn = reinterpret_cast<PFNTESTSTATE>(GetProcAddress(proxy, "hw2vr_test_state"));
    const HudTestState s = state_fn ? state_fn() : HudTestState{};
    std::printf("HUD state: hud_fbo %u (ours %u), overlay tex %u, captures %u, redirected draws %u\n", s.hud_fbo,
                hud_fbo, s.overlay_texture, s.captures, s.redirected_draws);
    check(s.hud_fbo == hud_fbo, "HUD framebuffer identified from its ortho uploads");
    check(s.captures >= 3 && s.redirected_draws == s.captures, "one capture and one cursor redirect per frame");

    std::vector<unsigned char> px(static_cast<size_t>(w) * h * 4);
    auto at = [&](int x, int y) { return &px[(static_cast<size_t>(y) * w + x) * 4]; };
    glBindTexture(GL_TEXTURE_2D, s.overlay_texture);
    glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
    const unsigned char* left = at(w / 4, h / 2);
    const unsigned char* right = at(3 * w / 4, h / 2);
    std::printf("overlay pixels: left %d,%d,%d,%d  right %d,%d,%d,%d\n", left[0], left[1], left[2], left[3], right[0],
                right[1], right[2], right[3]);
    check(pixel_near(left, 64, 0, 0, 128), "overlay: HUD element premultiplied with coverage alpha (64,0,0,128)");
    check(pixel_near(right, 64, 0, 0, 128), "overlay: cursor redirected onto it with coverage alpha (64,0,0,128)");
    glBindTexture(GL_TEXTURE_2D, hud_tex);
    glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
    check(pixel_near(at(w / 4, h / 2), 0, 0, 0, 255), "game's HUD target cleared after capture (no HUD in the eyes)");
    check(glGetError() == GL_NO_ERROR, "no GL errors in the HUD pipeline");
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
    check(gl.upload_reads_back(hud, "inMatMV", kView, kView), "HUD program (ortho inMatP): inMatMV untouched");

    gl.use_program(mv_as_view);
    gl.upload_reads_back(mv_as_view, "inMatP", kPerspective, kPerspective);
    expect.camera(kView, want);
    check(gl.upload_reads_back(mv_as_view, "inMatMV", kView, want),
          "P*MV program with perspective inMatP: inMatMV treated as camera");

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

    if (hud_test_requested()) hud_pipeline_test(gl, dc, proxy);

    for (GLuint p : {scene_v, scene_c, hud, mv_as_view, view_space, hud_p_only}) gl.delete_program(p);
    wglMakeCurrent(nullptr, nullptr);
    wglDeleteContext(ctx);
    ReleaseDC(wnd, dc);
    DestroyWindow(wnd);

    std::printf("%s: %d failure(s)\n", g_failures ? "FAILED" : "PASSED", g_failures);
    return g_failures ? 1 : 0;
}
