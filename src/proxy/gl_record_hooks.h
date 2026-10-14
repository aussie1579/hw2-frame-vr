#pragma once

#include <windows.h>

// Recording hooks for synchronized stereo (stereo/frame_replay.h): the GL entry points the game uses during a frame,
// besides the camera-aware ones in gl_hooks.cpp. See tools/gen_exports.py for which exports are hooked.
namespace hw2vr::record {

bool init();  // resolve the real GL 1.1 exports we wrap (after load_real_opengl32)

// For hook_wglGetProcAddress: what to hand the game for an extension function. Returns our recording hook for
// recorded functions, the real pointer for benign ones (queries, setup), and a counting thunk for anything else.
PROC wrap_extension(const char* name, PROC real);

// Element-array buffer of the bound VAO: indexed draws are only replayable with indices in a buffer.
bool element_buffer_bound();

// Name of an unrecorded call by the code stored in g_hw2vr_last_unrecorded (export index or extension thunk).
const char* unrecorded_call_name(long code);

}  // namespace hw2vr::record
