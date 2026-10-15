#pragma once

#include <vector>

// The game draws its HUD into its own framebuffer (fbo 35 on build 12653478), cleared to (0,0,0,1) and blended so
// alpha holds *transmittance*; the final composite lays it over the scene (docs/FINDINGS.md "Render passes").
// That layer mixes two kinds of draw:
//   - 2D UI with an ortho inMatP (taskbar, menus, text, CPU-positioned markers) -> belongs on the flat VR overlay;
//   - world-anchored 3D overlay with a perspective inMatP (selection circles, move lines, ship icons with pixel
//     offsets, the Sensors Manager disc) -> must be stereo, at the depth of what it marks.
// In VR the 2D draws are redirected into our overlay texture (with coverage alpha) and the 3D draws stay in the
// game's HUD framebuffer with the per-eye camera, so the game's own composite puts them in each eye image. UI the
// game draws straight to the screen after the composite (the mouse cursor) goes into a small cursor texture centred
// on the cursor's hotspot, so the cursor can be shown on the VR panel or out in the world at the laser's aim.
// The VR panel shows a filtered, downsampled copy of the overlay (build_panel_texture): SteamVR samples overlays
// without mipmaps, so the full-size UI on a panel ~1300 headset pixels wide would shimmer.
namespace hw2vr::hud {

// Identification: gl_hooks reports every ortho inMatP upload with the draw framebuffer it happened in; the HUD is the
// framebuffer that collects the most of them, consistently over several frames.
void note_ortho_projection(unsigned draw_fbo);
bool is_hud_fbo(unsigned fbo);

void set_capture(bool enabled, bool premultiplied);
void set_target_size(int width, int height);  // the game's HUD is window-sized
void set_window(void* hwnd);                  // the game window (cursor position for the cursor texture)
// Called by the glBindFramebuffer hook before the game switches draw framebuffer.
void on_draw_framebuffer_change(unsigned from, unsigned to);
void end_frame();

bool overlay_ready();        // the overlay texture was rebuilt this frame
unsigned overlay_texture();  // full-size 2D UI (premultiplied colour, coverage alpha)
int overlay_width();         // its size (the game window's), 0 before the first HUD frame
int overlay_height();
void release();              // delete GL objects (VR off)

// The overlay filtered down to width x height for the VR panel (call after the frame's draws, before the swap),
// over a dark backing of opacity `backing` (0 = see-through). Returns the panel texture, 0 if there's no overlay.
unsigned build_panel_texture(int width, int height, float backing = 0.0f);
// Mipmaps of this frame's screen UI and the coverage mask from them (ui_at / ui_coverage). Once per frame after the
// game's draws, whether or not a panel shows the UI.
void update_mask();

// Where the 2D UI actually has something: a coarse coverage mask (1/8 of the window per side) read back without
// stalling, one frame late, from the overlay's mipmaps. build_panel_texture updates it. The laser treats the
// panel as UI only where the mask is set; elsewhere it points through the panel into the world.
// Returns 1 = UI there, 0 = transparent, -1 = no mask yet. u, v from the top-left.
int ui_at(float u, float v);
// Fraction (0..1) of a screen rectangle {u0, v0, u1, v1} (top-left origin) that has screen UI on it, from the same
// mask; -1 = no mask yet. Tells whether a game window (build manager, a menu) is open.
float ui_coverage(const float rect[4]);
// The mask itself, for spotting UI that newly appears (the right-click menu): one byte per 8x8-pixel cell, rows
// bottom-up; mask_covered(byte) = UI there. Empty (0 x 0) until the first readback.
const std::vector<unsigned char>& ui_mask(int* width, int* height);
inline bool mask_covered(unsigned char cell) { return cell > 2; }  // any coverage in the cell (mip-averaged)
// A rectangle of this frame's screen UI {u0, v0, u1, v1} (top-left origin) copied at native resolution into crop
// texture `slot` (0..3); returns it with its size, 0 if there's no overlay. Call after the frame's draws.
unsigned crop_texture(int slot, const float rect[4], int* width, int* height);

// The mouse cursor, drawn into its own kCursorSize square texture with its hotspot at the centre.
constexpr int kCursorSize = 128;
struct Cursor {
    bool drawn = false;   // the game drew a cursor this frame
    unsigned texture = 0;
    int x = 0, y = 0;     // hotspot, client pixels
};
const Cursor& current_cursor();  // this frame so far (before the swap)
const Cursor& last_cursor();     // the last completed frame (after the swap)
void set_cursor_override(int x, int y);  // tests: pretend the cursor is here (x < 0 = off)

// Every game draw, before it runs. is_2d: the current program's projection is ortho (or untracked).
// Redirect: onto the overlay (screen-space UI). RedirectCursor: into the cursor texture. PlaceAtDepth: a world-space
// marker, drawn into the game's HUD layer at its ship's depth (world_marker_depth + stereo::place_at_depth).
// Skip: during the second-eye replay, the screen UI and cursor are already captured.
enum class DrawAction { Draw, Redirect, RedirectCursor, PlaceAtDepth, Skip };
DrawAction draw_action(unsigned draw_fbo, unsigned program, bool is_2d);
// For a PlaceAtDepth draw: its view depth in game units. bounds = its screen rectangle {x0, y0, x1, y1} in [0,1]
// (y up), or nullptr if unknown. During the replay it returns what the first eye used, in order.
float world_marker_depth(const float* bounds);
void set_world_markers(bool on);  // false: world markers stay with the screen UI (as before 0.8.0)
bool replaying();
void begin_redirect(unsigned draw_fbo);  // bind the overlay target, keep the game's colour blend, coverage alpha
void end_redirect(unsigned draw_fbo);    // restore the game's blend and framebuffer
bool begin_cursor_redirect();            // bind the cursor target, viewport shifted onto the hotspot; false = draw as is
void end_cursor_redirect(unsigned draw_fbo);

// While a frame is replayed for the second eye: no new overlay frame, no identification statistics.
void set_replaying(bool on);

// For tests/proxy_smoke.cpp (exported from the DLL as hw2vr_test_state).
struct TestState {
    unsigned hud_fbo;
    unsigned overlay_texture;
    unsigned overlay_frames;
    unsigned redirected_draws;
    unsigned cursor_texture;
    unsigned cursor_draws;
    unsigned world_draws;     // world markers placed (first eye)
    unsigned world_anchored;  // ... of which had a surface behind them
};
TestState test_state();
float last_world_marker_depth();

}  // namespace hw2vr::hud
