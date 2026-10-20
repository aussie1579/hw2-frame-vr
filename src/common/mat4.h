#pragma once

// Column-major 4x4 float matrices, the layout the game uploads with glUniformMatrix4fv(transpose = GL_FALSE).
// Element (row r, column c) is m[c * 4 + r]; translation lives in m[12..14].
#include <cmath>
#include <cstring>

namespace hw2vr::mat4 {

inline void identity(float out[16]) {
    static constexpr float kIdentity[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    std::memcpy(out, kIdentity, sizeof kIdentity);
}

// out = a * b (out may alias neither input).
inline void multiply(const float a[16], const float b[16], float out[16]) {
    for (int c = 0; c < 4; ++c)
        for (int r = 0; r < 4; ++r) {
            float s = 0.0f;
            for (int k = 0; k < 4; ++k) s += a[k * 4 + r] * b[c * 4 + k];
            out[c * 4 + r] = s;
        }
}

// Inverse of a rotation + translation matrix.
inline void rigid_inverse(const float in[16], float out[16]) {
    identity(out);
    for (int r = 0; r < 3; ++r)
        for (int c = 0; c < 3; ++c) out[c * 4 + r] = in[r * 4 + c];  // transpose the 3x3
    for (int r = 0; r < 3; ++r)
        out[12 + r] = -(out[r] * in[12] + out[4 + r] * in[13] + out[8 + r] * in[14]);
}

// OpenVR HmdMatrix34_t (row-major 3x4) <-> column-major 4x4.
inline void from_rows3x4(const float rows[3][4], float out[16]) {
    identity(out);
    for (int r = 0; r < 3; ++r)
        for (int c = 0; c < 4; ++c) out[c * 4 + r] = rows[r][c];
}
inline void to_rows3x4(const float in[16], float rows[3][4]) {
    for (int r = 0; r < 3; ++r)
        for (int c = 0; c < 4; ++c) rows[r][c] = in[c * 4 + r];
}

inline void rotation_y(float radians, float out[16]) {
    identity(out);
    const float c = std::cos(radians), s = std::sin(radians);
    out[0] = c;
    out[2] = -s;
    out[8] = s;
    out[10] = c;
}

// Rotation about +X: positive turns -Z (forward) towards +Y (up).
inline void rotation_x(float radians, float out[16]) {
    identity(out);
    const float c = std::cos(radians), s = std::sin(radians);
    out[5] = c;
    out[6] = s;
    out[9] = -s;
    out[10] = c;
}

inline void translation(float x, float y, float z, float out[16]) {
    identity(out);
    out[12] = x;
    out[13] = y;
    out[14] = z;
}

// Yaw (rotation about +Y) of a pose: the heading of its Z axis in the XZ plane, so yaw_of(rotation_y(a)) == a.
inline float yaw_of(const float pose[16]) { return std::atan2(pose[8], pose[10]); }

// glUniform-style perspective matrix (w = -z), as opposed to ortho (w = 1).
inline bool is_perspective(const float m[16]) { return std::fabs(m[11] + 1.0f) < 1e-4f && std::fabs(m[15]) < 1e-4f; }

}  // namespace hw2vr::mat4
