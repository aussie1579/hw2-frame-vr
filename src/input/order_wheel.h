#pragma once

#include "common/vec3.h"

// The order wheel: hold the right bumper and a radial menu of orders opens between you and what the laser points
// at; point the laser at a slice and let go to give that order to the selected ships, on that target. Our own
// panel (input/ui_canvas.h); the orders themselves are the game's hotkeys and modifier-clicks (input/controls.cpp).
namespace hw2vr::wheel {

enum Order { kMove, kAttack, kGuard, kDock, kStop, kRepair, kHarvest, kSpecial, kOrderCount };

void start();
void stop();

// Opens `distance` metres from the head towards `toward`, facing the head.
void open(const Vec3& head, const Vec3& toward, float distance = 0.6f);
bool is_open();
// The laser over the wheel: highlights the slice it points at. Returns the beam length to the wheel (0 = misses).
float point(const Vec3& origin, const Vec3& dir);
// Closes it; the order chosen, or -1 (centre, or not on the wheel).
int close();
const wchar_t* label(int order);

}  // namespace hw2vr::wheel
