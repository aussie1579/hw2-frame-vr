// Smoke test for the opengl32.dll proxy, without the game. Built next to the proxy and statically importing
// opengl32, so the loader picks our DLL from the exe directory exactly like HomeworldRM.exe does. Creates the same
// GL 3.3 compatibility context as the game, then checks forwarding, the wglGetProcAddress hooks, and which
// uniforms the camera override touches. It reads the hw2vr.ini next to it (as the proxy does) to know whether the
// spike override starts on; tools/smoke-test.ps1 runs it with the override off and on.
#include <windows.h>
#include <GL/gl.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

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

    void camera(const float in[16], float out[16]) const {
        if (mode == 0) {
            std::memcpy(out, in, 64);
            return;
        }
        const float a = (mode & 1) ? yaw_degrees * 3.14159265f / 180.0f : 0.0f;
        const bool sky = std::fabs(in[12]) < 1 && std::fabs(in[13]) < 1 && std::fabs(in[14]) < 1;
        const float x = (mode & 2) && !sky ? offset_x : 0.0f;
        const float c = std::cos(a), s = std::sin(a);
        const float rotation[16] = {c, 0, -s, 0, 0, 1, 0, 0, s, 0, c, 0, 0, 0, 0, 1};
        const float translation[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, -x, 0, 0, 1};
        float eye[16];
        multiply(rotation, translation, eye);
        multiply(eye, in, out);
    }
};

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
              module_of(gl.delete_program) == proxy,
          "wglGetProcAddress returns proxy hooks for the 5 tracked functions");
    check(gl.create_shader && module_of(gl.create_shader) != proxy, "untracked functions (glCreateShader) pass through");

    // The four shader conventions seen in the game (docs/FINDINGS.md).
    const GLuint scene_v = gl.build("void main() { gl_Position = inMatP * inMatV * inMatM * vec4(inPos, 1.0); }\n");
    const GLuint scene_c = gl.build("void main() { gl_Position = inMatP * inMatC * inMatMV * vec4(inPos, 1.0); }\n");
    const GLuint hud = gl.build("void main() { gl_Position = inMatP * inMatMV * vec4(inPos, 1.0); }\n");
    const GLuint mv_as_view = gl.build("void main() { gl_Position = inMatP * inMatMV * vec4(inPos, 1.0); }\n");
    check(scene_v && scene_c && hud && mv_as_view, "4 test programs linked (through hooked glLinkProgram)");

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
    check(glGetError() == GL_NO_ERROR, "no GL errors");

    check(wglSwapBuffers(dc) == TRUE, "wglSwapBuffers (hooked export)");
    check(SwapBuffers(dc) == TRUE, "gdi32 SwapBuffers");

    for (GLuint p : {scene_v, scene_c, hud, mv_as_view}) gl.delete_program(p);
    wglMakeCurrent(nullptr, nullptr);
    wglDeleteContext(ctx);
    ReleaseDC(wnd, dc);
    DestroyWindow(wnd);

    std::printf("%s: %d failure(s)\n", g_failures ? "FAILED" : "PASSED", g_failures);
    return g_failures ? 1 : 0;
}
