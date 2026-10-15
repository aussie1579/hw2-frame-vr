#pragma once

#include <windows.h>

#include "input/key_names.h"

// Synthesised mouse and keyboard input for the game. HomeworldRM reads both from window messages (no raw input or
// DirectInput; it warps the cursor with SetCursorPos during right-drag camera rotation - docs/FINDINGS.md "Input"),
// so real input events are what it needs: SendInput for buttons, wheel, keys and relative motion, SetCursorPos for
// placing the cursor. All of it only while the game window is in the foreground.
namespace hw2vr::inputsim {

bool game_foreground();                          // the foreground window belongs to this process
bool cursor_over(HWND game);                     // the window under the cursor is the game's
void set_cursor_client(HWND game, int x, int y); // client pixels
bool cursor_client(HWND game, int* x, int* y);   // where the cursor is now, client pixels
void move_relative(int dx, int dy);              // raw mouse counts (subject to the user's mouse speed setting)
void mouse_button(int button, bool down);        // 0 = left, 1 = right, 2 = middle
void wheel(int notches);                         // positive = away from the user (HW2: zoom in)
void keys(const keys::Binding& combo, bool down);  // down: in order; up: reverse order; one SendInput each
// Brings the game window to the front (after the SteamVR dashboard or another program took the focus) - only on the
// player's own controller input. True if it is the foreground window afterwards.
bool bring_to_front(HWND game);

}  // namespace hw2vr::inputsim
