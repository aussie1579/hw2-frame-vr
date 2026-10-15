#pragma once

#include <string>
#include <vector>

#include "assets/game_assets.h"
#include "common/vec3.h"

// Radial menus (the D-pad submenus, the order menu): opened between the player and what the laser points at,
// facing them; the laser highlights a slice. Each menu is drawn once, with the game's own icons
// (assets/game_assets.h); a highlight wedge turned round the centre marks the slice, so pointing never re-draws.
namespace hw2vr::radial {

struct Item {
    std::wstring label;
    assets::IconRef icon;  // no name: the label only
};

struct Menu {
    std::wstring title;  // in the centre
    std::vector<Item> items;
};

int add(const Menu& menu);  // before start(): its id
void start();               // draws every menu (loads the game's icons)
void stop();

void open(int menu, const Vec3& head, const Vec3& toward, float distance = 0.6f);
bool is_open();
int open_menu();  // -1 if none
// The laser over the open menu: highlights the slice it points at. Returns the beam length to it (0 = misses).
float point(const Vec3& origin, const Vec3& dir);
int selected();  // the highlighted slice, -1 = none (the centre)
// Closes it; the item chosen, or -1 (centre, or not on the menu).
int close();

// Tests: a menu's image (highlight -1 = none).
bool test_render(int menu, int highlight, std::vector<unsigned>* pixels, int* size);

}  // namespace hw2vr::radial
