#pragma once

#include <windows.h>

#include "input/key_names.h"

// Synthesised mouse and keyboard input for the game. HomeworldRM reads both from window messages (no raw input or
// DirectInput; it warps the cursor with SetCursorPos during right-drag camera rotation - docs/FINDINGS.md "Input").
// Keys always go out through SendInput (only while the game window is in the foreground). The mouse has two modes:
//   - cursor (the game window fits on the desktop): SendInput for buttons, wheel and relative motion, SetCursorPos
//     for placing the cursor - real input, as a mouse would make it;
//   - window (a game window bigger than the desktop, e.g. 2880x2880 for VR): the real cursor can't reach most of it,
//     so mouse events are queued here and delivered straight to the game's window procedure (a subclass), with the
//     real mouse's own messages swallowed. The game's SetCursorPos and ClipCursor calls are hooked in its import
//     table: a warp just tells us where the game put its cursor (the anchor of a right-drag turn), and neither moves
//     or traps the real cursor.
namespace hw2vr::inputsim {

// Window mode on (game = the game's window) or off. The first switch to window mode installs the subclass and the
// import hooks; logged.
void set_window_mode(HWND game, bool on);
bool window_mode();

bool game_foreground();                          // the foreground window belongs to this process
bool cursor_over(HWND game);                     // the window under the cursor is the game's (window mode: true)
void set_cursor_client(HWND game, int x, int y); // client pixels
bool cursor_client(HWND game, int* x, int* y);   // where the cursor is now, client pixels
void move_relative(int dx, int dy);              // mouse counts (cursor mode: subject to the user's mouse speed)
void mouse_button(int button, bool down);        // 0 = left, 1 = right, 2 = middle
void wheel(int notches);                         // positive = away from the user (HW2: zoom in)
void keys(const keys::Binding& combo, bool down);  // down: in order; up: reverse order; one SendInput each
// Brings the game window to the front (after the SteamVR dashboard or another program took the focus) - only on the
// player's own controller input. True if it is the foreground window afterwards.
bool bring_to_front(HWND game);

}  // namespace hw2vr::inputsim
