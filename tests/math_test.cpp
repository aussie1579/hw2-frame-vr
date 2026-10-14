// Unit tests for the per-eye camera maths (src/stereo/eye_math.h, src/common/mat4.h). No GL, no headset.
#include <cmath>
#include <cstdio>
#include <utility>

#include "stereo/eye_math.h"

using namespace hw2vr;

namespace {

int g_failures = 0;
void check(bool ok, const char* what) {
    std::printf("[%s] %s\n", ok ? " OK " : "FAIL", what);
    if (!ok) ++g_failures;
}
bool near(float a, float b, float eps = 1e-4f) { return std::fabs(a - b) <= eps * (1.0f + std::fabs(b)); }
bool near16(const float* a, const float* b) {
    for (int i = 0; i < 16; ++i)
        if (!near(a[i], b[i])) return false;
    return true;
}
void transform(const float m[16], const float p[4], float out[4]) {
    for (int r = 0; r < 4; ++r) out[r] = m[r] * p[0] + m[4 + r] * p[1] + m[8 + r] * p[2] + m[12 + r] * p[3];
}
void rotation_x(float a, float out[16]) {
    mat4::identity(out);
    out[5] = std::cos(a);
    out[6] = std::sin(a);
    out[9] = -std::sin(a);
    out[10] = std::cos(a);
}
constexpr float kDeg = 3.14159265f / 180.0f;

}  // namespace

int main() {
    float I[16], a[16], b[16], c[16];
    mat4::identity(I);

    // rigid_inverse undoes a rotation + translation
    float r[16], t[16], pose[16], inv[16];
    mat4::rotation_y(37 * kDeg, r);
    mat4::translation(1, -2, 3, t);
    mat4::multiply(t, r, pose);
    mat4::rigid_inverse(pose, inv);
    mat4::multiply(pose, inv, a);
    check(near16(a, I), "rigid_inverse(pose) * pose == identity");
    check(near(mat4::yaw_of(r), 37 * kDeg), "yaw_of(rotation_y(37 deg)) == 37 deg");

    // Eye separation: right eye 32 mm to the right, 100 game units per metre -> scene shifts 3.2 units left
    float eye_to_head[16], e[16];
    mat4::translation(0.032f, 0, 0, eye_to_head);
    stereo::eye_from_game_camera(I, I, eye_to_head, 100.0f, e);
    const float ahead[4] = {0, 0, -10, 1};
    float p[4];
    transform(e, ahead, p);
    check(near(p[0], -3.2f) && near(p[2], -10.0f), "right eye: point ahead shifts -3.2 units in x (IPD x world scale)");

    // Head yaw: turning the head left (+yaw) makes a point straight ahead appear to the right
    float head_left[16];
    mat4::rotation_y(10 * kDeg, head_left);
    stereo::eye_from_game_camera(head_left, I, I, 100.0f, e);
    transform(e, ahead, p);
    check(p[0] > 0 && near(std::atan2(p[0], -p[2]), 10 * kDeg), "head turned left 10 deg: point ahead appears 10 deg right");

    // Recentring removes yaw and position but keeps pitch (horizon stays level)
    float yaw[16], pitch[16], yp[16], head[16], recentre[16];
    mat4::rotation_y(30 * kDeg, yaw);
    rotation_x(10 * kDeg, pitch);
    mat4::translation(0.2f, 1.1f, -0.4f, t);
    mat4::multiply(yaw, pitch, yp);
    mat4::multiply(t, yp, head);
    stereo::recentre_inverse_from(head, recentre);
    stereo::eye_from_game_camera(head, recentre, I, 100.0f, e);
    mat4::rigid_inverse(pitch, b);
    check(near16(e, b), "recentred: only the pitch remains (no yaw, no translation)");

    // Head movement after recentring is scaled by world scale
    mat4::translation(0.2f + 0.5f, 1.1f, -0.4f, t);  // lean 0.5 m to the right of the recentre point
    mat4::multiply(t, yp, c);
    stereo::eye_from_game_camera(c, recentre, I, 100.0f, e);
    float lean_world[4];
    const float origin[4] = {0, 0, 0, 1};
    float e_inv[16];
    mat4::rigid_inverse(e, e_inv);
    transform(e_inv, origin, lean_world);  // eye position in game view space
    float expected_dir[4];
    const float right_half_m[4] = {50.0f, 0, 0, 0};
    float unyaw[16];
    mat4::rigid_inverse(yaw, unyaw);
    transform(unyaw, right_half_m, expected_dir);
    check(near(lean_world[0], expected_dir[0], 1e-3f) && near(lean_world[2], expected_dir[2], 1e-3f) &&
              near(lean_world[1], 0.0f, 1e-3f),
          "0.5 m lean -> eye moves 50 game units (in the recentred frame)");

    // Eye projection: frustum edges map to NDC +-1, depth row of the game projection kept
    const float game[16] = {0.5978f, 0, 0, 0, 0, 1.428f, 0, 0, 0, 0, -1, -1, 0, 0, -6, 0};
    const stereo::Frustum f{-1.2f, 0.8f, -1.1f, 0.9f};
    float proj[16];
    stereo::eye_projection(game, f, proj);
    auto ndc = [&](float xt, float yt, float z) {
        const float v[4] = {xt * -z, yt * -z, z, 1};
        float clip[4];
        transform(proj, v, clip);
        return std::pair<float, float>(clip[0] / clip[3], clip[1] / clip[3]);
    };
    check(near(ndc(0.8f, 0, -5).first, 1.0f) && near(ndc(-1.2f, 0, -5).first, -1.0f), "eye projection: left/right tangents -> NDC x -1/+1");
    check(near(ndc(0, 0.9f, -5).second, 1.0f) && near(ndc(0, -1.1f, -5).second, -1.0f), "eye projection: up/down tangents -> NDC y +1/-1");
    check(proj[10] == game[10] && proj[14] == game[14] && proj[11] == -1.0f && proj[15] == 0.0f,
          "eye projection keeps the game's depth row (near 3, infinite far)");

    std::printf("%s: %d failure(s)\n", g_failures ? "FAILED" : "PASSED", g_failures);
    return g_failures ? 1 : 0;
}
