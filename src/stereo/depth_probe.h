#pragma once

#include "common/vec3.h"

// The scene's depth, for the laser and for the game's 2D world markers. The game draws its 3D scene into a
// multisampled framebuffer (fbo 1 on build 12653478: 16x MSAA, DEPTH_COMPONENT32 renderbuffer). After the game has
// rendered an eye, that depth is resolved into our own texture and small shaders march rays through it:
//   - the laser: the hand's ray (game view space), looking for the first point on, or just behind, a visible
//     surface;
//   - the marker grid: one ray per ~20-pixel cell of the game's screen, from the game camera, giving how far away
//     the scene is behind every part of the screen. The game positions its 2D markers (selection circles, brackets,
//     health bars, names) on the screen over their ships; the grid says how deep to put them.
// Results come back without stalling (fence + pixel-pack buffer), usually by the next frame.
namespace hw2vr::depth {

// After WaitGetPoses: the camera of the eye the game renders next (game view -> eye view, eye projection) and the
// laser ray to test in it (game view space, game units, dir normalised).
void set_camera(const float eye_from_game[16], const float eye_projection[16]);
void set_ray(const Vec3& origin, const Vec3& dir);
void clear_ray();

// Before the swap: after the game's draws, before the second-eye replay overwrites the depth.
// width/height: the scene framebuffer's size (the game window's).
void run(int width, int height);

enum class Result { None, Hit, Miss };
// The newest finished laser probe (for the ray set a frame or two ago): distance along that ray in game units.
Result latest(float* distance);

// After the swap: pick up a finished marker grid.
void poll_grid();
// View-space depth (game units, positive) of the scene behind a screen rectangle {x0, y0, x1, y1} in [0,1]
// (y up): the nearest surface in its middle half, else anywhere in it (slightly enlarged). False = empty space.
bool anchor_view_depth(const float bounds[4], float* view_depth);

void release();  // VR off: frees the depth copy

struct TestState {
    unsigned probes, results, hits;  // laser, since VR started
    unsigned grids;                  // marker grids read back
};
TestState test_state();

}  // namespace hw2vr::depth
