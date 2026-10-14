#pragma once

#include <cstdint>

// One SteamVR overlay with a GL texture, placed in seated tracking space. Setters skip unchanged values, so they
// can be called every frame. vr::stop() destroys every overlay (their handles go invalid; create() again after
// the next start).
namespace hw2vr::vr {

class Overlay {
public:
    Overlay() = default;
    Overlay(const Overlay&) = delete;
    Overlay& operator=(const Overlay&) = delete;

    // sort_order: higher draws on top of lower. Logs and returns false on failure (the overlay then stays a no-op).
    bool create(const char* key, const char* name, unsigned sort_order, bool premultiplied);
    void destroy();
    bool valid() const { return handle_ != 0; }

    // SteamVR copies GL overlay textures when this is called: call again whenever the texture's content changes.
    void set_texture(unsigned gl_texture);
    void set_width(float metres);
    void set_curvature(float curvature);  // 0 = flat; width / (2 pi radius)
    void set_alpha(float alpha);
    void set_color(float r, float g, float b);
    void set_texel_aspect(float aspect);
    void set_bounds(float u_min, float v_min, float u_max, float v_max);  // texture sub-rectangle, v from the top
    void place(const float seated_from_overlay[16]);  // column-major
    void show();
    void hide();
    bool visible() const { return visible_; }

private:
    uint64_t handle_ = 0;  // k_ulOverlayHandleInvalid
    bool visible_ = false;
    float width_ = -1.0f, curvature_ = -1.0f, alpha_ = -1.0f, aspect_ = -1.0f;
    float color_[3] = {-1.0f, -1.0f, -1.0f}, bounds_[4] = {-1.0f, -1.0f, -1.0f, -1.0f};
};

void destroy_all_overlays();  // vr::stop()

}  // namespace hw2vr::vr
