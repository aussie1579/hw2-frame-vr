#include "vr/overlay.h"

#include <algorithm>
#include <vector>

#include "common/log.h"
#include "common/mat4.h"
#include "vr/openvr_internal.h"

namespace hw2vr::vr {
namespace {

std::vector<Overlay*> g_overlays;  // created ones, for destroy_all_overlays

}  // namespace

bool Overlay::create(const char* key, const char* name, unsigned sort_order, bool premultiplied) {
    if (valid()) return true;
    ::vr::IVROverlay* o = overlay_api();
    if (!o) return false;
    ::vr::VROverlayHandle_t h = ::vr::k_ulOverlayHandleInvalid;
    const auto err = o->CreateOverlay(key, name, &h);
    if (err != ::vr::VROverlayError_None) {
        HW2VR_LOG("vr: CreateOverlay(%s) failed with error %d", key, static_cast<int>(err));
        return false;
    }
    handle_ = h;
    visible_ = false;  // a new overlay starts hidden with SteamVR's defaults: forget the cached values
    width_ = curvature_ = alpha_ = aspect_ = -1.0f;
    std::fill(std::begin(color_), std::end(color_), -1.0f);
    std::fill(std::begin(bounds_), std::end(bounds_), -1.0f);
    o->SetOverlaySortOrder(h, sort_order);
    o->SetOverlayFlag(h, ::vr::VROverlayFlags_IsPremultiplied, premultiplied);
    g_overlays.push_back(this);
    return true;
}

void Overlay::destroy() {
    if (!valid()) return;
    if (::vr::IVROverlay* o = overlay_api()) o->DestroyOverlay(handle_);
    g_overlays.erase(std::remove(g_overlays.begin(), g_overlays.end(), this), g_overlays.end());
    handle_ = 0;
    visible_ = false;
}

void Overlay::set_texture(unsigned gl_texture) {
    if (!valid() || !gl_texture) return;
    ::vr::Texture_t tex{reinterpret_cast<void*>(static_cast<uintptr_t>(gl_texture)), ::vr::TextureType_OpenGL,
                        ::vr::ColorSpace_Gamma};
    overlay_api()->SetOverlayTexture(handle_, &tex);
}

void Overlay::set_width(float metres) {
    if (!valid() || metres == width_) return;
    width_ = metres;
    overlay_api()->SetOverlayWidthInMeters(handle_, metres);
}

void Overlay::set_curvature(float curvature) {
    if (!valid() || curvature == curvature_) return;
    curvature_ = curvature;
    overlay_api()->SetOverlayCurvature(handle_, curvature);
}

void Overlay::set_alpha(float alpha) {
    if (!valid() || alpha == alpha_) return;
    alpha_ = alpha;
    overlay_api()->SetOverlayAlpha(handle_, alpha);
}

void Overlay::set_color(float r, float g, float b) {
    if (!valid() || (r == color_[0] && g == color_[1] && b == color_[2])) return;
    color_[0] = r, color_[1] = g, color_[2] = b;
    overlay_api()->SetOverlayColor(handle_, r, g, b);
}

void Overlay::set_texel_aspect(float aspect) {
    if (!valid() || aspect == aspect_) return;
    aspect_ = aspect;
    overlay_api()->SetOverlayTexelAspect(handle_, aspect);
}

void Overlay::set_bounds(float u_min, float v_min, float u_max, float v_max) {
    if (!valid() || (u_min == bounds_[0] && v_min == bounds_[1] && u_max == bounds_[2] && v_max == bounds_[3])) return;
    bounds_[0] = u_min, bounds_[1] = v_min, bounds_[2] = u_max, bounds_[3] = v_max;
    ::vr::VRTextureBounds_t b{u_min, v_min, u_max, v_max};
    overlay_api()->SetOverlayTextureBounds(handle_, &b);
}

void Overlay::place(const float seated_from_overlay[16]) {
    if (!valid()) return;
    ::vr::HmdMatrix34_t m;
    mat4::to_rows3x4(seated_from_overlay, m.m);
    overlay_api()->SetOverlayTransformAbsolute(handle_, ::vr::TrackingUniverseSeated, &m);
}

void Overlay::show() {
    if (!valid() || visible_) return;
    overlay_api()->ShowOverlay(handle_);
    visible_ = true;
}

void Overlay::hide() {
    if (!valid() || !visible_) return;
    overlay_api()->HideOverlay(handle_);
    visible_ = false;
}

void destroy_all_overlays() {
    while (!g_overlays.empty()) g_overlays.back()->destroy();
}

}  // namespace hw2vr::vr
