#include "hud/marker_geometry.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <unordered_map>
#include <vector>

namespace hw2vr::markers {
namespace {

constexpr unsigned kArrayBuffer = 0x8892, kElementArrayBuffer = 0x8893;
constexpr unsigned kFloat = 0x1406, kUnsignedByte = 0x1401, kUnsignedShort = 0x1403, kUnsignedInt = 0x1405;
constexpr std::ptrdiff_t kMaxShadow = 256 * 1024;  // HUD streaming buffers are a few KB; meshes are left alone
constexpr size_t kMaxTotal = 48u << 20;
constexpr int kMaxVertices = 8192;                 // per draw, to bound the cost of a huge one

struct Attrib {
    unsigned buffer = 0;
    int size = 0;
    unsigned type = 0;
    int stride = 0;
    std::uintptr_t offset = 0;
};
struct Vao {
    Attrib position;  // attribute 0
    bool has_position = false;
    unsigned element_buffer = 0;
};
struct Shadow {
    std::vector<uint8_t> bytes;
    unsigned version = 0;
};

std::unordered_map<unsigned, Vao> g_vaos;  // 0 = the default VAO
std::unordered_map<unsigned, Shadow> g_shadows;
size_t g_total = 0;
unsigned g_vao = 0, g_array_buffer = 0;

// The last answer, for the same draw repeated (the game draws each outline five times with pixel offsets).
struct Cache {
    unsigned vao = 0, vb = 0, vb_version = 0, eb = 0, eb_version = 0, index_type = 0;
    std::ptrdiff_t first = -1;
    int count = -1;
    bool elements = false, ok = false;
    float mv[16] = {}, proj[16] = {}, out[4] = {};
} g_cache;

Shadow* shadow_of(unsigned buffer) {
    auto it = g_shadows.find(buffer);
    return it == g_shadows.end() ? nullptr : &it->second;
}

void drop(unsigned buffer) {
    auto it = g_shadows.find(buffer);
    if (it == g_shadows.end()) return;
    g_total -= it->second.bytes.size();
    g_shadows.erase(it);
}

unsigned target_buffer(unsigned target) {
    if (target == kArrayBuffer) return g_array_buffer;
    if (target == kElementArrayBuffer) return g_vaos[g_vao].element_buffer;
    return 0;
}

}  // namespace

void on_bind_vertex_array(unsigned vao) { g_vao = vao; }

void on_bind_buffer(unsigned target, unsigned buffer) {
    if (target == kArrayBuffer) g_array_buffer = buffer;
    else if (target == kElementArrayBuffer) g_vaos[g_vao].element_buffer = buffer;
}

void on_vertex_attrib_pointer(unsigned index, int size, unsigned type, int stride, const void* pointer) {
    if (index != 0) return;
    Vao& v = g_vaos[g_vao];
    v.position = {g_array_buffer, size, type, stride, reinterpret_cast<std::uintptr_t>(pointer)};
    v.has_position = g_array_buffer != 0;
}

void on_buffer_data(unsigned target, std::ptrdiff_t size, const void* data) {
    const unsigned buffer = target_buffer(target);
    if (!buffer) return;
    if (!data || size <= 0 || size > kMaxShadow) {
        drop(buffer);
        return;
    }
    Shadow* s = shadow_of(buffer);
    const size_t old = s ? s->bytes.size() : 0;
    if (!s && g_total + static_cast<size_t>(size) > kMaxTotal) return;
    Shadow& sh = g_shadows[buffer];
    sh.bytes.assign(static_cast<const uint8_t*>(data), static_cast<const uint8_t*>(data) + size);
    ++sh.version;
    g_total = g_total - old + sh.bytes.size();
}

void on_buffer_sub_data(unsigned target, std::ptrdiff_t offset, std::ptrdiff_t size, const void* data) {
    const unsigned buffer = target_buffer(target);
    Shadow* s = buffer ? shadow_of(buffer) : nullptr;
    if (!s) return;
    if (!data || offset < 0 || size < 0 || static_cast<size_t>(offset + size) > s->bytes.size()) {
        drop(buffer);  // outside what we know: forget rather than be wrong
        return;
    }
    std::memcpy(s->bytes.data() + offset, data, static_cast<size_t>(size));
    ++s->version;
}

void on_delete_buffers(int n, const unsigned* names) {
    for (int i = 0; i < n; ++i) drop(names[i]);
}

void on_delete_vertex_arrays(int n, const unsigned* names) {
    for (int i = 0; i < n; ++i)
        if (names[i] != 0) g_vaos.erase(names[i]);
}

bool screen_bounds(bool elements, std::ptrdiff_t first, int count, unsigned index_type, const float mv[16],
                   const float proj[16], float out[4]) {
    auto vit = g_vaos.find(g_vao);
    if (vit == g_vaos.end() || !vit->second.has_position || count <= 0) return false;
    const Vao& vao = vit->second;
    const Attrib& a = vao.position;
    if (a.type != kFloat || a.size < 2) return false;
    const Shadow* vb = shadow_of(a.buffer);
    const Shadow* eb = elements ? shadow_of(vao.element_buffer) : nullptr;
    if (!vb || (elements && !eb)) return false;

    Cache& c = g_cache;
    if (c.vao == g_vao && c.vb == a.buffer && c.vb_version == vb->version && c.elements == elements &&
        c.first == first && c.count == count && c.index_type == index_type &&
        (!elements || (c.eb == vao.element_buffer && c.eb_version == eb->version)) &&
        std::memcmp(c.mv, mv, sizeof c.mv) == 0 && std::memcmp(c.proj, proj, sizeof c.proj) == 0) {
        std::memcpy(out, c.out, sizeof c.out);
        return c.ok;
    }

    const size_t stride = a.stride ? static_cast<size_t>(a.stride) : sizeof(float) * static_cast<size_t>(a.size);
    const size_t index_size = index_type == kUnsignedShort ? 2 : index_type == kUnsignedInt ? 4 : index_type == kUnsignedByte ? 1 : 0;
    if (elements && index_size == 0) return false;
    float x0 = 1e30f, y0 = 1e30f, x1 = -1e30f, y1 = -1e30f;
    bool any = false;
    const int n = std::min(count, kMaxVertices);
    for (int i = 0; i < n; ++i) {
        size_t vertex;
        if (elements) {
            const size_t at = static_cast<size_t>(first) + static_cast<size_t>(i) * index_size;
            if (first < 0 || at + index_size > eb->bytes.size()) break;
            const uint8_t* p = eb->bytes.data() + at;
            vertex = index_size == 2 ? *reinterpret_cast<const uint16_t*>(p)
                     : index_size == 4 ? *reinterpret_cast<const uint32_t*>(p)
                                       : *p;
        } else {
            vertex = static_cast<size_t>(first + i);
        }
        const size_t at = a.offset + vertex * stride;
        if (at + sizeof(float) * static_cast<size_t>(a.size) > vb->bytes.size()) continue;
        float pos[4] = {0.0f, 0.0f, 0.0f, 1.0f};
        std::memcpy(pos, vb->bytes.data() + at, sizeof(float) * static_cast<size_t>(std::min(a.size, 3)));
        float eye[4], clip[4];
        for (int r = 0; r < 4; ++r) eye[r] = mv[r] * pos[0] + mv[4 + r] * pos[1] + mv[8 + r] * pos[2] + mv[12 + r];
        for (int r = 0; r < 4; ++r)
            clip[r] = proj[r] * eye[0] + proj[4 + r] * eye[1] + proj[8 + r] * eye[2] + proj[12 + r] * eye[3];
        if (clip[3] <= 1e-6f) continue;
        const float sx = (clip[0] / clip[3] + 1.0f) * 0.5f, sy = (clip[1] / clip[3] + 1.0f) * 0.5f;
        x0 = std::min(x0, sx), y0 = std::min(y0, sy), x1 = std::max(x1, sx), y1 = std::max(y1, sy);
        any = true;
    }
    c.vao = g_vao, c.vb = a.buffer, c.vb_version = vb->version, c.elements = elements, c.first = first;
    c.count = count, c.index_type = index_type, c.eb = vao.element_buffer, c.eb_version = eb ? eb->version : 0;
    std::memcpy(c.mv, mv, sizeof c.mv);
    std::memcpy(c.proj, proj, sizeof c.proj);
    c.ok = any;
    c.out[0] = x0, c.out[1] = y0, c.out[2] = x1, c.out[3] = y1;
    std::memcpy(out, c.out, sizeof c.out);
    return any;
}

}  // namespace hw2vr::markers
