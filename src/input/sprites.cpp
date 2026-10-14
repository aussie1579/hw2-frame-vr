#include "input/sprites.h"

#include <windows.h>

#include <algorithm>
#include <cmath>

namespace hw2vr::sprites {
namespace {

unsigned argb(float alpha, int r, int g, int b) {
    const unsigned a = static_cast<unsigned>(255.0f * std::clamp(alpha, 0.0f, 1.0f) + 0.5f);
    return a << 24 | static_cast<unsigned>(r) << 16 | static_cast<unsigned>(g) << 8 | static_cast<unsigned>(b);
}

std::vector<std::wstring> split_lines(const std::wstring& text) {
    std::vector<std::wstring> lines;
    size_t start = 0;
    while (true) {
        const size_t nl = text.find(L'\n', start);
        lines.push_back(text.substr(start, nl == std::wstring::npos ? std::wstring::npos : nl - start));
        if (nl == std::wstring::npos) break;
        start = nl + 1;
    }
    return lines;
}

}  // namespace

Image beam() {
    constexpr int kLength = 64, kThickness = 8;
    Image img{kLength, kThickness, std::vector<unsigned>(kLength * kThickness)};
    for (int y = 0; y < kThickness; ++y)
        for (int x = 0; x < kLength; ++x) {
            const float across = 1.0f - std::fabs((y + 0.5f) / kThickness * 2.0f - 1.0f);  // 0 at the edges
            const float along = 1.0f - 0.5f * x / (kLength - 1);
            img.pixels[y * kLength + x] = argb(across * along * 0.9f, 140, 200, 255);
        }
    return img;
}

Image grab_bar() {
    constexpr int kW = 256, kH = 32;
    Image img{kW, kH, std::vector<unsigned>(kW * kH)};
    const float radius = kH / 2.0f - 1.0f, rim = 3.0f, cy = (kH - 1) / 2.0f;
    const float left = radius + 0.5f, right = kW - 1.5f - radius;
    for (int y = 0; y < kH; ++y)
        for (int x = 0; x < kW; ++x) {
            const float cx = std::clamp(static_cast<float>(x), left, right);
            const float d = std::sqrt((x - cx) * (x - cx) + (y - cy) * (y - cy));  // from the capsule's spine
            const float outer = std::clamp(radius - d + 0.5f, 0.0f, 1.0f);
            const float inner = std::clamp(radius - rim - d + 0.5f, 0.0f, 1.0f);
            const int level = static_cast<int>(50 + 190 * (outer > 0.0f ? inner / outer : 0.0f));
            img.pixels[y * kW + x] = argb(outer * 0.9f, level, level, level);
        }
    return img;
}

Image text_panel(const std::wstring& text, int width_px) {
    const std::vector<std::wstring> lines = split_lines(text);
    constexpr int kLineHeight = 46, kPadding = 24;
    const int w = width_px, h = kPadding * 2 + static_cast<int>(lines.size()) * kLineHeight;
    Image img{w, h, std::vector<unsigned>(static_cast<size_t>(w) * h)};

    // GDI draws white-on-black into a DIB; the grey level becomes text coverage (GDI leaves alpha at 0).
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof bi.bmiHeader;
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h;  // top-down
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HDC dc = CreateCompatibleDC(nullptr);
    HBITMAP bmp = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!dc || !bmp || !bits) {
        if (bmp) DeleteObject(bmp);
        if (dc) DeleteDC(dc);
        return img;
    }
    HGDIOBJ old_bmp = SelectObject(dc, bmp);
    HFONT title = CreateFontW(-30, 0, 0, 0, FW_SEMIBOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, ANTIALIASED_QUALITY, 0, L"Segoe UI");
    HFONT body = CreateFontW(-28, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, ANTIALIASED_QUALITY, 0, L"Segoe UI");
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(255, 255, 255));
    std::vector<int> line_of_row(h, -1);
    for (size_t i = 0; i < lines.size(); ++i) {
        HGDIOBJ old_font = SelectObject(dc, i == 0 ? title : body);
        RECT r{kPadding, kPadding + static_cast<int>(i) * kLineHeight, w - kPadding, kPadding + static_cast<int>(i + 1) * kLineHeight};
        DrawTextW(dc, lines[i].c_str(), -1, &r, DT_LEFT | DT_TOP | DT_SINGLELINE | DT_NOPREFIX);
        SelectObject(dc, old_font);
        for (int y = r.top; y < r.bottom && y < h; ++y) line_of_row[y] = static_cast<int>(i);
    }
    GdiFlush();
    const auto* src = static_cast<const unsigned*>(bits);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            const float c = ((src[y * w + x] >> 16) & 0xFF) / 255.0f;  // text coverage
            const bool is_title = line_of_row[y] == 0;
            const int tr = 255, tg = is_title ? 160 : 255, tb = is_title ? 60 : 255;  // orange title, white body
            const auto mix = [c](int bg, int fg) { return static_cast<int>(bg + (fg - bg) * c + 0.5f); };
            img.pixels[static_cast<size_t>(y) * w + x] =
                argb(0.88f + 0.12f * c, mix(20, tr), mix(22, tg), mix(28, tb));
        }
    SelectObject(dc, old_bmp);
    DeleteObject(title);
    DeleteObject(body);
    DeleteObject(bmp);
    DeleteDC(dc);
    return img;
}

}  // namespace hw2vr::sprites
