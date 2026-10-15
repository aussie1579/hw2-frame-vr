#include <windows.h>

#include <cstring>
#include <string>

#include "common/config.h"
#include "common/hotkeys.h"
#include "common/log.h"
#include "common/version.h"
#include "hud/hud_layer.h"
#include "hud/hud_panel.h"
#include "input/controls.h"
#include "hud/screen_windows.h"
#include "input/key_names.h"
#include "input/tablet.h"
#include "vr/vr_input.h"
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

void configure_panel_and_controls(const hw2vr::Config& cfg, const std::wstring& dir) {
    hw2vr::panel::Settings panel;
    panel.layout = static_cast<hw2vr::panel::Layout>(cfg.vr_hud_layout);
    panel.aligned_distance = cfg.vr_hud_distance;
    panel.aligned_scale = cfg.vr_hud_scale;
    panel.backing = cfg.console_backing;
    if (panel.layout == hw2vr::panel::Layout::Console) {
        panel.defaults = hw2vr::panel::centred(cfg.console_distance, cfg.console_width_degrees, cfg.console_height);
        panel.state_path = dir + L"hw2vr_console.ini";
    } else if (cfg.vr_hud_layout == 3) {  // tablet: the whole screen UI only for menus and dialogs, in front
        panel.layout = hw2vr::panel::Layout::Modal;
        panel.defaults = hw2vr::panel::centred(cfg.menu_distance, cfg.menu_width_degrees, 0.0f);
    } else {
        panel.defaults = hw2vr::panel::centred(cfg.panel_distance, cfg.panel_width_degrees, cfg.panel_height);
        panel.state_path = dir + L"hw2vr_panel.ini";
    }
    panel.pixels_per_degree = cfg.panel_pixels_per_degree;
    panel.premultiplied = cfg.vr_hud_premultiplied;
    hw2vr::hud::set_world_markers(cfg.vr_world_markers);

    const bool tablet_mode = cfg.vr_hud_layout == 3;
    hw2vr::tablet::Settings tablet;
    tablet.width = cfg.tablet_width;
    tablet.lift = cfg.tablet_lift;
    tablet.palm_axis = cfg.tablet_palm_axis;
    tablet.always = cfg.tablet_always;
    tablet.enabled = tablet_mode;
    hw2vr::tablet::configure(tablet);
    hw2vr::screens::Settings screens;
    std::memcpy(screens.status_region, cfg.status_region, sizeof screens.status_region);
    std::memcpy(screens.manager_region, cfg.manager_region, sizeof screens.manager_region);
    std::memcpy(screens.modal_region, cfg.modal_region, sizeof screens.modal_region);
    screens.manager_width = cfg.manager_width;
    screens.enabled = tablet_mode;
    hw2vr::screens::configure(screens);
    hw2vr::panel::configure(panel);

    hw2vr::controls::Settings c;
    c.stick_deadzone = cfg.stick_deadzone;
    c.camera_speed = cfg.camera_speed;
    c.camera_curve = cfg.camera_curve;
    c.camera_invert_y = cfg.camera_invert_y;
    c.zoom_notches_per_second = cfg.zoom_notches_per_second;
    c.zoom_invert = cfg.zoom_invert;
    c.click_freeze_ms = cfg.click_freeze_ms;
    c.drag_degrees = cfg.drag_degrees;
    hw2vr::vrinput::set_trigger_thresholds(cfg.trigger_press, cfg.trigger_release);
    c.filter_min_cutoff = cfg.filter_min_cutoff;
    c.filter_beta = cfg.filter_beta;
    c.haptics = cfg.haptics;
    c.show_beam = cfg.show_beam;
    c.cursor_distance = cfg.cursor_distance;
    for (int i = 0; i < hw2vr::input::kButtonCount; ++i) {
        if (!hw2vr::keys::parse_binding(cfg.buttons[i].c_str(), &c.buttons[i]))
            HW2VR_LOG("config: [buttons] %ls = '%ls' has an unknown key name - unbound", hw2vr::input::kButtons[i].ini_key,
                      cfg.buttons[i].c_str());
    }
    hw2vr::controls::configure(c);
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

    hw2vr::stereo::configure_spike(cfg.spike_yaw_degrees, cfg.spike_offset_x, cfg.spike_start_mode, cfg.key_spike);
    hw2vr::vrframe::Settings vr;
    vr.autostart = cfg.vr_autostart;
    vr.world_scale = cfg.vr_world_scale;
    vr.hud_premultiplied = cfg.vr_hud_premultiplied;
    vr.sync_stereo = cfg.vr_sync_stereo;
    vr.fit_window = cfg.vr_fit_window;
    vr.key_toggle = cfg.key_vr_toggle;
    vr.key_recentre = cfg.key_recentre;
    vr.key_scale_up = cfg.key_scale_up;
    vr.key_scale_down = cfg.key_scale_down;
    hw2vr::vrframe::configure(vr, dir);  // VR itself starts on the render thread (hotkey or autostart)
    configure_panel_and_controls(cfg, dir);
    if (cfg.debug_hud_capture) {
        hw2vr::hud::set_capture(true, cfg.vr_hud_premultiplied);
        HW2VR_LOG("debug: HUD capture forced on without VR");
    }
    if (cfg.debug_replay_test_offset_x != 0.0f) {
        hw2vr::proxy::configure_replay_test(cfg.debug_replay_test_offset_x);
        HW2VR_LOG("debug: replaying every frame without VR (view offset %.2f)", cfg.debug_replay_test_offset_x);
    }
    HW2VR_LOG("stereo mode: %s", cfg.vr_sync_stereo ? "synchronized (replay)" : "alternating frames");
    using hw2vr::hotkeys::key_name;
    HW2VR_LOG("hotkeys (Ctrl+Shift+): %s VR on/off%s, %s recentre, %s/%s world scale, %s flat camera spike",
              key_name(cfg.key_vr_toggle), cfg.vr_autostart ? " (autostart on)" : "", key_name(cfg.key_recentre),
              key_name(cfg.key_scale_up), key_name(cfg.key_scale_down), key_name(cfg.key_spike));
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
