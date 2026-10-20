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

// How far the game camera turns to look where the head looks (docs/FINDINGS.md "Game camera turn"): the head's yaw
// and pitch, no roll (the game's screen stays level), as a rotation in the recentred frame - the game camera's view
// space before the turn. `head`: the head pose in that frame. Looking (nearly) straight up or down, the forward vector
// has little heading left, so the up vector, tipped over the same way, takes over (only there: elsewhere it would
// let head roll leak into the yaw).
inline void game_camera_turn(const float head[16], float out[16]) {
    const float fx = -head[8], fy = -head[9], fz = -head[10];  // forward = -Z axis
    const float level = std::sqrt(fx * fx + fz * fz);           // cos(pitch)
    const float w = level > 0.3f ? 0.0f : (0.3f - level) / 0.3f;
    const float hx = fx - w * fy * head[4], hz = fz - w * fy * head[6];
    const float yaw = std::atan2(-hx, -hz);
    const float pitch = std::asin(fy < -1.0f ? -1.0f : fy > 1.0f ? 1.0f : fy);
    float y[16], x[16];
    mat4::rotation_y(yaw, y);
    mat4::rotation_x(pitch, x);
    mat4::multiply(y, x, out);
}

// A screen rectangle {x0, y0, x1, y1} in [0,1] (y up) of one game camera, as another camera at the same place sees
// it: `rot` turns view-space directions of the first into the second's; both project with p = {P0, P5, P8, P9}. Out:
// the bounding box of the turned corners. False if a corner falls behind the second camera.
inline bool turn_screen_rect(const float b[4], const float p[4], const float rot[16], float out[4]) {
    out[0] = out[1] = 1e30f;
    out[2] = out[3] = -1e30f;
    for (int k = 0; k < 4; ++k) {
        const float u = b[(k & 1) ? 2 : 0], v = b[(k & 2) ? 3 : 1];
        const float x = (2.0f * u - 1.0f + p[2]) / p[0], y = (2.0f * v - 1.0f + p[3]) / p[1], z = -1.0f;
        const float tx = rot[0] * x + rot[4] * y + rot[8] * z, ty = rot[1] * x + rot[5] * y + rot[9] * z;
        const float tz = rot[2] * x + rot[6] * y + rot[10] * z;
        if (tz > -1e-4f) return false;
        const float su = (p[0] * tx / -tz - p[2] + 1.0f) * 0.5f, sv = (p[1] * ty / -tz - p[3] + 1.0f) * 0.5f;
        out[0] = su < out[0] ? su : out[0];
        out[2] = su > out[2] ? su : out[2];
        out[1] = sv < out[1] ? sv : out[1];
        out[3] = sv > out[3] ? sv : out[3];
    }
    return true;
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
