#pragma once

namespace hw2vr::hotkeys {

// Ctrl+Shift+<vk>, edge-triggered, only while the game window has focus. Poll once per frame per key.
bool pressed(int vk);

}  // namespace hw2vr::hotkeys
