#pragma once

// Minimal 3-vector maths for the laser and the VR panel (seated tracking space, metres, OpenVR axes: +X right,
// +Y up, -Z forward). Poses are column-major 4x4 (common/mat4.h).
#include <cmath>

namespace hw2vr {

struct Vec3 {
    float x = 0.0f, y = 0.0f, z = 0.0f;

    Vec3 operator+(const Vec3& o) const { return {x + o.x, y + o.y, z + o.z}; }
    Vec3 operator-(const Vec3& o) const { return {x - o.x, y - o.y, z - o.z}; }
    Vec3 operator-() const { return {-x, -y, -z}; }
    Vec3 operator*(float s) const { return {x * s, y * s, z * s}; }
    Vec3& operator+=(const Vec3& o) { return *this = *this + o; }
};

inline float dot(const Vec3& a, const Vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline Vec3 cross(const Vec3& a, const Vec3& b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
inline float length(const Vec3& v) { return std::sqrt(dot(v, v)); }
inline Vec3 normalize(const Vec3& v) {
    const float l = length(v);
    return l > 1e-12f ? v * (1.0f / l) : Vec3{};
}

// Columns of a column-major pose.
inline Vec3 axis_x(const float m[16]) { return {m[0], m[1], m[2]}; }
inline Vec3 axis_y(const float m[16]) { return {m[4], m[5], m[6]}; }
inline Vec3 axis_z(const float m[16]) { return {m[8], m[9], m[10]}; }
inline Vec3 position(const float m[16]) { return {m[12], m[13], m[14]}; }

// Pose from orthonormal axes and a position.
inline void pose_from_axes(const Vec3& x, const Vec3& y, const Vec3& z, const Vec3& p, float out[16]) {
    const float m[16] = {x.x, x.y, x.z, 0, y.x, y.y, y.z, 0, z.x, z.y, z.z, 0, p.x, p.y, p.z, 1};
    for (int i = 0; i < 16; ++i) out[i] = m[i];
}

// Rotation part of a column-major matrix applied to a direction.
inline Vec3 rotate(const float m[16], const Vec3& v) {
    return {m[0] * v.x + m[4] * v.y + m[8] * v.z, m[1] * v.x + m[5] * v.y + m[9] * v.z,
            m[2] * v.x + m[6] * v.y + m[10] * v.z};
}
inline Vec3 transform_point(const float m[16], const Vec3& p) { return rotate(m, p) + position(m); }

}  // namespace hw2vr
