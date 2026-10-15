#pragma once

#include <windows.h>

#include "input/key_names.h"

// Short input sequences spread over frames, so the game handles each event in its own message pump: a modifier
// key goes down a frame before the click it modifies, a mode key (Move) a few frames before the click that
// confirms it. run() once per frame; the cursor should stay put while busy().
namespace hw2vr::macro {

void tap(const keys::Binding& keys);  // down this frame, up the next
// modifier (may be None) down, click (button 0 left, 1 right) at a client pixel, modifier up.
void click_with(const keys::Binding& modifier, HWND window, int x, int y, int button = 0);
// A mode key, then (a few frames later) a left click at a client pixel: e.g. Move, then "here".
void key_then_click(const keys::Binding& keys, HWND window, int x, int y);

void run();
bool busy();
void clear();  // releases anything it holds down, drops the rest

}  // namespace hw2vr::macro
