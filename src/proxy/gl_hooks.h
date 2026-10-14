#pragma once

namespace hw2vr::proxy {

// Resolves the real wglGetProcAddress / wglSwapBuffers. Must run after load_real_opengl32().
bool init_hooks();

}  // namespace hw2vr::proxy
