// Unit tests for the per-eye camera maths (src/stereo/eye_math.h, src/common/mat4.h), the VR panel geometry
// (src/input/panel_geometry.h) and the laser <-> mouse mapping (src/input/laser_math.h). No GL, no headset.
#include <cmath>
#include <cstdio>
#include <utility>
#include <vector>

#include "hud/marker_memory.h"
#include "input/laser_math.h"
#include "input/ui_canvas.h"
#include "input/panel_geometry.h"
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

    // The game camera turn follows the head's yaw and pitch, without roll
    {
        float roll[16], head_yp[16], head_ypr[16], turn[16], fwd_t[4], fwd_h[4];
        const float forward[4] = {0, 0, -1, 0};
        float z[16];
        mat4::identity(z);
        const float rc = std::cos(20 * kDeg), rs = std::sin(20 * kDeg);
        z[0] = rc, z[1] = rs, z[4] = -rs, z[5] = rc;  // 20 deg roll about Z
        std::memcpy(roll, z, sizeof z);
        mat4::multiply(yaw, pitch, head_yp);       // 30 deg yaw, 10 deg pitch
        mat4::multiply(head_yp, roll, head_ypr);
        stereo::game_camera_turn(head_ypr, turn);
        check(near16(turn, head_yp), "camera turn = the head's yaw and pitch, roll dropped");
        transform(turn, forward, fwd_t);
        transform(head_ypr, forward, fwd_h);
        check(near(fwd_t[0], fwd_h[0], 1e-4f) && near(fwd_t[1], fwd_h[1], 1e-4f) && near(fwd_t[2], fwd_h[2], 1e-4f),
              "camera turn: the turned game camera looks where the head looks");

        // Looking straight up (90 deg pitch) after a 120 deg yaw: the heading comes from the up vector
        float y120[16], up90[16], head_up[16], expected[16];
        mat4::rotation_y(120 * kDeg, y120);
        mat4::rotation_x(90 * kDeg, up90);
        mat4::multiply(y120, up90, head_up);
        stereo::game_camera_turn(head_up, turn);
        mat4::multiply(y120, up90, expected);
        check(near16(turn, expected), "camera turn: straight up keeps the heading (120 deg)");

        // Turning the game camera and placing the eye relative to the turned camera leaves the picture unchanged:
        // eye(turned camera) * turn^-1 * view == eye(camera) * view
        float game_pose[16], game_inv[16], e_turned[16], e_plain[16], turn_inv[16], lhs[16], tmp[16];
        mat4::translation(0.1f, 1.2f, -0.3f, t);
        mat4::multiply(t, head_ypr, c);  // head pose (seated)
        mat4::identity(game_pose);       // recentred at the seated origin
        stereo::game_camera_turn(c, turn);
        mat4::multiply(game_pose, turn, tmp);
        mat4::rigid_inverse(tmp, game_inv);
        stereo::eye_from_game_camera(c, game_inv, eye_to_head, 100.0f, e_turned);
        stereo::eye_from_game_camera(c, I, eye_to_head, 100.0f, e_plain);
        mat4::rigid_inverse(turn, turn_inv);
        mat4::multiply(e_turned, turn_inv, lhs);
        check(near16(lhs, e_plain), "camera turn: eye placement compensates it exactly (same picture)");
    }

    // A marker rectangle mapped into the camera the depth grid was traced from (turned 10 deg to the right of it)
    {
        const float gp[4] = {0.5978f, 1.428f, 0.0f, 0.0f};
        const float rect[4] = {0.45f, 0.45f, 0.55f, 0.55f};
        float rot[16], out[4];
        mat4::identity(rot);
        check(stereo::turn_screen_rect(rect, gp, rot, out) && near(out[0], 0.45f) && near(out[3], 0.55f),
              "turn_screen_rect: no turn, same rectangle");
        mat4::rotation_y(10 * kDeg, rot);  // straight ahead now = 10 deg to the left for the grid's camera
        const float expected = (0.5978f * -std::tan(10 * kDeg) + 1.0f) * 0.5f;
        check(stereo::turn_screen_rect(rect, gp, rot, out) && near(0.5f * (out[0] + out[2]), expected, 2e-3f) &&
                  near(0.5f * (out[1] + out[3]), 0.5f, 1e-3f),
              "turn_screen_rect: a 10 deg turn moves the rectangle where the other camera sees it");
    }

    // World marker depth memory: a marker whose grid depth flips between two values each frame stays put; noise is
    // eased in; a real change is taken once it has held for 3 frames; a frame the grid misses keeps the depth.
    {
        hud::MarkerMemory mem;
        const Vec3 dir = normalize(Vec3{0.1f, 0.0f, -1.0f});
        const float size = 0.01f;
        float z = mem.resolve(dir, size, true, 2000.0f, 9999.0f);
        mem.end_frame();
        bool steady = near(z, 2000.0f);
        for (int i = 0; i < 6; ++i) {  // flips between the ship and something far behind it
            z = mem.resolve(dir, size, true, (i & 1) ? 2000.0f : 8000.0f, 9999.0f);
            mem.end_frame();
            steady = steady && near(z, 2000.0f, 1.0f);
        }
        check(steady, "marker memory: a depth flipping between two values every frame stays steady");
        z = mem.resolve(dir, size, true, 2100.0f, 9999.0f);
        mem.end_frame();
        check(z > 2000.0f && z < 2100.0f, "marker memory: a small change is eased in");
        const float before = z;
        z = mem.resolve(dir, size, false, 0.0f, 9999.0f);
        mem.end_frame();
        check(near(z, before), "marker memory: a frame the grid misses keeps the depth (not the fallback)");
        float zs[3];
        for (float& v : zs) {
            v = mem.resolve(normalize(Vec3{0.1005f, 0.0f, -1.0f}), size, true, 5000.0f, 9999.0f);  // head moved a bit
            mem.end_frame();
        }
        check(near(zs[0], before) && near(zs[1], before) && near(zs[2], 5000.0f),
              "marker memory: a real change is taken once it has held for 3 frames");
        z = mem.resolve(normalize(Vec3{-0.5f, 0.0f, -1.0f}), size, false, 0.0f, 1234.0f);
        check(near(z, 1234.0f), "marker memory: a new marker the grid misses gets the fallback");
    }

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

    // --- VR panel geometry ---
    // Anchor at eye height 1.2 m, turned 30 degrees left (yaw), looking down 20 degrees (pitch must not tip the panel).
    float anchor[16], tilt[16];
    mat4::rotation_y(30.0f * kDeg, a);
    rotation_x(-20.0f * kDeg, tilt);
    mat4::multiply(a, tilt, anchor);
    anchor[13] = 1.2f;
    const Vec3 eye = position(anchor);
    const Vec3 fwd{-std::sin(30.0f * kDeg), 0.0f, -std::cos(30.0f * kDeg)};
    for (const float curvature : {0.0f, 0.18f, 0.4f}) {
        const input::PanelGeometry g = input::PanelGeometry::in_front(anchor, 1.8f, 0.0f, 0.0f, 2.0f, 0.42f, curvature);
        char what[160];
        std::snprintf(what, sizeof what, "panel (curvature %.2f): centre 1.8 m along the level heading, facing the eye", curvature);
        check(near(g.centre.x, eye.x + fwd.x * 1.8f, 1e-4f) && near(g.centre.y, 1.2f) && near(g.centre.z, fwd.z * 1.8f, 1e-4f) &&
                  near(dot(g.normal, fwd), -1.0f) && near(g.up.y, 1.0f),
              what);
        // Rays from the eye at known surface points must come back with those uvs, top-left origin.
        bool all = true, oriented = true;
        for (const float u : {0.05f, 0.3f, 0.5f, 0.77f, 0.95f})
            for (const float v : {0.1f, 0.5f, 0.9f}) {
                const Vec3 target = g.point_at(u, v);
                float uv[2], dist;
                if (!g.intersect(eye, normalize(target - eye), uv, &dist) || !near(uv[0], u, 1e-3f) ||
                    !near(uv[1], v, 1e-3f) || !near(dist, length(target - eye), 1e-3f))
                    all = false;
                if (u < 0.5f && dot(target - g.centre, g.right) >= 0.0f) oriented = false;  // u grows to the right
                if (v < 0.5f && target.y <= g.centre.y) oriented = false;                    // v grows downwards
            }
        std::snprintf(what, sizeof what, "panel (curvature %.2f): ray hits return the surface uv and distance", curvature);
        check(all, what);
        std::snprintf(what, sizeof what, "panel (curvature %.2f): uv (0,0) is top-left as seen from the eye", curvature);
        check(oriented, what);
        float uv[2], dist;
        std::snprintf(what, sizeof what, "panel (curvature %.2f): rays past the edge or from behind miss", curvature);
        check(!g.intersect(eye, normalize(g.point_at(1.02f, 0.5f) - eye), uv, &dist) &&
                  !g.intersect(eye, normalize(g.point_at(0.5f, -0.05f) - eye), uv, &dist) &&
                  !g.intersect(eye + fwd * 4.0f, -fwd, uv, &dist),
              what);
    }
    {
        const input::PanelGeometry centred =
            input::PanelGeometry::in_front(anchor, 1.8f, 0.0f, 0.0f, 1.8f * 65.0f * kDeg, 0.42f, 65.0f / 360.0f);
        bool equidistant = true;
        for (const float u : {0.0f, 0.25f, 0.5f, 0.75f, 1.0f})
            if (!near(length(centred.point_at(u, 0.5f) - eye), 1.8f, 1e-3f)) equidistant = false;
        check(equidistant, "panel curved round the viewer (curvature = degrees / 360): every column 1.8 m away");
        float d, side, h;
        input::PanelGeometry::relative(anchor, centred.centre + Vec3{0, 0.3f, 0}, &d, &side, &h);
        check(near(d, 1.8f) && near(side, 0.0f, 1e-3f) && near(h, 0.3f), "panel: relative() inverts in_front()");
        const int px = centred.pixels_across(eye, 20.0f);
        check(px >= 1299 && px <= 1301, "panel: 65 degrees wide at 20 px/degree -> ~1300 texture pixels");
    }

    // --- Laser -> mouse in the world: the game projection of the laser direction, and back ---
    {
        const float game_proj[16] = {0.5978f, 0, 0, 0, 0, 1.428f, 0, 0, 0, 0, -1, -1, 0, 0, -6, 0};
        float px, py;
        check(input::project_direction(game_proj, {0, 0, -1}, 3440, 1440, &px, &py) && near(px, 1720.0f) && near(py, 720.0f),
              "laser straight ahead -> window centre");
        check(input::project_direction(game_proj, normalize(Vec3{1.0f / 0.5978f, 0, -1}), 3440, 1440, &px, &py) &&
                  near(px, 3440.0f, 1e-3f),
              "laser at the game's horizontal half-FOV -> right edge");
        input::project_direction(game_proj, normalize(Vec3{0, 0.35f, -1}), 3440, 1440, &px, &py);
        check(py < 720.0f, "laser up -> cursor up (y grows downwards)");
        check(!input::project_direction(game_proj, {0, 0, 1}, 3440, 1440, &px, &py), "laser behind the camera -> no pixel");
        const Vec3 back = normalize(input::unproject_pixel(game_proj, 2500.0f, 300.0f, 3440, 1440));
        input::project_direction(game_proj, back, 3440, 1440, &px, &py);
        check(near(px, 2500.0f, 1e-3f) && near(py, 300.0f, 1e-3f), "unproject_pixel inverts project_direction");
        float cx = 3500.0f, cy = 700.0f;
        check(!input::clamp_inside(&cx, &cy, 3440, 1440, 0.02f) && near(cx, 3439.0f - 0.02f * 3440) && near(cy, 700.0f),
              "aim outside the window is kept off the edge-pan zone");
        // Through a yawed recentre pose: a laser along the recentred heading lands in the centre.
        float camera_pose[16], camera_inverse[16];
        mat4::rotation_y(30.0f * kDeg, camera_pose);
        camera_pose[13] = 1.2f;
        mat4::rigid_inverse(camera_pose, camera_inverse);
        check(input::project_direction(game_proj, rotate(camera_inverse, fwd), 3440, 1440, &px, &py) &&
                  near(px, 1720.0f, 1e-3f) && near(py, 720.0f, 1e-3f),
              "laser along the recentred heading -> window centre");
    }

    // --- The order wheel's slices (0 = top, clockwise) and the tablet's hit test ---
    check(ui::wheel_slice(0.0f, 0.8f, 8, ui::kWheelInner) == 0 && ui::wheel_slice(0.8f, 0.0f, 8, ui::kWheelInner) == 2 &&
              ui::wheel_slice(0.0f, -0.8f, 8, ui::kWheelInner) == 4 && ui::wheel_slice(-0.8f, 0.0f, 8, ui::kWheelInner) == 6,
          "wheel: top / right / bottom / left -> slices 0 / 2 / 4 / 6");
    check(ui::wheel_slice(0.5f, 0.5f, 8, ui::kWheelInner) == 1 && ui::wheel_slice(-0.5f, 0.5f, 8, ui::kWheelInner) == 7,
          "wheel: the diagonals between them -> slices 1 and 7");
    check(ui::wheel_slice(0.1f, 0.1f, 8, ui::kWheelInner) == -1 && ui::wheel_slice(0.9f, 0.9f, 8, ui::kWheelInner) == -2,
          "wheel: the centre -> -1, outside the rim -> -2");
    {
        std::vector<ui::Item> items = {{{0.0f, 0.0f, 0.2f, 0.1f}, L"label", ui::Kind::Label},
                                       {{0.3f, 0.0f, 0.6f, 0.1f}, L"a", ui::Kind::Button},
                                       {{0.3f, 0.2f, 0.6f, 0.3f}, L"b", ui::Kind::Button}};
        check(ui::hit(items, 0.4f, 0.05f) == 1 && ui::hit(items, 0.4f, 0.25f) == 2 && ui::hit(items, 0.1f, 0.05f) == -1 &&
                  ui::hit(items, 0.4f, 0.15f) == -1,
              "panel hit test: buttons by their rectangles, labels and gaps never");
    }

    std::printf("%s: %d failure(s)\n", g_failures ? "FAILED" : "PASSED", g_failures);
    return g_failures ? 1 : 0;
}
