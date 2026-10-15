#pragma once

#include "common/vec3.h"

// Where the laser touches the scene. The game draws its 3D scene into a multisampled framebuffer (fbo 1 on build
// 12653478: 16x MSAA, DEPTH_COMPONENT32 renderbuffer). After the game has rendered an eye, that depth is resolved
// into our own texture and a small shader marches the laser ray (game view space) through it, looking for the
// first point that lies on, or just behind, a visible surface. The result comes back without stalling (fence +
// pixel-pack buffer), usually by the next frame.
namespace hw2vr::depth {

// After WaitGetPoses: the camera of the eye the game renders next (game view -> eye view, eye projection) and the
// ray to test in it (game view space, game units, dir normalised).
void set_camera(const float eye_from_game[16], const float eye_projection[16]);
void set_ray(const Vec3& origin, const Vec3& dir);
void clear_ray();

// Before the swap: after the game's draws, before the second-eye replay overwrites the depth.
// width/height: the scene framebuffer's size (the game window's).
void run(int width, int height);

enum class Result { None, Hit, Miss };
// The newest finished probe (for the ray set a frame or two ago): distance along that ray in game units if Hit.
Result latest(float* distance);

void release();  // VR off: frees the depth copy

struct TestState {
    unsigned probes, results, hits;  // since VR started
};
TestState test_state();

}  // namespace hw2vr::depth
