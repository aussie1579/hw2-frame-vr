#include "proxy/engine_hooks.h"

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>

#include "common/log.h"
#include "common/mat4.h"

// Written by the stub below; read by the proxy on the render thread.
extern "C" {
volatile unsigned char g_hw2vr_ui_screen_phase = 0;
void* g_hw2vr_ui_in_target = nullptr;  // rnd_RunPhase
void* g_hw2vr_camera_update_target = nullptr;  // the game's camera update
void __cdecl hw2vr_camera_updated(float* camera);
}

namespace hw2vr::engine {
namespace {

bool g_installed = false;
bool g_camera_hooked = false;
bool g_turn_on = false;
float g_turn_inverse[16];
unsigned g_camera_updates = 0, g_camera_rejected = 0, g_camera_calls = 0;
float g_fov = 0.0f;             // override, degrees (0 = none)
float g_game_fov = 0.0f;        // the game's own, saved while overridden
float* g_fov_camera = nullptr;  // where it was saved
constexpr int kFov = 0x124 / 4;

// Replaces the patched call's target: raise the flag, then continue into rnd_RunPhase with the stack untouched
// (the caller's return address and phase-id argument are still where it put them).
__declspec(naked) void ui_in_stub() {
    __asm {
        mov byte ptr [g_hw2vr_ui_screen_phase], 1
        jmp dword ptr [g_hw2vr_ui_in_target]
    }
}

// Replaces the camera update's call: run it (thiscall, ecx = the camera), then turn the matrices it built. The pushed
// camera pointer survives the call (it takes no stack arguments) and is the cdecl argument of the second call.
__declspec(naked) void camera_update_stub() {
    __asm {
        push ecx
        call dword ptr [g_hw2vr_camera_update_target]
        call hw2vr_camera_updated
        add esp, 4
        ret
    }
}

struct Section {
    const uint8_t* begin;
    size_t size;
};

bool image_sections(const uint8_t* base, Section* code, Section* image) {
    if (!base) return false;
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS32*>(base + dos->e_lfanew);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE || nt->Signature != IMAGE_NT_SIGNATURE) return false;
    *image = {base, nt->OptionalHeader.SizeOfImage};
    const IMAGE_SECTION_HEADER* s = IMAGE_FIRST_SECTION(nt);
    for (unsigned i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++s) {
        if (std::memcmp(s->Name, ".text", 6) == 0) {
            *code = {base + s->VirtualAddress, s->Misc.VirtualSize};
            return true;
        }
    }
    return false;
}

const uint8_t* find(const uint8_t* begin, const uint8_t* end, const void* pattern, size_t n) {
    for (const uint8_t* p = begin; p + n <= end; ++p)
        if (std::memcmp(p, pattern, n) == 0) return p;
    return nullptr;
}

uint32_t read32(const uint8_t* p) {
    uint32_t v;
    std::memcpy(&v, p, 4);
    return v;
}

// Points a `call rel32` at `to`.
bool redirect_call(uint8_t* call, const void* to) {
    const int32_t rel = static_cast<int32_t>(reinterpret_cast<uintptr_t>(to) - (reinterpret_cast<uintptr_t>(call) + 5));
    DWORD old = 0;
    if (!VirtualProtect(call + 1, 4, PAGE_EXECUTE_READWRITE, &old)) return false;
    std::memcpy(call + 1, &rel, 4);
    VirtualProtect(call + 1, 4, old, &old);
    FlushInstructionCache(GetCurrentProcess(), call, 5);
    return true;
}

// The game's view matrix: rotation + translation. Anything else is left alone.
bool rigid(const float m[16]) {
    if (std::fabs(m[3]) > 1e-3f || std::fabs(m[7]) > 1e-3f || std::fabs(m[11]) > 1e-3f || std::fabs(m[15] - 1.0f) > 1e-3f)
        return false;
    for (int c = 0; c < 3; ++c) {
        const float* v = m + c * 4;
        if (std::fabs(v[0] * v[0] + v[1] * v[1] + v[2] * v[2] - 1.0f) > 1e-2f) return false;
    }
    return true;
}

constexpr int kMaxCameraCalls = 4;

// The camera update (FUN_0078bf32 on build 12653478) starts `sub esp,64h; push ebx; mov ebx,ecx; push esi; push edi;
// cmp byte ptr [ebx+190h],1` and pushes the "roll_blend" string's address (its roll blend name) in its first 0x200
// bytes. Returns it and fills `calls` with every `call rel32` to it in the code section (*n_calls counts them all).
const uint8_t* find_camera_update(const uint8_t* image_base, uint8_t** calls, int* n_calls) {
    *n_calls = 0;
    Section code{}, image{};
    if (!image_sections(image_base, &code, &image)) return nullptr;
    static const uint8_t kPrologue[] = {0x83, 0xEC, 0x64, 0x53, 0x8B, 0xD9, 0x56, 0x57, 0x80, 0xBB, 0x90, 0x01, 0x00, 0x00, 0x01};
    static const char kRoll[] = "roll_blend";
    const uint8_t* const image_end = image.begin + image.size;
    const uint8_t* const code_end = code.begin + code.size;
    const uint8_t* fn = nullptr;
    for (const uint8_t* p = find(code.begin, code_end, kPrologue, sizeof kPrologue); p && !fn;
         p = find(p + 1, code_end, kPrologue, sizeof kPrologue)) {
        for (const uint8_t* str = find(image.begin, image_end, kRoll, sizeof kRoll); str && !fn;
             str = find(str + 1, image_end, kRoll, sizeof kRoll)) {
            uint8_t push[5] = {0x68};
            const uint32_t str_addr = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(str));
            std::memcpy(push + 1, &str_addr, 4);
            if (find(p, (std::min)(p + 0x200, code_end), push, sizeof push)) fn = p;
        }
    }
    if (!fn) return nullptr;
    for (const uint8_t* q = code.begin; q + 5 <= code_end; ++q) {
        if (*q != 0xE8) continue;
        const uintptr_t target = reinterpret_cast<uintptr_t>(q) + 5 + static_cast<int32_t>(read32(q + 1));
        if (target != reinterpret_cast<uintptr_t>(fn)) continue;
        if (*n_calls < kMaxCameraCalls) calls[*n_calls] = const_cast<uint8_t*>(q);
        ++*n_calls;
    }
    return fn;
}

void install_camera(const uint8_t* base) {
    uint8_t* calls[kMaxCameraCalls] = {};
    int n = 0;
    const uint8_t* fn = find_camera_update(base, calls, &n);
    if (!fn || n < 1 || n > kMaxCameraCalls) {
        HW2VR_LOG("engine: the camera update wasn't found (%s, %d calls) - the game camera won't turn with the head",
                  fn ? "function found" : "no function", n);
        return;
    }
    g_hw2vr_camera_update_target = const_cast<uint8_t*>(fn);
    int redirected = 0;
    for (int i = 0; i < n; ++i) redirected += redirect_call(calls[i], &camera_update_stub) ? 1 : 0;
    const unsigned rva = static_cast<unsigned>(reinterpret_cast<uintptr_t>(fn) - reinterpret_cast<uintptr_t>(base));
    HW2VR_LOG("engine: camera turn hook installed - camera update at exe+0x%X (Ghidra 0x%08X), %d of %d call(s) "
              "redirected", rva, rva + 0x400000, redirected, n);
    g_camera_hooked = redirected > 0;
}

}  // namespace

// After every camera update: the view (camera +0x04) and the sky's rotation (+0x44) turned in view space,
// view' = turn^-1 * view. The eye position doesn't change; the culling planes and everything else come after.
void turn_camera(float* camera) {
    ++g_camera_calls;
    // The field of view is kept, not rebuilt by the update: set it here and the next update (and the renderer)
    // project with it.
    if (g_fov > 0.0f && camera[kFov] != g_fov) {
        if (!g_fov_camera) g_fov_camera = camera, g_game_fov = camera[kFov];
        camera[kFov] = g_fov;
    } else if (g_fov <= 0.0f && g_fov_camera == camera) {
        camera[kFov] = g_game_fov;
        g_fov_camera = nullptr;
    }
    if (!g_turn_on) return;
    float* const mats[2] = {camera + 1, camera + 0x11};
    if (!rigid(mats[0]) || !rigid(mats[1])) {
        if (g_camera_rejected++ == 0) HW2VR_LOG("engine: a camera update built a matrix that isn't a rotation - not turned");
        return;
    }
    for (float* m : mats) {
        float out[16];
        mat4::multiply(g_turn_inverse, m, out);
        std::memcpy(m, out, sizeof out);
    }
    ++g_camera_updates;
}

// The call instruction that runs PHASE_UIIn in a mapped HomeworldRM.exe image, or nullptr.
uint8_t* find_ui_in_call(const uint8_t* image_base) {
    Section code{}, image{};
    if (!image_sections(image_base, &code, &image)) return nullptr;
    // 1. The phase name string (with its terminator, so a longer name can't match), 2. code that pushes its address,
    // 3. mov [phase id], eax after the name lookup, 4. push dword ptr [phase id]; call rnd_RunPhase. Every candidate
    // at each step is tried; anything that doesn't line up leaves the game untouched.
    static const char kName[] = "PHASE_UIIn";
    const uint8_t* const image_end = image.begin + image.size;
    const uint8_t* const code_end = code.begin + code.size;
    uint8_t* call = nullptr;
    for (const uint8_t* str = find(image.begin, image_end, kName, sizeof kName); str && !call;
         str = find(str + 1, image_end, kName, sizeof kName)) {
        uint8_t push[5] = {0x68};
        const uint32_t str_addr = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(str));
        std::memcpy(push + 1, &str_addr, 4);
        for (const uint8_t* site = find(code.begin, code_end, push, sizeof push); site && !call;
             site = find(site + 1, code_end, push, sizeof push)) {
            for (const uint8_t* mov = site + 5; mov < site + 48 && mov + 5 <= code_end && !call; ++mov) {
                if (*mov != 0xA3) continue;
                uint8_t run[6] = {0xFF, 0x35};
                std::memcpy(run + 2, mov + 1, 4);
                const uint8_t* push_id = find(mov + 5, (std::min)(site + 112, code_end - 11), run, sizeof run);
                if (push_id && push_id[6] == 0xE8) call = const_cast<uint8_t*>(push_id + 6);
            }
        }
    }
    return call;
}

namespace {

bool install_ui(const uint8_t* base) {
    Section code{}, image{};
    uint8_t* call = find_ui_in_call(base);
    if (!call || !image_sections(base, &code, &image)) {
        HW2VR_LOG("engine: the PHASE_UIIn call wasn't found - UI phase hook off (world markers stay with the HUD)");
        return false;
    }
    const uint8_t* const code_end = code.begin + code.size;
    const uintptr_t target = reinterpret_cast<uintptr_t>(call) + 5 + static_cast<int32_t>(read32(call + 1));
    if (target < reinterpret_cast<uintptr_t>(code.begin) || target >= reinterpret_cast<uintptr_t>(code_end)) {
        HW2VR_LOG("engine: PHASE_UIIn call target 0x%08X outside the code - UI phase hook off", static_cast<unsigned>(target));
        return false;
    }
    g_hw2vr_ui_in_target = reinterpret_cast<void*>(target);

    if (!redirect_call(call, &ui_in_stub)) {
        HW2VR_LOG("engine: VirtualProtect failed (%lu) - UI phase hook off", GetLastError());
        return false;
    }
    const uintptr_t image_base = reinterpret_cast<uintptr_t>(image.begin);
    HW2VR_LOG("engine: UI phase hook installed - PHASE_UIIn call at exe+0x%X (Ghidra 0x%08X), rnd_RunPhase at exe+0x%X",
              static_cast<unsigned>(reinterpret_cast<uintptr_t>(call) - image_base),
              static_cast<unsigned>(reinterpret_cast<uintptr_t>(call) - image_base + 0x400000),
              static_cast<unsigned>(target - image_base));
    g_installed = true;
    return true;
}

}  // namespace

bool install() {
    static bool tried = false;
    if (tried) return g_installed;
    tried = true;
    const auto* base = reinterpret_cast<const uint8_t*>(GetModuleHandleW(nullptr));
    install_camera(base);
    return install_ui(base);
}

bool installed() { return g_installed; }
bool screen_ui_phase() { return g_hw2vr_ui_screen_phase != 0; }
void frame_end() { g_hw2vr_ui_screen_phase = 0; }
void set_screen_ui_phase(bool on) { g_hw2vr_ui_screen_phase = on ? 1 : 0; }
void set_test_installed(bool on) { g_installed = on; }

bool camera_hook_installed() { return g_camera_hooked; }
unsigned camera_updates() { return g_camera_updates; }
unsigned camera_calls() { return g_camera_calls; }

void set_camera_fov(float degrees) { g_fov = degrees > 1.0f && degrees < 170.0f ? degrees : 0.0f; }

void set_camera_turn(const float* turn) {
    g_turn_on = turn != nullptr;
    if (turn) mat4::rigid_inverse(turn, g_turn_inverse);
}

unsigned test_find_ui_in(const wchar_t* exe_path) {
    HMODULE m = LoadLibraryExW(exe_path, nullptr, DONT_RESOLVE_DLL_REFERENCES);
    if (!m) return 0;
    const auto* base = reinterpret_cast<const uint8_t*>(m);
    const uint8_t* call = find_ui_in_call(base);
    const unsigned rva = call ? static_cast<unsigned>(call - base) : 0;
    FreeLibrary(m);
    return rva;
}

unsigned test_find_camera_update(const wchar_t* exe_path, int* calls) {
    *calls = 0;
    HMODULE m = LoadLibraryExW(exe_path, nullptr, DONT_RESOLVE_DLL_REFERENCES);
    if (!m) return 0;
    const auto* base = reinterpret_cast<const uint8_t*>(m);
    uint8_t* found[kMaxCameraCalls] = {};
    const uint8_t* fn = find_camera_update(base, found, calls);
    const unsigned rva = fn ? static_cast<unsigned>(fn - base) : 0;
    FreeLibrary(m);
    return rva;
}

void test_turn_camera(float* camera, const float* turn) {
    set_camera_turn(turn);
    hw2vr_camera_updated(camera);
    set_camera_turn(nullptr);
}

}  // namespace hw2vr::engine

extern "C" void __cdecl hw2vr_camera_updated(float* camera) { hw2vr::engine::turn_camera(camera); }
