#pragma once

#include <windows.h>

// The command menus on the left D-pad: hold a direction, point the laser at a slice, let go (or click it with the
// bumper). Orders, tactics and stances, strike-group formations, production and the game's screens - radial menus
// (input/radial_menu.h) with the game's own icons. Every slice is the game's own hotkey (scripts/keybindings.lua),
// the targeted orders with a click on what the laser pointed at when the menu opened.
namespace hw2vr::menus {

enum Id { kOrders, kTactics, kFormations, kProduction, kMenuCount };

int parse(const wchar_t* name);  // "orders", "tactics", "formations", "production" -> Id; -1 if unknown
const char* name(int menu);

// Adds the menus to radial:: (once; before radial::start, or for test renders).
void register_menus();
int radial_id(int menu);  // the menu's radial:: id, -1 before register_menus()

// Runs a slice: its hotkey, or a mode key / held modifier with a click at client pixel (x, y). Without a target
// (the laser wasn't on the world when the menu opened) Move and Attack move only enter their mode - the next
// click places them - and the other targeted orders do nothing. False = nothing sent.
bool run(int menu, int item, HWND window, int x, int y, bool target);
const wchar_t* label(int menu, int item);

// The build / research / launch window share one place on the screen: the one opened last (from the fleet menu)
// is closed by its own toggle key (the window's Close tab, hud/screen_windows.h).
void close_manager();

}  // namespace hw2vr::menus
