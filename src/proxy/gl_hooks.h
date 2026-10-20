#pragma once

namespace hw2vr::proxy {

// Resolves the real wglGetProcAddress / wglSwapBuffers / draw entry points and the recording hooks. Must run after
// load_real_opengl32().
bool init_hooks();

// Replays the recorded frame (stereo/frame_replay.h) through the camera-aware hooks, with whatever camera
// stereo/view_override currently holds. Called at wglSwapBuffers for the second eye.
void replay_frame();

// [debug] replay_test_offset_x: replay every frame without VR (tests only).
void configure_replay_test(float offset_x);

// [vr] lens_flares: the game's lens flares (bg_lensflare: the sun's flare and its ghosts, flares from effects) are
// sprites the game lays out on the CPU along the line from the light to its own screen centre - a camera effect
// that floats in front of the eyes in VR. Off: their draws are skipped while VR runs.
void set_lens_flares_in_vr(bool on);

}  // namespace hw2vr::proxy
