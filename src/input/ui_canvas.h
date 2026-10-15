#pragma once

#include <string>
#include <vector>

#include "input/sprites.h"

// Our own VR panels (the wrist tablet, the order wheel), drawn on the CPU: rounded shapes per pixel, text with GDI.
// Images are straight alpha, rows top to bottom (sprites::Image), re-rendered only when something on them changes.
namespace hw2vr::ui {

struct Rect {
    float x0, y0, x1, y1;  // panel uv, top-left origin
    bool contains(float u, float v) const { return u >= x0 && u <= x1 && v >= y0 && v <= y1; }
};

enum class Kind { Button, Label, Title };

struct Item {
    Rect rect;
    std::wstring text;
    Kind kind = Kind::Button;
    bool hover = false, pressed = false, lit = false;
};

// A dark rounded panel with its buttons and labels.
sprites::Image render_panel(int width, int height, const std::vector<Item>& items);

// The index of the button under (u, v), -1 if none (labels never count).
int hit(const std::vector<Item>& items, float u, float v);

// A radial menu: labels.size() slices clockwise from the top round a centre label. highlight: a slice index,
// labels.size() for the centre, -1 for none.
sprites::Image render_wheel(int size, const std::vector<std::wstring>& labels, const std::wstring& centre, int highlight);

// Which part of a wheel a point is on: x right, y up, in wheel radii from its centre. The slice index, -1 for the
// centre (inside `inner`), -2 outside the wheel.
int wheel_slice(float x, float y, int slices, float inner);
constexpr float kWheelInner = 0.32f;  // centre disc radius, in wheel radii

}  // namespace hw2vr::ui
