#pragma once

#include <string>
#include <vector>

// Procedural images for the VR controls (straight alpha, 0xAARRGGBB words, rows top to bottom), uploaded with
// gl::upload_bgra. Ported from the Factorio VR project's overlays.
namespace hw2vr::sprites {

struct Image {
    int width = 0, height = 0;
    std::vector<unsigned> pixels;
};

Image beam();      // light blue strip fading towards the far end (length along x)
Image grab_bar();  // light capsule with a dark rim
// Help text on a dark panel: first line is an orange title, the rest white. GDI, Segoe UI.
Image text_panel(const std::wstring& text, int width_px);

}  // namespace hw2vr::sprites
