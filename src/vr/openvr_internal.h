#pragma once

// OpenVR interfaces for the vr/ modules that talk to SteamVR directly (overlays, input). Only valid between
// vr::start() and vr::stop(); nullptr otherwise.
#include <openvr.h>

namespace hw2vr::vr {

::vr::IVRSystem* system_api();
::vr::IVROverlay* overlay_api();
::vr::IVRInput* input_api();

}  // namespace hw2vr::vr
