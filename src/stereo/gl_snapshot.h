#pragma once

// GL state captured at the start of a game frame (right after the swap) and restored before the frame is replayed
// for the second eye, so the replay starts from the state the game's own pass started from.
namespace hw2vr::replay {

void snapshot_frame_start();
void restore_frame_start();
// What restore_frame_start() bound, so the hooks' shadow tracking can follow.
unsigned snapshot_program();
unsigned snapshot_draw_fbo();

}  // namespace hw2vr::replay
