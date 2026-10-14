#pragma once

#include <windows.h>

namespace hw2vr::proxy {

// Loads the system opengl32.dll (SysWOW64 for this 32-bit process) and points every forwarding stub at it.
// Called from DllMain; returns false if the real DLL or any export can't be resolved.
bool load_real_opengl32();

HMODULE real_opengl32();

}  // namespace hw2vr::proxy
