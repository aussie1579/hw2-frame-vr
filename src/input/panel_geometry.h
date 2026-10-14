#pragma once

// Where the HUD panel is in the room, flat or curved, as plain maths with a known orientation (UV (0,0) = top-left,
// matching window pixels), so the laser hit test never depends on SteamVR's own hit test. Ported from the Factorio
// VR project, whose curved maths matched ComputeOverlayIntersection to 0.00 cm on 25 rays.
//
// Curved panels match SteamVR's overlay curvature: the overlay bends round a vertical axis Radius() in front of its
// centre (on the viewer's side), the width is the arc length, and curvature = width / (2 pi radius).
#include <algorithm>
#include <cmath>

#include "common/vec3.h"

namespace hw2vr::input {

struct PanelGeometry {
    Vec3 centre, right{1, 0, 0}, up{0, 1, 0}, normal{0, 0, 1};  // normal points back at the viewer (overlay +Z)
    float width = 0.0f, height = 0.0f;
    float curvature = 0.0f;  // 0 = flat

    static constexpr float kFlat = 1e-4f;
    static constexpr float kPi = 3.14159265358979f;

    bool curved() const { return curvature > kFlat; }
    float radius() const { return width / (2.0f * kPi * curvature); }

    // Level forward/right of a pose (yaw only), so a tilted head never tips the panel.
    static void level_axes(const float pose[16], Vec3* forward, Vec3* right) {
        Vec3 f = -axis_z(pose);  // OpenVR forward is -Z
        f.y = 0.0f;
        f = dot(f, f) < 1e-6f ? Vec3{0, 0, -1} : normalize(f);
        *forward = f;
        *right = cross(f, Vec3{0, 1, 0});
    }

    // Panel `distance` ahead of an anchor pose, `side` to the right and `height` up, turned to face the anchor's
    // position (tilted towards it when above or below), edges level.
    static PanelGeometry in_front(const float anchor[16], float distance, float side, float height, float width,
                                  float aspect_h_over_w, float curvature) {
        Vec3 forward, right_of_anchor;
        level_axes(anchor, &forward, &right_of_anchor);
        PanelGeometry g;
        const Vec3 eye = position(anchor);
        g.centre = eye + forward * distance + right_of_anchor * side + Vec3{0, height, 0};
        const Vec3 to_eye = eye - g.centre;
        g.normal = dot(to_eye, to_eye) < 1e-6f ? -forward : normalize(to_eye);
        g.right = normalize(cross(Vec3{0, 1, 0}, g.normal));
        g.up = cross(g.normal, g.right);
        g.width = width;
        g.height = width * aspect_h_over_w;
        g.curvature = curvature;
        return g;
    }

    // Inverse of in_front for a point: (distance ahead, side, height) relative to the anchor.
    static void relative(const float anchor[16], const Vec3& point, float* distance, float* side, float* height) {
        Vec3 forward, right;
        level_axes(anchor, &forward, &right);
        const Vec3 offset = point - position(anchor);
        *distance = dot(offset, forward);
        *side = dot(offset, right);
        *height = offset.y;
    }

    // Ray against the front face; uv 0..1 from the top-left.
    bool intersect(const Vec3& origin, const Vec3& dir, float uv[2], float* distance) const {
        if (width <= 0.0f || height <= 0.0f) return false;
        return curved() ? intersect_curved(origin, dir, uv, distance) : intersect_flat(origin, dir, uv, distance);
    }

    Vec3 point_at(float u, float v) const {
        const float y = (0.5f - v) * height;
        if (!curved()) return centre + right * ((u - 0.5f) * width) + up * y;
        const float r = radius(), angle = (u - 0.5f) * width / r;
        return centre + right * (r * std::sin(angle)) + up * y + normal * (r * (1.0f - std::cos(angle)));
    }

    // Pose of a flat sprite lying on the surface at uv, lifted `lift` metres towards the viewer.
    void pose_at(float u, float v, float lift, float out[16]) const {
        Vec3 r = right, n = normal;
        if (curved()) {
            const float angle = (u - 0.5f) * width / radius();
            r = right * std::cos(angle) + normal * std::sin(angle);
            n = normal * std::cos(angle) - right * std::sin(angle);
        }
        pose_from_axes(r, up, n, point_at(u, v) + n * lift, out);
    }

    // Pose of the panel itself (SteamVR applies the curvature around it).
    void pose(float out[16]) const { pose_from_axes(right, up, normal, centre, out); }

    // About how many headset pixels the panel spans seen from `eye` at `pixels_per_degree`; 0 if that is 0.
    int pixels_across(const Vec3& eye, float pixels_per_degree) const {
        if (pixels_per_degree <= 0.0f || width <= 0.0f) return 0;
        const float d = std::max(length(eye - centre), 0.1f);
        return static_cast<int>(std::ceil(pixels_per_degree * (180.0f / kPi) * width / d));
    }

private:
    bool intersect_flat(const Vec3& origin, const Vec3& dir, float uv[2], float* distance) const {
        const float facing = dot(dir, normal);
        if (facing > -1e-4f) return false;  // parallel or from behind
        const float t = dot(centre - origin, normal) / facing;
        if (t <= 0.0f) return false;
        const Vec3 offset = origin + dir * t - centre;
        uv[0] = dot(offset, right) / width + 0.5f;
        uv[1] = 0.5f - dot(offset, up) / height;
        *distance = t;
        return uv[0] >= 0.0f && uv[0] <= 1.0f && uv[1] >= 0.0f && uv[1] <= 1.0f;
    }

    // In the panel's frame (x right, y up, z towards the viewer, origin at the centre) the surface is
    // x = r sin(a), z = r (1 - cos(a)): a cylinder of radius r round the axis x = 0, z = r.
    bool intersect_curved(const Vec3& origin, const Vec3& dir, float uv[2], float* distance) const {
        const float r = radius();
        const Vec3 rel = origin - centre;
        const float ox = dot(rel, right), oy = dot(rel, up), oz = dot(rel, normal) - r;
        const float dx = dot(dir, right), dy = dot(dir, up), dz = dot(dir, normal);
        const float a = dx * dx + dz * dz;
        if (a < 1e-9f) return false;  // parallel to the axis
        const float b = 2.0f * (ox * dx + oz * dz), c = ox * ox + oz * oz - r * r;
        const float disc = b * b - 4.0f * a * c;
        if (disc < 0.0f) return false;
        const float sq = std::sqrt(disc), half_angle = width / (2.0f * r);
        for (const float t : {(-b - sq) / (2.0f * a), (-b + sq) / (2.0f * a)}) {  // nearest first
            if (t <= 1e-5f) continue;
            const float px = ox + t * dx, py = oy + t * dy, pz = oz + t * dz;  // pz relative to the axis
            const float angle = std::atan2(px, -pz);
            if (std::fabs(angle) > half_angle || std::fabs(py) > height / 2.0f) continue;
            // Front face only: the surface normal points at the axis and the ray must come against it.
            const float nx = -std::sin(angle), nz = std::cos(angle);
            if (dx * nx + dz * nz >= 0.0f) continue;
            *distance = t;
            uv[0] = angle * r / width + 0.5f;
            uv[1] = 0.5f - py / height;
            return true;
        }
        return false;
    }
};

}  // namespace hw2vr::input
