#pragma once

// Pure per-eye camera maths (no GL, no OpenVR), shared by the VR frame loop and tests/math_test.cpp.
#include "common/mat4.h"

namespace hw2vr::stereo {

struct Frustum {
    float left, right, down, up;  // tangents of the half-angles (left/down negative)
};

// The game camera sits at the recentre pose; everything the head does relative to that is applied in view space.
// E maps game view space -> eye view space:  E = inverse(recentred_head * eye_to_head), translations in metres
// scaled by world_scale (game units per real metre).
inline void eye_from_game_camera(const float hmd_pose[16], const float recentre_inverse[16], const float eye_to_head[16],
                                 float world_scale, float out[16]) {
    float head[16], eye_offset[16], eye_pose[16];
    mat4::multiply(recentre_inverse, hmd_pose, head);
    for (int i = 12; i < 15; ++i) head[i] *= world_scale;
    std::memcpy(eye_offset, eye_to_head, sizeof eye_offset);
    for (int i = 12; i < 15; ++i) eye_offset[i] *= world_scale;
    mat4::multiply(head, eye_offset, eye_pose);
    mat4::rigid_inverse(eye_pose, out);
}

// Recentring keeps only the head's yaw and position, so the horizon stays level.
inline void recentre_inverse_from(const float hmd_pose[16], float out[16]) {
    float yaw[16], pose[16];
    mat4::rotation_y(mat4::yaw_of(hmd_pose), yaw);
    std::memcpy(pose, yaw, sizeof pose);
    pose[12] = hmd_pose[12];
    pose[13] = hmd_pose[13];
    pose[14] = hmd_pose[14];
    mat4::rigid_inverse(pose, out);
}

// The game's perspective matrix with its x/y rows replaced by the eye frustum. The depth row (near plane,
// infinite or finite far) is kept, so every pass keeps its own depth behaviour.
inline void eye_projection(const float game[16], const Frustum& f, float out[16]) {
    std::memcpy(out, game, 16 * sizeof(float));
    out[0] = 2.0f / (f.right - f.left);
    out[4] = 0.0f;
    out[8] = (f.right + f.left) / (f.right - f.left);
    out[1] = 0.0f;
    out[5] = 2.0f / (f.up - f.down);
    out[9] = (f.up + f.down) / (f.up - f.down);
}

}  // namespace hw2vr::stereo
