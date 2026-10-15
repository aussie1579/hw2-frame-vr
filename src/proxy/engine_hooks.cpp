#include "proxy/engine_hooks.h"

#include <windows.h>

#include <algorithm>
#include <cstdint>
#include <cstring>

#include "common/log.h"

// Written by the stub below; read by the proxy on the render thread.
extern "C" {
volatile unsigned char g_hw2vr_ui_screen_phase = 0;
void* g_hw2vr_ui_in_target = nullptr;  // rnd_RunPhase
}

namespace hw2vr::engine {
namespace {

bool g_installed = false;

// Replaces the patched call's target: raise the flag, then continue into rnd_RunPhase with the stack untouched
// (the caller's return address and phase-id argument are still where it put them).
__declspec(naked) void ui_in_stub() {
    __asm {
        mov byte ptr [g_hw2vr_ui_screen_phase], 1
        jmp dword ptr [g_hw2vr_ui_in_target]
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

}  // namespace

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

bool install() {
    if (g_installed) return true;
    const auto* base = reinterpret_cast<const uint8_t*>(GetModuleHandleW(nullptr));
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

    const int32_t rel = static_cast<int32_t>(reinterpret_cast<uintptr_t>(&ui_in_stub) - (reinterpret_cast<uintptr_t>(call) + 5));
    DWORD old = 0;
    if (!VirtualProtect(call + 1, 4, PAGE_EXECUTE_READWRITE, &old)) {
        HW2VR_LOG("engine: VirtualProtect failed (%lu) - UI phase hook off", GetLastError());
        return false;
    }
    std::memcpy(call + 1, &rel, 4);
    VirtualProtect(call + 1, 4, old, &old);
    FlushInstructionCache(GetCurrentProcess(), call, 5);
    const uintptr_t image_base = reinterpret_cast<uintptr_t>(image.begin);
    HW2VR_LOG("engine: UI phase hook installed - PHASE_UIIn call at exe+0x%X (Ghidra 0x%08X), rnd_RunPhase at exe+0x%X",
              static_cast<unsigned>(reinterpret_cast<uintptr_t>(call) - image_base),
              static_cast<unsigned>(reinterpret_cast<uintptr_t>(call) - image_base + 0x400000),
              static_cast<unsigned>(target - image_base));
    g_installed = true;
    return true;
}

bool installed() { return g_installed; }
bool screen_ui_phase() { return g_hw2vr_ui_screen_phase != 0; }
void frame_end() { g_hw2vr_ui_screen_phase = 0; }
void set_screen_ui_phase(bool on) { g_hw2vr_ui_screen_phase = on ? 1 : 0; }
void set_test_installed(bool on) { g_installed = on; }

unsigned test_find_ui_in(const wchar_t* exe_path) {
    HMODULE m = LoadLibraryExW(exe_path, nullptr, DONT_RESOLVE_DLL_REFERENCES);
    if (!m) return 0;
    const auto* base = reinterpret_cast<const uint8_t*>(m);
    const uint8_t* call = find_ui_in_call(base);
    const unsigned rva = call ? static_cast<unsigned>(call - base) : 0;
    FreeLibrary(m);
    return rva;
}

}  // namespace hw2vr::engine
