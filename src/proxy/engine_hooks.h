#pragma once

// Hooks into HomeworldRM.exe's own code (in memory only; the exe on disk is never touched).
//
// The UI renderer (FUN_006e527f on build 12653478) draws the world-space HUD layers (selection circles, brackets,
// health bars, ship names, order callouts) after PHASE_UIPre, then runs PHASE_UIIn and draws the screen-space
// layers (taskbar, menus, managers). Both are 2D draws into the same HUD framebuffer, so the GL stream alone can't
// tell them apart. The call that runs PHASE_UIIn (`push [phase id]; call rnd_RunPhase` in FUN_0071619d) is
// redirected through a stub that raises a flag and jumps on to rnd_RunPhase. It is found from the "PHASE_UIIn"
// string reference, not a hard-coded address; if it can't be found nothing is patched (docs/FINDINGS.md "UI phases").
//
// The game camera's update (FUN_0078bf32) builds the camera's world->view matrix (camera +0x04) from its orbit
// angles, zoom and focus, plus a rotation-only copy for the sky (+0x44); the culling planes, the eye position, the
// renderer's view and the mouse picking all come from those afterwards. Its call in the camera wrapper
// (FUN_00476ab3) is redirected through a stub that runs it, then turns both matrices by the head's rotation: the game
// looks - culls, picks, places its markers - where the player looks, while its orbit, zoom and focus stay untouched.
// Found from the function's prologue and its "roll_blend" string reference (docs/FINDINGS.md "Game camera turn").
namespace hw2vr::engine {

bool install();          // at the first swap (the game's code is running by then); logs what it found
bool installed();        // the UI phase hook
bool screen_ui_phase();  // this frame's screen-space UI has started (the PHASE_UIIn call ran)
void frame_end();        // at the swap

bool camera_hook_installed();
// The rotation the game camera turns by from its next update on: a rotation in the game camera's own view space (the
// turned camera's axes, column-major), nullptr = none.
void set_camera_turn(const float* turn);
unsigned camera_updates();  // camera updates the turn was applied to so far (rejected ones not counted)
unsigned camera_calls();    // every camera update so far - none in a frame = no 3D world (menus, loading screens)
// The game camera's vertical field of view (camera +0x124, 70 by default), taken from its next update on, so it
// draws, culls and picks for a taller view than its own; 0 = put the game's own back.
void set_camera_fov(float degrees);

// For tests: pretend the screen-space UI started (true) or not; find the PHASE_UIIn call in an exe on disk (mapped
// as an image without running it) and return its RVA, 0 if not found.
void set_screen_ui_phase(bool on);
void set_test_installed(bool on);
unsigned test_find_ui_in(const wchar_t* exe_path);
// The camera update function's RVA in an exe on disk (0 if not found) and how many calls to it were found.
unsigned test_find_camera_update(const wchar_t* exe_path, int* calls);
// Runs the turn on a camera object laid out like the game's (the stub's second half), with `turn` set.
void test_turn_camera(float* camera, const float* turn);

}  // namespace hw2vr::engine
