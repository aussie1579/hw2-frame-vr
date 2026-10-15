#include "input/command_menus.h"

#include <cwchar>
#include <string>
#include <vector>

#include "assets/game_assets.h"
#include "common/log.h"
#include "input/key_names.h"
#include "input/macro.h"
#include "input/radial_menu.h"

namespace hw2vr::menus {
namespace {

enum class Act {
    Tap,            // the hotkey
    ModeThenClick,  // a mode key, then a click on the target (Move: M, then "there")
    ModifierClick,  // a click on the target with the key held (Attack: Ctrl+click, Guard: G+click)
};

struct Entry {
    const wchar_t* label;
    assets::IconRef icon;  // no name: the label only
    const wchar_t* keys;
    Act act = Act::Tap;
};

struct Def {
    const char* name;
    const wchar_t* title;
    std::vector<Entry> items;  // clockwise from the top
};

#define CMD "ui\\newui\\taskbar\\commandicons\\"
#define FORM "ui\\newui\\taskbar\\formationicons\\"
#define HUD "ui\\newui\\taskbar\\hudicons\\"

// HW2 keys (scripts/keybindings.lua, docs/FINDINGS.md "Input"). Icon cells: assets::IconRef - the stance and HUD
// strips' normal cells are picked to be lit (their own "normal" is dim), as the game shows them under the cursor.
const Def kMenus[kMenuCount] = {
    {"orders", L"ORDERS",
     {{L"Move", {CMD "cmd_ico_move.dds"}, L"M", Act::ModeThenClick},
      {L"Attack move", {CMD "cmd_ico_moveatk.dds"}, L"Ctrl+A", Act::ModeThenClick},
      {L"Attack", {CMD "cmd_ico_attack.dds"}, L"Ctrl", Act::ModifierClick},
      {L"Guard", {CMD "cmd_ico_guard.dds"}, L"G", Act::ModifierClick},
      {L"Dock", {CMD "cmd_ico_dock.dds"}, L"D"},
      {L"Stop", {CMD "cmd_ico_stop.dds"}, L"S"},
      {L"Repair", {CMD "cmd_ico_repair.dds"}, L"Y", Act::ModifierClick},
      {L"Harvest", {CMD "cmd_ico_resource.dds"}, L"H", Act::ModifierClick},
      {L"Special", {CMD "cmd_ico_specialattack.dds"}, L"Z", Act::ModifierClick}}},
    // Tactics on the right half, stances on the left.
    {"tactics", L"TACTICS",
     {{L"Aggressive", {CMD "cmd_ico_aggressive.dds"}, L"F2"},
      {L"Defensive", {CMD "cmd_ico_defensive.dds"}, L"F3"},
      {L"Passive", {CMD "cmd_ico_passive.dds"}, L"F4"},
      {L"Damage", {CMD "cmd_ico_stance_dmg.dds", 128, 2}, L"F5"},
      {L"Evasive", {CMD "cmd_ico_stance_eva.dds", 128, 2}, L"F6"},
      {L"Neutral", {CMD "cmd_ico_stance_neu.dds", 128, 2}, L"F7"}}},
    {"formations", L"FORMATION",
     {{L"Phalanx", {FORM "form_ico_capital_phalanx.dds"}, L"Shift+F1"},
      {L"Line", {FORM "form_ico_frigate_line.dds"}, L"Shift+F2"},
      {L"Screen", {FORM "form_ico_fighter_screen.dds"}, L"Shift+F3"},
      {L"Delta", {FORM "form_ico_delta.dds"}, L"Shift+F4"},
      {L"Broad", {FORM "form_ico_broad.dds"}, L"Shift+F5"},
      {L"X", {FORM "form_ico_x.dds"}, L"Shift+F6"},
      {L"Movers", {}, L"Shift+F7"},
      {L"Wall", {FORM "form_ico_wall.dds"}, L"Shift+F8"},
      {L"Sphere", {FORM "form_ico_sphere.dds"}, L"Shift+F9"},
      {L"Leave group", {}, L"Slash"}}},
    {"production", L"FLEET",
     {{L"Build", {}, L"B"},
      {L"Research", {}, L"R"},
      {L"Launch", {}, L"L"},
      {L"Build queue", {HUD "hud_global_queue.dds", 128, 2}, L"Q"},
      {L"Menu", {HUD "menu_ico_main.dds", 128, 3}, L"F10"},
      {L"Events", {HUD "hud_events.dds", 128, 2}, L"V"},
      {L"Objectives", {"ui\\newui\\taskbar\\objectiveicon.tga", 0, 0}, L"O"},
      {L"Sensors", {HUD "menu_ico_sensors4k.dds", 136, 1}, L"Space"}}},
};

#undef CMD
#undef FORM
#undef HUD

int g_radial[kMenuCount] = {-1, -1, -1, -1};

}  // namespace

int parse(const wchar_t* name) {
    for (int i = 0; i < kMenuCount; ++i) {
        const std::string n = kMenus[i].name;
        if (_wcsicmp(name, std::wstring(n.begin(), n.end()).c_str()) == 0) return i;
    }
    return -1;
}

const char* name(int menu) { return menu >= 0 && menu < kMenuCount ? kMenus[menu].name : "?"; }

void register_menus() {
    if (g_radial[0] >= 0) return;
    for (int i = 0; i < kMenuCount; ++i) {
        radial::Menu m;
        m.title = kMenus[i].title;
        for (const Entry& e : kMenus[i].items) m.items.push_back({e.label, e.icon});
        g_radial[i] = radial::add(m);
    }
}

int radial_id(int menu) { return menu >= 0 && menu < kMenuCount ? g_radial[menu] : -1; }

const wchar_t* label(int menu, int item) {
    if (menu < 0 || menu >= kMenuCount || item < 0 || item >= static_cast<int>(kMenus[menu].items.size())) return L"none";
    return kMenus[menu].items[item].label;
}

bool run(int menu, int item, HWND window, int x, int y, bool target) {
    if (menu < 0 || menu >= kMenuCount || item < 0 || item >= static_cast<int>(kMenus[menu].items.size())) return false;
    const Entry& e = kMenus[menu].items[item];
    keys::Binding b;
    if (!keys::parse_binding(e.keys, &b) || b.kind != keys::Binding::Keys) return false;
    switch (e.act) {
        case Act::Tap: macro::tap(b); break;
        case Act::ModeThenClick:
            if (target) macro::key_then_click(b, window, x, y);
            else macro::tap(b);  // the mode only: the next click places it
            break;
        case Act::ModifierClick:
            if (!target) {
                HW2VR_LOG("menus: %s > %ls needs a target - point the laser at it before opening the menu", kMenus[menu].name,
                          e.label);
                return false;
            }
            macro::click_with(b, window, x, y);
            break;
    }
    HW2VR_LOG("menus: %s > %ls%s", kMenus[menu].name, e.label,
              e.act == Act::Tap ? "" : target ? " (on the target)" : " (mode only: click to place it)");
    return true;
}

}  // namespace hw2vr::menus
