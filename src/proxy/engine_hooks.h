#pragma once

// Hooks into HomeworldRM.exe's own code (in memory only; the exe on disk is never touched).
//
// The UI renderer (FUN_006e527f on build 12653478) draws the world-space HUD layers (selection circles, brackets,
// health bars, ship names, order callouts) after PHASE_UIPre, then runs PHASE_UIIn and draws the screen-space
// layers (taskbar, menus, managers). Both are 2D draws into the same HUD framebuffer, so the GL stream alone can't
// tell them apart. The call that runs PHASE_UIIn (`push [phase id]; call rnd_RunPhase` in FUN_0071619d) is
// redirected through a stub that raises a flag and jumps on to rnd_RunPhase. It is found from the "PHASE_UIIn"
// string reference, not a hard-coded address; if it can't be found nothing is patched (docs/FINDINGS.md "UI phases").
namespace hw2vr::engine {

bool install();          // at the first swap (the game's code is running by then); logs what it found
bool installed();
bool screen_ui_phase();  // this frame's screen-space UI has started (the PHASE_UIIn call ran)
void frame_end();        // at the swap

// For tests: pretend the screen-space UI started (true) or not; find the PHASE_UIIn call in an exe on disk (mapped
// as an image without running it) and return its RVA, 0 if not found.
void set_screen_ui_phase(bool on);
void set_test_installed(bool on);
unsigned test_find_ui_in(const wchar_t* exe_path);

}  // namespace hw2vr::engine
