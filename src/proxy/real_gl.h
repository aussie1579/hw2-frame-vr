#pragma once

#include <windows.h>

namespace hw2vr::proxy {

// Loads the system opengl32.dll (SysWOW64 for this 32-bit process) and points every forwarding stub at it.
// Called from DllMain; returns false if the real DLL or any export can't be resolved.
bool load_real_opengl32();

HMODULE real_opengl32();

// Name of a forwarded (stub) export by index, for logging which unrecorded call broke a frame's replay.
const char* forwarded_export_name(long index);

}  // namespace hw2vr::proxy

// Bumped by the counting stubs (exports.cpp) and by counting thunks for unrecorded extension functions.
extern "C" long g_hw2vr_unrecorded_calls;
extern "C" long g_hw2vr_last_unrecorded;

namespace hw2vr::proxy {

}  // namespace hw2vr::proxy
