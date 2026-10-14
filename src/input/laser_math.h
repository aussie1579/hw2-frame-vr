#pragma once

// Laser <-> game mouse maths (pure, covered by tests/math_test.cpp).
// The game camera sits at the recentre pose (stereo/eye_math.h), so a seated-space direction d is the game-view
// direction inverse(camera_pose) * d. The game picks along the ray from its camera through the mouse pixel, so a
// laser pointing into the world maps to the pixel its direction projects to, and the cursor drawn at that pixel
// lies back along the same direction.
#include <algorithm>

#include "common/vec3.h"

namespace hw2vr::input {

// Game view-space direction (-Z forward) -> client pixel (x right, y down) through the game projection
// (column-major, perspective). False if the direction points behind the camera.
inline bool project_direction(const float proj[16], const Vec3& v, int width, int height, float* px, float* py) {
    if (v.z > -1e-4f) return false;
    const float w = -v.z;
    const float ndc_x = (proj[0] * v.x + proj[4] * v.y + proj[8] * v.z) / w;
    const float ndc_y = (proj[1] * v.x + proj[5] * v.y + proj[9] * v.z) / w;
    *px = (ndc_x + 1.0f) * 0.5f * static_cast<float>(width);
    *py = (1.0f - ndc_y) * 0.5f * static_cast<float>(height);
    return true;
}

// Client pixel -> game view-space direction (not normalised, z = -1). Inverse of project_direction for a
// projection without x/y shear (proj[4] = proj[1] = 0), which the game's is.
inline Vec3 unproject_pixel(const float proj[16], float px, float py, int width, int height) {
    const float ndc_x = px / static_cast<float>(width) * 2.0f - 1.0f;
    const float ndc_y = 1.0f - py / static_cast<float>(height) * 2.0f;
    return {(ndc_x + proj[8]) / proj[0], (ndc_y + proj[9]) / proj[5], -1.0f};
}

// Keeps a pixel `margin` (fraction of the size) inside the window, away from the game's edge-pan zones.
inline bool clamp_inside(float* px, float* py, int width, int height, float margin) {
    const float mx = margin * width, my = margin * height;
    const float cx = std::clamp(*px, mx, width - 1.0f - mx), cy = std::clamp(*py, my, height - 1.0f - my);
    const bool inside = cx == *px && cy == *py;
    *px = cx;
    *py = cy;
    return inside;
}

}  // namespace hw2vr::input
