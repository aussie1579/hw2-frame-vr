#include <windows.h>

#include <string>

#include "common/config.h"
#include "common/log.h"
#include "common/version.h"
#include "proxy/gl_hooks.h"
#include "proxy/real_gl.h"
#include "stereo/view_override.h"
#include "vr/vr_frame.h"

namespace {

std::wstring module_dir(HMODULE module) {
    wchar_t buf[MAX_PATH];
    const DWORD n = GetModuleFileNameW(module, buf, MAX_PATH);
    std::wstring path(buf, n);
    return path.substr(0, path.find_last_of(L"\\/") + 1);
}

// Runs under the loader lock (the game imports opengl32 statically): no threads, no waiting on other DLLs.
bool init(HMODULE self) {
    const std::wstring dir = module_dir(self);
    hw2vr::log::init((dir + L"hw2vr.log").c_str());
    const hw2vr::Config cfg = hw2vr::load_config(dir + L"hw2vr.ini");
    hw2vr::log::set_verbose(cfg.verbose);
    HW2VR_LOG("%s %s opengl32.dll proxy loaded (pid %lu)", HW2VR_PRODUCT_NAME, HW2VR_VERSION_STR, GetCurrentProcessId());

    // Without the real opengl32 the game cannot render at all, so failing here stops the process cleanly
    // instead of crashing on the first GL call.
    if (!hw2vr::proxy::load_real_opengl32() || !hw2vr::proxy::init_hooks()) return false;

    hw2vr::stereo::configure_spike(cfg.spike_yaw_degrees, cfg.spike_offset_x, cfg.spike_start_mode);
    hw2vr::vrframe::Settings vr;
    vr.autostart = cfg.vr_autostart;
    vr.world_scale = cfg.vr_world_scale;
    vr.hud_distance = cfg.vr_hud_distance;
    vr.hud_scale = cfg.vr_hud_scale;
    vr.hud_premultiplied = cfg.vr_hud_premultiplied;
    hw2vr::vrframe::configure(vr, dir);  // VR itself starts on the render thread (Ctrl+Shift+F8 or autostart)
    HW2VR_LOG("hotkeys (Ctrl+Shift+): F8 VR on/off%s, F10 recentre, PgUp/PgDn world scale, F9 flat camera spike",
              cfg.vr_autostart ? " (autostart on)" : "");
    return true;
}

}  // namespace

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID reserved) {
    switch (reason) {
        case DLL_PROCESS_ATTACH:
            DisableThreadLibraryCalls(module);
            return init(module) ? TRUE : FALSE;
        case DLL_PROCESS_DETACH:
            HW2VR_LOG("unloading (%s)", reserved ? "process exit" : "FreeLibrary");
            hw2vr::log::shutdown();
            break;
    }
    return TRUE;
}
