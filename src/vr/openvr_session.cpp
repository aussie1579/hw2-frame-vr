#include "vr/openvr_session.h"

#include <windows.h>

#include <openvr.h>

#include "common/log.h"
#include "common/mat4.h"

namespace hw2vr::vr {
namespace {

// openvr_api.dll C entry points (VR_CALLTYPE = __cdecl). We replicate vr::VR_Init's sequence instead of linking.
using PFN_InitInternal2 = uint32_t(__cdecl*)(::vr::EVRInitError*, ::vr::EVRApplicationType, const char*);
using PFN_ShutdownInternal = void(__cdecl*)();
using PFN_IsHmdPresent = bool(__cdecl*)();
using PFN_IsInterfaceVersionValid = bool(__cdecl*)(const char*);
using PFN_GetGenericInterface = void*(__cdecl*)(const char*, ::vr::EVRInitError*);
using PFN_GetInitErrorDescription = const char*(__cdecl*)(::vr::EVRInitError);

HMODULE g_dll = nullptr;
PFN_ShutdownInternal g_shutdown = nullptr;
PFN_GetInitErrorDescription g_describe = nullptr;
::vr::IVRSystem* g_system = nullptr;
::vr::IVRCompositor* g_compositor = nullptr;
::vr::IVROverlay* g_overlay = nullptr;
::vr::VROverlayHandle_t g_hud = ::vr::k_ulOverlayHandleInvalid;
EyeFrustum g_frustum[2] = {};
float g_eye_to_head[2][16] = {};

const char* describe(::vr::EVRInitError e) { return g_describe ? g_describe(e) : "?"; }

template <class T>
T get_interface(PFN_GetGenericInterface get, const char* version) {
    ::vr::EVRInitError err = ::vr::VRInitError_None;
    void* p = get(version, &err);
    if (!p) HW2VR_LOG("vr: %s unavailable: %s", version, describe(err));
    return static_cast<T>(p);
}

void unload() {
    g_system = nullptr;
    g_compositor = nullptr;
    g_overlay = nullptr;
    g_hud = ::vr::k_ulOverlayHandleInvalid;
    if (g_dll) FreeLibrary(g_dll);
    g_dll = nullptr;
}

}  // namespace

bool start(const std::wstring& dll_dir) {
    if (running()) return true;
    const std::wstring path = dll_dir + L"openvr_api.dll";
    g_dll = LoadLibraryW(path.c_str());
    if (!g_dll) {
        HW2VR_LOG("vr: cannot load %ls (error %lu) - staying flat", path.c_str(), GetLastError());
        return false;
    }
    auto init = reinterpret_cast<PFN_InitInternal2>(GetProcAddress(g_dll, "VR_InitInternal2"));
    auto hmd_present = reinterpret_cast<PFN_IsHmdPresent>(GetProcAddress(g_dll, "VR_IsHmdPresent"));
    auto version_valid = reinterpret_cast<PFN_IsInterfaceVersionValid>(GetProcAddress(g_dll, "VR_IsInterfaceVersionValid"));
    auto get = reinterpret_cast<PFN_GetGenericInterface>(GetProcAddress(g_dll, "VR_GetGenericInterface"));
    g_shutdown = reinterpret_cast<PFN_ShutdownInternal>(GetProcAddress(g_dll, "VR_ShutdownInternal"));
    g_describe = reinterpret_cast<PFN_GetInitErrorDescription>(GetProcAddress(g_dll, "VR_GetVRInitErrorAsEnglishDescription"));
    if (!init || !hmd_present || !version_valid || !get || !g_shutdown) {
        HW2VR_LOG("vr: openvr_api.dll is missing expected exports - staying flat");
        unload();
        return false;
    }
    if (!hmd_present()) {
        HW2VR_LOG("vr: no headset detected (is SteamVR running with the Frame connected?) - staying flat");
        unload();
        return false;
    }

    ::vr::EVRInitError err = ::vr::VRInitError_None;
    init(&err, ::vr::VRApplication_Scene, nullptr);
    if (err == ::vr::VRInitError_None && !version_valid(::vr::IVRSystem_Version)) err = ::vr::VRInitError_Init_InterfaceNotFound;
    if (err != ::vr::VRInitError_None) {
        HW2VR_LOG("vr: VR_Init failed: %s - staying flat", describe(err));
        unload();
        return false;
    }
    g_system = get_interface<::vr::IVRSystem*>(get, ::vr::IVRSystem_Version);
    g_compositor = get_interface<::vr::IVRCompositor*>(get, ::vr::IVRCompositor_Version);
    g_overlay = get_interface<::vr::IVROverlay*>(get, ::vr::IVROverlay_Version);
    if (!g_system || !g_compositor || !g_overlay) {
        stop();
        return false;
    }
    g_system->SetSDKVersion(::vr::k_nSteamVRVersionMajor, ::vr::k_nSteamVRVersionMinor, ::vr::k_nSteamVRVersionBuild);
    g_compositor->SetTrackingSpace(::vr::TrackingUniverseSeated);

    char model[128] = "?";
    g_system->GetStringTrackedDeviceProperty(::vr::k_unTrackedDeviceIndex_Hmd, ::vr::Prop_ModelNumber_String, model, sizeof model);
    const float hz = g_system->GetFloatTrackedDeviceProperty(::vr::k_unTrackedDeviceIndex_Hmd, ::vr::Prop_DisplayFrequency_Float);
    uint32_t w = 0, h = 0;
    g_system->GetRecommendedRenderTargetSize(&w, &h);
    HW2VR_LOG("vr: started - HMD '%s' %.0f Hz, recommended %ux%u per eye", model, hz, w, h);

    for (int eye = 0; eye < 2; ++eye) {
        const auto e = eye == 0 ? ::vr::Eye_Left : ::vr::Eye_Right;
        float l, r, t, b;
        g_system->GetProjectionRaw(e, &l, &r, &t, &b);
        // OpenVR's "top"/"bottom" naming is inverted for GL; take the larger tangent as up.
        g_frustum[eye] = {l < r ? l : r, l < r ? r : l, t < b ? t : b, t < b ? b : t};
        const ::vr::HmdMatrix34_t m = g_system->GetEyeToHeadTransform(e);
        mat4::from_rows3x4(m.m, g_eye_to_head[eye]);
        HW2VR_LOG("vr: %s eye tangents L %.3f R %.3f D %.3f U %.3f, eye offset (%.4f, %.4f, %.4f) m",
                  eye == 0 ? "left" : "right", g_frustum[eye].left, g_frustum[eye].right, g_frustum[eye].down,
                  g_frustum[eye].up, g_eye_to_head[eye][12], g_eye_to_head[eye][13], g_eye_to_head[eye][14]);
    }
    return true;
}

void stop() {
    if (!g_dll) return;
    if (g_overlay && g_hud != ::vr::k_ulOverlayHandleInvalid) g_overlay->DestroyOverlay(g_hud);
    if (g_system && g_shutdown) g_shutdown();
    HW2VR_LOG("vr: stopped");
    unload();
}

bool running() { return g_system && g_compositor; }

bool wait_poses(float hmd_pose[16]) {
    ::vr::TrackedDevicePose_t poses[::vr::k_unMaxTrackedDeviceCount];
    const auto err = g_compositor->WaitGetPoses(poses, ::vr::k_unMaxTrackedDeviceCount, nullptr, 0);
    const auto& hmd = poses[::vr::k_unTrackedDeviceIndex_Hmd];
    if (err != ::vr::VRCompositorError_None || !hmd.bPoseIsValid) return false;
    mat4::from_rows3x4(hmd.mDeviceToAbsoluteTracking.m, hmd_pose);
    return true;
}

const EyeFrustum& frustum(int eye) { return g_frustum[eye & 1]; }
const float* eye_to_head(int eye) { return g_eye_to_head[eye & 1]; }

bool submit(int eye, unsigned gl_texture, const float hmd_pose_used[16]) {
    ::vr::VRTextureWithPose_t tex{};
    tex.handle = reinterpret_cast<void*>(static_cast<uintptr_t>(gl_texture));
    tex.eType = ::vr::TextureType_OpenGL;
    tex.eColorSpace = ::vr::ColorSpace_Gamma;
    mat4::to_rows3x4(hmd_pose_used, tex.mDeviceToAbsoluteTracking.m);
    const auto err = g_compositor->Submit(eye == 0 ? ::vr::Eye_Left : ::vr::Eye_Right, &tex, nullptr,
                                          ::vr::Submit_TextureWithPose);
    static bool logged = false;
    if (err != ::vr::VRCompositorError_None && !logged) {
        HW2VR_LOG("vr: Submit failed with error %d (logged once)", static_cast<int>(err));
        logged = true;
    }
    return err == ::vr::VRCompositorError_None;
}

bool hud_create(bool premultiplied) {
    if (g_hud != ::vr::k_ulOverlayHandleInvalid) return true;
    const auto err = g_overlay->CreateOverlay("hw2vr.hud", "Homeworld 2 HUD", &g_hud);
    if (err != ::vr::VROverlayError_None) {
        HW2VR_LOG("vr: CreateOverlay failed with error %d - HUD stays in the eye images", static_cast<int>(err));
        g_hud = ::vr::k_ulOverlayHandleInvalid;
        return false;
    }
    g_overlay->SetOverlayFlag(g_hud, ::vr::VROverlayFlags_IsPremultiplied, premultiplied);
    return true;
}

void hud_set_texture(unsigned gl_texture) {
    if (g_hud == ::vr::k_ulOverlayHandleInvalid) return;
    ::vr::Texture_t tex{reinterpret_cast<void*>(static_cast<uintptr_t>(gl_texture)), ::vr::TextureType_OpenGL,
                        ::vr::ColorSpace_Gamma};
    g_overlay->SetOverlayTexture(g_hud, &tex);
}

void hud_place(const float tracking_from_hud[16], float width_m) {
    if (g_hud == ::vr::k_ulOverlayHandleInvalid) return;
    ::vr::HmdMatrix34_t m;
    mat4::to_rows3x4(tracking_from_hud, m.m);
    g_overlay->SetOverlayTransformAbsolute(g_hud, ::vr::TrackingUniverseSeated, &m);
    g_overlay->SetOverlayWidthInMeters(g_hud, width_m);
}

void hud_visible(bool on) {
    if (g_hud == ::vr::k_ulOverlayHandleInvalid) return;
    on ? g_overlay->ShowOverlay(g_hud) : g_overlay->HideOverlay(g_hud);
}

}  // namespace hw2vr::vr
