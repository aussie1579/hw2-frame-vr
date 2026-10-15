#include "input/ui_canvas.h"

#include <windows.h>

#include <algorithm>
#include <cmath>

namespace hw2vr::ui {
namespace {

constexpr float kPi = 3.14159265358979f;

struct Rgba {
    float r, g, b, a;
};

// The game's own look (ui\newui styles): dark glass, teal outlines (OUTLINECOLOR 124,200,196), warm titles.
constexpr Rgba kBackground{8, 14, 18, 0.88f}, kEdge{124, 200, 196, 0.85f};
constexpr Rgba kButton{20, 34, 40, 0.94f}, kHover{30, 78, 86, 0.97f}, kPressed{60, 150, 150, 1.0f};
constexpr Rgba kLit{196, 138, 40, 0.97f}, kButtonEdge{124, 200, 196, 0.75f};
constexpr Rgba kText{236, 242, 240, 1.0f}, kLabelText{150, 196, 192, 1.0f}, kTitleText{255, 255, 230, 1.0f};

// Straight-alpha "over" into a pixel stored as 0xAARRGGBB.
void blend(unsigned& dst, const Rgba& c, float coverage) {
    const float a = std::clamp(c.a * coverage, 0.0f, 1.0f);
    if (a <= 0.0f) return;
    const float da = ((dst >> 24) & 0xFF) / 255.0f;
    const float dr = static_cast<float>((dst >> 16) & 0xFF), dg = static_cast<float>((dst >> 8) & 0xFF),
                db = static_cast<float>(dst & 0xFF);
    const float oa = a + da * (1.0f - a);
    auto mix = [&](float s, float d) { return oa > 0.0f ? (s * a + d * da * (1.0f - a)) / oa : 0.0f; };
    const unsigned r = static_cast<unsigned>(std::clamp(mix(c.r, dr), 0.0f, 255.0f) + 0.5f);
    const unsigned g = static_cast<unsigned>(std::clamp(mix(c.g, dg), 0.0f, 255.0f) + 0.5f);
    const unsigned b = static_cast<unsigned>(std::clamp(mix(c.b, db), 0.0f, 255.0f) + 0.5f);
    const unsigned A = static_cast<unsigned>(oa * 255.0f + 0.5f);
    dst = A << 24 | r << 16 | g << 8 | b;
}

// Signed distance from p to a rounded box (centre c, half size h, corner radius r), in pixels.
float rounded_box(float px, float py, float cx, float cy, float hx, float hy, float r) {
    const float qx = std::fabs(px - cx) - (hx - r), qy = std::fabs(py - cy) - (hy - r);
    const float ox = std::max(qx, 0.0f), oy = std::max(qy, 0.0f);
    return std::sqrt(ox * ox + oy * oy) + std::min(std::max(qx, qy), 0.0f) - r;
}

void fill_rounded(sprites::Image& img, float x0, float y0, float x1, float y1, float radius, const Rgba& fill,
                  const Rgba& edge, float edge_width) {
    const float cx = 0.5f * (x0 + x1), cy = 0.5f * (y0 + y1), hx = 0.5f * (x1 - x0), hy = 0.5f * (y1 - y0);
    const int ix0 = std::max(0, static_cast<int>(x0) - 1), ix1 = std::min(img.width, static_cast<int>(x1) + 2);
    const int iy0 = std::max(0, static_cast<int>(y0) - 1), iy1 = std::min(img.height, static_cast<int>(y1) + 2);
    for (int y = iy0; y < iy1; ++y)
        for (int x = ix0; x < ix1; ++x) {
            const float d = rounded_box(x + 0.5f, y + 0.5f, cx, cy, hx, hy, radius);
            const float inside = std::clamp(0.5f - d, 0.0f, 1.0f);
            if (inside <= 0.0f) continue;
            unsigned& px = img.pixels[static_cast<size_t>(y) * img.width + x];
            blend(px, fill, inside);
            if (edge_width > 0.0f) blend(px, edge, inside * std::clamp(edge_width + 0.5f + d, 0.0f, 1.0f));
        }
}

// An image scaled (bilinear) into a pixel rectangle, keeping its aspect, centred.
void draw_image(sprites::Image& img, const sprites::Image& src, float x0, float y0, float x1, float y1, float opacity = 1.0f) {
    if (src.width <= 0 || src.height <= 0 || x1 <= x0 || y1 <= y0) return;
    const float scale = std::min((x1 - x0) / src.width, (y1 - y0) / src.height);
    const float w = src.width * scale, h = src.height * scale;
    const float ox = 0.5f * (x0 + x1 - w), oy = 0.5f * (y0 + y1 - h);
    const int ix0 = std::max(0, static_cast<int>(ox)), ix1 = std::min(img.width, static_cast<int>(ox + w) + 1);
    const int iy0 = std::max(0, static_cast<int>(oy)), iy1 = std::min(img.height, static_cast<int>(oy + h) + 1);
    auto texel = [&](int x, int y, int c) {
        x = std::clamp(x, 0, src.width - 1), y = std::clamp(y, 0, src.height - 1);
        return static_cast<float>((src.pixels[static_cast<size_t>(y) * src.width + x] >> (8 * c)) & 0xFF);
    };
    for (int y = iy0; y < iy1; ++y)
        for (int x = ix0; x < ix1; ++x) {
            const float sx = (x + 0.5f - ox) / scale - 0.5f, sy = (y + 0.5f - oy) / scale - 0.5f;
            if (sx < -0.5f || sy < -0.5f || sx > src.width - 0.5f || sy > src.height - 0.5f) continue;
            const int tx = static_cast<int>(std::floor(sx)), ty = static_cast<int>(std::floor(sy));
            const float fx = sx - tx, fy = sy - ty;
            float c[4];
            for (int k = 0; k < 4; ++k)
                c[k] = (texel(tx, ty, k) * (1 - fx) + texel(tx + 1, ty, k) * fx) * (1 - fy) +
                       (texel(tx, ty + 1, k) * (1 - fx) + texel(tx + 1, ty + 1, k) * fx) * fy;
            blend(img.pixels[static_cast<size_t>(y) * img.width + x], Rgba{c[2], c[1], c[0], c[3] / 255.0f}, opacity);
        }
}

// Text: GDI draws white on black into a DIB; the grey level is the coverage, blended in the item's colour.
class TextLayer {
public:
    TextLayer(int w, int h) : w_(w), h_(h) {
        BITMAPINFO bi{};
        bi.bmiHeader.biSize = sizeof bi.bmiHeader;
        bi.bmiHeader.biWidth = w;
        bi.bmiHeader.biHeight = -h;  // top-down
        bi.bmiHeader.biPlanes = 1;
        bi.bmiHeader.biBitCount = 32;
        bi.bmiHeader.biCompression = BI_RGB;
        dc_ = CreateCompatibleDC(nullptr);
        bmp_ = dc_ ? CreateDIBSection(dc_, &bi, DIB_RGB_COLORS, &bits_, nullptr, 0) : nullptr;
        if (bmp_) old_ = SelectObject(dc_, bmp_);
        if (dc_) SetBkMode(dc_, TRANSPARENT), SetTextColor(dc_, RGB(255, 255, 255));
    }
    ~TextLayer() {
        if (old_) SelectObject(dc_, old_);
        if (bmp_) DeleteObject(bmp_);
        if (dc_) DeleteDC(dc_);
    }
    bool ok() const { return bits_ != nullptr; }

    // Draws `text` centred in the pixel rectangle and blends it into `img` in `colour`.
    void draw(sprites::Image& img, const std::wstring& text, int x0, int y0, int x1, int y1, int font_px, bool bold,
              const Rgba& colour, UINT align = DT_CENTER) {
        if (!ok() || text.empty()) return;
        x0 = std::max(0, x0), y0 = std::max(0, y0), x1 = std::min(w_, x1), y1 = std::min(h_, y1);
        if (x1 <= x0 || y1 <= y0) return;
        auto* p = static_cast<unsigned*>(bits_);
        for (int y = y0; y < y1; ++y) std::fill(p + static_cast<size_t>(y) * w_ + x0, p + static_cast<size_t>(y) * w_ + x1, 0u);
        // Shrink to fit the width (a long label on a narrow button) rather than cut it off.
        HFONT font = nullptr;
        for (int attempt = 0; attempt < 8; ++attempt) {
            if (font) DeleteObject(font);
            font = CreateFontW(-font_px, 0, 0, 0, bold ? FW_SEMIBOLD : FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0,
                               ANTIALIASED_QUALITY, 0, L"Segoe UI");
            HGDIOBJ previous = SelectObject(dc_, font);
            SIZE extent{};
            GetTextExtentPoint32W(dc_, text.c_str(), static_cast<int>(text.size()), &extent);
            SelectObject(dc_, previous);
            if (extent.cx <= (x1 - x0) * 0.92f || font_px <= 9) break;
            font_px = std::max(9, static_cast<int>(font_px * 0.88f));
        }
        HGDIOBJ old_font = SelectObject(dc_, font);
        RECT r{x0, y0, x1, y1};
        DrawTextW(dc_, text.c_str(), -1, &r, align | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
        SelectObject(dc_, old_font);
        DeleteObject(font);
        GdiFlush();
        for (int y = y0; y < y1; ++y)
            for (int x = x0; x < x1; ++x) {
                const float c = ((p[static_cast<size_t>(y) * w_ + x] >> 16) & 0xFF) / 255.0f;
                if (c > 0.0f) blend(img.pixels[static_cast<size_t>(y) * img.width + x], colour, c);
            }
    }

private:
    int w_, h_;
    HDC dc_ = nullptr;
    HBITMAP bmp_ = nullptr;
    HGDIOBJ old_ = nullptr;
    void* bits_ = nullptr;
};

}  // namespace

sprites::Image render_panel(int width, int height, const std::vector<Item>& items) {
    sprites::Image img{width, height, std::vector<unsigned>(static_cast<size_t>(width) * height, 0u)};
    const float corner = 0.035f * std::min(width, height);
    fill_rounded(img, 0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height), corner, kBackground, kEdge, 2.0f);
    TextLayer text(width, height);
    for (const Item& it : items) {
        const float x0 = it.rect.x0 * width, y0 = it.rect.y0 * height, x1 = it.rect.x1 * width, y1 = it.rect.y1 * height;
        const float size = it.kind == Kind::Button ? 0.46f : it.kind == Kind::Title ? 0.62f : 0.36f;
        const int font = std::max(10, static_cast<int>((y1 - y0) * size));
        if (it.kind == Kind::Button) {
            const Rgba& fill = it.pressed ? kPressed : it.lit ? kLit : it.hover ? kHover : kButton;
            fill_rounded(img, x0, y0, x1, y1, 0.16f * std::min(y1 - y0, x1 - x0), fill, kButtonEdge, it.hover ? 2.0f : 1.0f);
            if (it.icon) {  // icon above, text under it
                const float pad = 0.08f * (y1 - y0), text_h = it.text.empty() ? 0.0f : 0.28f * (y1 - y0);
                draw_image(img, *it.icon, x0 + pad, y0 + pad, x1 - pad, y1 - pad - text_h);
                if (!it.text.empty())
                    text.draw(img, it.text, static_cast<int>(x0) + 2, static_cast<int>(y1 - pad - text_h),
                              static_cast<int>(x1) - 2, static_cast<int>(y1 - pad * 0.5f),
                              std::max(10, static_cast<int>(text_h * 0.55f)), true, kText);
            } else {
                text.draw(img, it.text, static_cast<int>(x0) + 2, static_cast<int>(y0), static_cast<int>(x1) - 2,
                          static_cast<int>(y1), font, true, kText);
            }
        } else {
            text.draw(img, it.text, static_cast<int>(x0), static_cast<int>(y0), static_cast<int>(x1), static_cast<int>(y1),
                      font, it.kind == Kind::Title, it.kind == Kind::Title ? kTitleText : kLabelText, DT_LEFT);
        }
    }
    return img;
}

sprites::Image render_wedge(int size, int slices) {
    sprites::Image img{size, size, std::vector<unsigned>(static_cast<size_t>(size) * size, 0u)};
    const float half = 0.5f * size, step = 2.0f * kPi / std::max(slices, 1);
    for (int y = 0; y < size; ++y)
        for (int x = 0; x < size; ++x) {
            const float dx = (x + 0.5f - half) / half, dy = (half - (y + 0.5f)) / half;
            const float r = std::sqrt(dx * dx + dy * dy);
            if (r > 0.99f || r < kWheelInner) continue;
            const float angle = std::atan2(dx, dy);  // 0 at the top
            const float from_edge = 0.5f * step - std::fabs(angle);
            if (from_edge <= 0.0f) continue;
            const float cov = std::clamp(from_edge * r * half - 1.5f, 0.0f, 1.0f) * std::clamp((0.985f - r) * half, 0.0f, 1.0f) *
                              std::clamp((r - kWheelInner) * half, 0.0f, 1.0f);
            unsigned& px = img.pixels[static_cast<size_t>(y) * size + x];
            blend(px, Rgba{124, 200, 196, 0.38f}, cov);
            const float rim = std::clamp(1.0f - std::fabs(r - 0.97f) * half * 0.3f, 0.0f, 1.0f);
            blend(px, Rgba{220, 255, 250, 0.9f}, rim * cov);
        }
    return img;
}

int hit(const std::vector<Item>& items, float u, float v) {
    for (size_t i = 0; i < items.size(); ++i)
        if (items[i].kind == Kind::Button && items[i].rect.contains(u, v)) return static_cast<int>(i);
    return -1;
}

int wheel_slice(float x, float y, int slices, float inner) {
    const float r = std::sqrt(x * x + y * y);
    if (r > 1.0f) return -2;
    if (r < inner || slices <= 0) return -1;
    float angle = std::atan2(x, y);  // 0 at the top, clockwise
    if (angle < 0.0f) angle += 2.0f * kPi;
    const float step = 2.0f * kPi / slices;
    return static_cast<int>(std::floor((angle + 0.5f * step) / step)) % slices;
}

sprites::Image render_wheel(int size, const std::vector<std::wstring>& labels, const std::wstring& centre, int highlight,
                            const std::vector<const sprites::Image*>& icons) {
    sprites::Image img{size, size, std::vector<unsigned>(static_cast<size_t>(size) * size, 0u)};
    const int n = static_cast<int>(labels.size());
    const float half = 0.5f * size, step = 2.0f * kPi / std::max(n, 1);
    const float gap_px = 3.0f;
    for (int y = 0; y < size; ++y)
        for (int x = 0; x < size; ++x) {
            const float dx = (x + 0.5f - half) / half, dy = (half - (y + 0.5f)) / half;  // y up
            const float r = std::sqrt(dx * dx + dy * dy);
            unsigned& px = img.pixels[static_cast<size_t>(y) * size + x];
            const float outer_cov = std::clamp((0.985f - r) * half, 0.0f, 1.0f);
            if (outer_cov <= 0.0f) continue;
            if (r < kWheelInner - 0.025f) {  // centre disc
                const float cov = std::clamp((kWheelInner - 0.025f - r) * half, 0.0f, 1.0f);
                blend(px, highlight == n ? kHover : kBackground, cov);
                continue;
            }
            const int slice = wheel_slice(dx, dy, n, kWheelInner);
            if (slice < 0) continue;
            // Gaps between slices and round the centre.
            float angle = std::atan2(dx, dy);
            if (angle < 0.0f) angle += 2.0f * kPi;
            const float from_edge = std::fabs(std::remainder(angle - (slice + 0.5f) * step, step));  // to the slice border
            const float gap_cov = std::clamp(from_edge * r * half - gap_px * 0.5f, 0.0f, 1.0f);
            const float inner_cov = std::clamp((r - kWheelInner) * half, 0.0f, 1.0f);
            const float cov = outer_cov * gap_cov * inner_cov;
            blend(px, slice == highlight ? kHover : kButton, cov);
            const float rim = std::clamp(1.0f - std::fabs(r - 0.975f) * half * 0.5f, 0.0f, 1.0f);
            if (rim > 0.0f) blend(px, kButtonEdge, rim * cov);
        }
    TextLayer text(size, size);
    const float label_r = 0.5f * (kWheelInner + 0.985f) * half;
    const int font = std::max(12, static_cast<int>(size * (n > 8 ? 0.042f : 0.05f)));
    const float slice_w = std::min(2.0f * label_r * std::sin(0.5f * step), size * 0.3f);  // chord at the label radius
    for (int i = 0; i < n; ++i) {
        const float a = i * step;
        const float cx = half + std::sin(a) * label_r, cy = half - std::cos(a) * label_r;
        const sprites::Image* icon = i < static_cast<int>(icons.size()) ? icons[i] : nullptr;
        const int w = static_cast<int>(slice_w * 0.95f);
        if (icon) {  // icon in the slice, its name under it
            const float s = std::min(slice_w * 0.62f, (0.985f - kWheelInner) * half * 0.62f);
            draw_image(img, *icon, cx - s * 0.5f, cy - s * 0.62f, cx + s * 0.5f, cy + s * 0.38f);
            text.draw(img, labels[i], static_cast<int>(cx) - w / 2, static_cast<int>(cy + s * 0.38f),
                      static_cast<int>(cx) + w / 2, static_cast<int>(cy + s * 0.38f) + font + font / 2, font, true, kText);
        } else {
            text.draw(img, labels[i], static_cast<int>(cx) - w / 2, static_cast<int>(cy) - font, static_cast<int>(cx) + w / 2,
                      static_cast<int>(cy) + font, font, true, kText);
        }
    }
    const int cw = static_cast<int>(size * 0.4f);
    text.draw(img, centre, static_cast<int>(half) - cw / 2, static_cast<int>(half) - font, static_cast<int>(half) + cw / 2,
              static_cast<int>(half) + font, static_cast<int>(font * 0.85f), false, kLabelText);
    return img;
}

}  // namespace hw2vr::ui
