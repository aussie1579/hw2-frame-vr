#include "assets/game_assets.h"

#include <windows.h>

#include <algorithm>
#include <cctype>
#include <cstring>

#include "common/log.h"

// stb_image (third_party/stb): zlib inflate and TGA decoding, implemented in stb_image_impl.cpp.
#define STBI_NO_STDIO
#define STBI_ONLY_TGA
#define STBI_SUPPORT_ZLIB
#include <stb_image.h>

namespace hw2vr::assets {
namespace {

std::string normalise(std::string name) {
    for (char& c : name) c = c == '/' ? '\\' : static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (name.rfind("data:", 0) == 0) name.erase(0, 5);
    while (!name.empty() && name[0] == '\\') name.erase(0, 1);
    return name;
}

bool read_at(HANDLE f, uint64_t offset, void* out, uint32_t size) {
    LARGE_INTEGER pos;
    pos.QuadPart = static_cast<LONGLONG>(offset);
    DWORD got = 0;
    return SetFilePointerEx(f, pos, nullptr, FILE_BEGIN) && ReadFile(f, out, size, &got, nullptr) && got == size;
}

uint32_t u32(const uint8_t* p) {
    uint32_t v;
    std::memcpy(&v, p, 4);
    return v;
}
uint16_t u16(const uint8_t* p) {
    uint16_t v;
    std::memcpy(&v, p, 2);
    return v;
}

// --- DXT block decoding (straight alpha out) ------------------------------------------------------------------------
void colours565(uint16_t c0, uint16_t c1, bool four, uint8_t out[4][4]) {
    auto expand = [](uint16_t c, uint8_t* o) {
        o[0] = static_cast<uint8_t>(((c >> 11) & 31) * 255 / 31);
        o[1] = static_cast<uint8_t>(((c >> 5) & 63) * 255 / 63);
        o[2] = static_cast<uint8_t>((c & 31) * 255 / 31);
        o[3] = 255;
    };
    expand(c0, out[0]);
    expand(c1, out[1]);
    for (int i = 0; i < 3; ++i) {
        if (four) {
            out[2][i] = static_cast<uint8_t>((2 * out[0][i] + out[1][i]) / 3);
            out[3][i] = static_cast<uint8_t>((out[0][i] + 2 * out[1][i]) / 3);
        } else {
            out[2][i] = static_cast<uint8_t>((out[0][i] + out[1][i]) / 2);
            out[3][i] = 0;
        }
    }
    out[2][3] = 255;
    out[3][3] = four ? 255 : 0;
}

void decode_dxt(const uint8_t* src, int w, int h, int format, sprites::Image* out) {
    // format: 1 = DXT1, 3 = DXT3, 5 = DXT5
    const int block = format == 1 ? 8 : 16;
    out->width = w;
    out->height = h;
    out->pixels.assign(static_cast<size_t>(w) * h, 0u);
    for (int by = 0; by < (h + 3) / 4; ++by)
        for (int bx = 0; bx < (w + 3) / 4; ++bx, src += block) {
            uint8_t alpha[16];
            std::fill(std::begin(alpha), std::end(alpha), static_cast<uint8_t>(255));
            const uint8_t* colour = src;
            if (format == 3) {
                for (int i = 0; i < 16; ++i) alpha[i] = static_cast<uint8_t>(((src[i / 2] >> ((i & 1) * 4)) & 15) * 17);
                colour = src + 8;
            } else if (format == 5) {
                const uint8_t a0 = src[0], a1 = src[1];
                uint8_t table[8] = {a0, a1};
                if (a0 > a1)
                    for (int i = 0; i < 6; ++i) table[2 + i] = static_cast<uint8_t>(((6 - i) * a0 + (i + 1) * a1) / 7);
                else {
                    for (int i = 0; i < 4; ++i) table[2 + i] = static_cast<uint8_t>(((4 - i) * a0 + (i + 1) * a1) / 5);
                    table[6] = 0, table[7] = 255;
                }
                uint64_t bits = 0;
                for (int i = 0; i < 6; ++i) bits |= static_cast<uint64_t>(src[2 + i]) << (8 * i);
                for (int i = 0; i < 16; ++i) alpha[i] = table[(bits >> (3 * i)) & 7];
                colour = src + 8;
            }
            const uint16_t c0 = u16(colour), c1 = u16(colour + 2);
            uint8_t palette[4][4];
            colours565(c0, c1, format != 1 || c0 > c1, palette);
            const uint32_t indices = u32(colour + 4);
            for (int i = 0; i < 16; ++i) {
                const int x = bx * 4 + (i & 3), y = by * 4 + (i >> 2);
                if (x >= w || y >= h) continue;
                const uint8_t* c = palette[(indices >> (2 * i)) & 3];
                const uint8_t a = format == 1 ? c[3] : alpha[i];
                out->pixels[static_cast<size_t>(y) * w + x] =
                    static_cast<uint32_t>(a) << 24 | static_cast<uint32_t>(c[0]) << 16 | static_cast<uint32_t>(c[1]) << 8 | c[2];
            }
        }
}

}  // namespace

bool Archive::open(const std::wstring& path) {
    files_.clear();
    path_ = path;
    HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    uint8_t head[204];
    bool ok = read_at(f, 0, head, sizeof head) && std::memcmp(head, "_ARCHIVE", 8) == 0 && u32(head + 8) == 2;
    if (ok) {
        // magic[8] version[4] tool_md5[16] name[128] header_md5[16] toc_size[4] data_offset[4], then the sections
        // (offsets relative to the section header at 180).
        const uint32_t data_offset = u32(head + 176);
        std::vector<uint8_t> toc(data_offset);
        ok = data_offset > 204 && data_offset < (64u << 20) && read_at(f, 0, toc.data(), data_offset);
        if (ok) {
            const uint8_t* sect = toc.data() + 180;
            const uint32_t dir_off = u32(sect + 6), file_off = u32(sect + 12), str_off = u32(sect + 18);
            const uint16_t dir_n = u16(sect + 10), file_n = u16(sect + 16);
            auto cstr = [&](uint32_t off) -> std::string {
                const size_t start = 180 + str_off + off;
                if (start >= toc.size()) return {};
                const char* s = reinterpret_cast<const char*>(toc.data() + start);
                return std::string(s, strnlen(s, toc.size() - start));
            };
            for (uint32_t d = 0; d < dir_n; ++d) {
                const uint8_t* dir = sect + dir_off + 12 * d;
                const std::string dname = cstr(u32(dir));
                const uint16_t first = u16(dir + 8), last = u16(dir + 10);
                for (uint32_t i = first; i < last && i < file_n; ++i) {
                    const uint8_t* fe = sect + file_off + 17 * i;
                    const std::string full = dname.empty() ? cstr(u32(fe)) : dname + "\\" + cstr(u32(fe));
                    files_[normalise(full)] = Entry{fe[4], data_offset + u32(fe + 5), u32(fe + 9), u32(fe + 13)};
                }
            }
        }
    }
    CloseHandle(f);
    if (!ok) files_.clear();
    return ok;
}

bool Archive::read(const std::string& name, std::vector<uint8_t>* out) const {
    auto it = files_.find(normalise(name));
    if (it == files_.end()) return false;
    const Entry& e = it->second;
    HANDLE f = CreateFileW(path_.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    std::vector<uint8_t> raw(e.csize);
    const bool ok = read_at(f, e.offset, raw.data(), e.csize);
    CloseHandle(f);
    if (!ok) return false;
    if (e.flags == 0 || e.csize == e.usize) {
        *out = std::move(raw);
        return true;
    }
    int len = 0;
    char* inflated = stbi_zlib_decode_malloc_guesssize_headerflag(reinterpret_cast<const char*>(raw.data()),
                                                                  static_cast<int>(raw.size()), static_cast<int>(e.usize), &len, 1);
    if (!inflated) return false;
    out->assign(reinterpret_cast<uint8_t*>(inflated), reinterpret_cast<uint8_t*>(inflated) + len);
    stbi_image_free(inflated);
    return true;
}

const Archive& game_archive() {
    static Archive archive;
    static bool tried = false;
    if (!tried) {
        tried = true;
        // Next to the running game (...\HomeworldRM\Bin\Release\HomeworldRM.exe); for the smoke test, which runs
        // elsewhere, next to the installed game that tools\smoke-test.ps1 names in HW2VR_GAME_EXE.
        wchar_t exe[MAX_PATH] = {}, env[MAX_PATH] = {};
        std::wstring candidates[2] = {std::wstring(exe, GetModuleFileNameW(nullptr, exe, MAX_PATH))};
        if (GetEnvironmentVariableW(L"HW2VR_GAME_EXE", env, MAX_PATH)) candidates[1] = env;
        std::wstring path;
        for (const std::wstring& c : candidates) {
            if (c.empty()) continue;
            path = c.substr(0, c.find_last_of(L"\\/")) + L"\\..\\..\\Data\\Homeworld2.big";
            if (archive.open(path)) break;
        }
        if (archive.is_open()) HW2VR_LOG("assets: the game's textures come from %ls", path.c_str());
        else HW2VR_LOG("assets: can't read %ls - the VR menus use text only", path.c_str());
    }
    return archive;
}

bool decode_image(const std::vector<uint8_t>& file, const std::string& name, sprites::Image* out) {
    if (file.size() >= 128 && std::memcmp(file.data(), "DDS ", 4) == 0) {
        const int h = static_cast<int>(u32(file.data() + 12)), w = static_cast<int>(u32(file.data() + 16));
        const uint32_t pf_flags = u32(file.data() + 80), bits = u32(file.data() + 88);
        const char* fourcc = reinterpret_cast<const char*>(file.data() + 84);
        const uint8_t* data = file.data() + 128;
        const size_t avail = file.size() - 128;
        if (w <= 0 || h <= 0 || w > 4096 || h > 4096) return false;
        if (pf_flags & 0x4) {  // FOURCC
            const int format = std::memcmp(fourcc, "DXT1", 4) == 0 ? 1 : std::memcmp(fourcc, "DXT3", 4) == 0 ? 3
                               : std::memcmp(fourcc, "DXT5", 4) == 0 ? 5 : 0;
            const size_t need = static_cast<size_t>((w + 3) / 4) * ((h + 3) / 4) * (format == 1 ? 8 : 16);
            if (!format || avail < need) return false;
            decode_dxt(data, w, h, format, out);
            return true;
        }
        if (bits == 32 && avail >= static_cast<size_t>(w) * h * 4) {  // A8R8G8B8: already 0xAARRGGBB words
            out->width = w;
            out->height = h;
            out->pixels.resize(static_cast<size_t>(w) * h);
            std::memcpy(out->pixels.data(), data, out->pixels.size() * 4);
            return true;
        }
        return false;
    }
    int w = 0, h = 0, channels = 0;
    stbi_uc* rgba = stbi_load_from_memory(file.data(), static_cast<int>(file.size()), &w, &h, &channels, 4);
    if (!rgba) {
        HW2VR_LOG("assets: can't decode %s (%s)", name.c_str(), stbi_failure_reason());
        return false;
    }
    out->width = w;
    out->height = h;
    out->pixels.resize(static_cast<size_t>(w) * h);
    for (size_t i = 0; i < out->pixels.size(); ++i)
        out->pixels[i] = static_cast<uint32_t>(rgba[i * 4 + 3]) << 24 | static_cast<uint32_t>(rgba[i * 4]) << 16 |
                         static_cast<uint32_t>(rgba[i * 4 + 1]) << 8 | rgba[i * 4 + 2];
    stbi_image_free(rgba);
    return true;
}

sprites::Image load_icon(const IconRef& icon) {
    if (!icon.name || !icon.name[0]) return {};
    const Archive& a = game_archive();
    std::vector<uint8_t> file;
    sprites::Image img;
    if (!a.is_open() || !a.read(icon.name, &file) || !decode_image(file, icon.name, &img)) {
        if (a.is_open()) HW2VR_LOG("assets: no icon %s", icon.name);
        return {};
    }
    if (icon.cell_height <= 0 || icon.cell_height >= img.height) return img;
    const int cells = img.height / icon.cell_height;
    const int cell = std::clamp(icon.cell, 0, cells - 1);
    sprites::Image out;
    out.width = img.width;
    out.height = icon.cell_height;
    const ptrdiff_t row = static_cast<ptrdiff_t>(img.width);
    out.pixels.assign(img.pixels.begin() + cell * icon.cell_height * row,
                      img.pixels.begin() + (cell + 1) * icon.cell_height * row);
    return out;
}

}  // namespace hw2vr::assets
