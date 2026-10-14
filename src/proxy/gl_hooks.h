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

}  // namespace hw2vr::proxy
