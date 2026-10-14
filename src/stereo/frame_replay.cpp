#include "stereo/frame_replay.h"

#include <windows.h>

#include <cstdio>
#include <vector>

#include "common/log.h"
#include "proxy/gl_record_hooks.h"
#include "proxy/real_gl.h"
#include "stereo/gl_snapshot.h"

namespace hw2vr::replay {

bool g_recording = false;
bool g_replaying = false;

namespace {

std::vector<Cmd> g_cmds;
std::vector<uint8_t> g_arena;
const char* g_why_not = nullptr;
long g_unrecorded_at_start = 0;
bool g_frame_recorded = false;
Stats g_stats = {};

using PFN_DeleteNames = void(__stdcall*)(int, const unsigned*);
std::vector<unsigned> g_deferred[3];
PFN_DeleteNames g_delete_fn[3] = {};

PFN_DeleteNames load_delete(Kind kind) {
    if (g_delete_fn[static_cast<int>(kind)]) return g_delete_fn[static_cast<int>(kind)];
    HMODULE m = proxy::real_opengl32();
    using PFN_wglGetProcAddress = PROC(WINAPI*)(LPCSTR);
    auto wgl = reinterpret_cast<PFN_wglGetProcAddress>(GetProcAddress(m, "wglGetProcAddress"));
    PFN_DeleteNames fn = nullptr;
    switch (kind) {
        case Kind::Buffers: fn = reinterpret_cast<PFN_DeleteNames>(wgl("glDeleteBuffers")); break;
        case Kind::VertexArrays: fn = reinterpret_cast<PFN_DeleteNames>(wgl("glDeleteVertexArrays")); break;
        case Kind::Textures: fn = reinterpret_cast<PFN_DeleteNames>(GetProcAddress(m, "glDeleteTextures")); break;
    }
    return g_delete_fn[static_cast<int>(kind)] = fn;
}

}  // namespace

void push(const Cmd& cmd) { g_cmds.push_back(cmd); }

uint32_t store(const void* data, size_t bytes) {
    if (!data) return kNull;
    const size_t offset = (g_arena.size() + 15) & ~size_t(15);
    g_arena.resize(offset + bytes);
    std::memcpy(g_arena.data() + offset, data, bytes);
    return static_cast<uint32_t>(offset);
}

const void* payload(uint32_t offset) { return offset == kNull ? nullptr : g_arena.data() + offset; }

void mark_unreplayable(const char* why) {
    if (!g_why_not) g_why_not = why;
}

void begin_frame(bool record) {
    g_cmds.clear();
    g_arena.clear();
    g_why_not = nullptr;
    g_frame_recorded = record;
    g_unrecorded_at_start = g_hw2vr_unrecorded_calls;
    if (record) snapshot_frame_start();
    g_recording = record;
}

void end_recording() { g_recording = false; }

bool replayable() {
    if (!g_frame_recorded) {
        mark_unreplayable("frame was not recorded");
    } else if (g_hw2vr_unrecorded_calls != g_unrecorded_at_start) {
        static char why[96];
        std::snprintf(why, sizeof why, "unrecorded GL call %s", record::unrecorded_call_name(g_hw2vr_last_unrecorded));
        mark_unreplayable(why);
    } else if (g_cmds.empty()) {
        mark_unreplayable("empty frame");
    }
    return g_why_not == nullptr;
}

const char* why_not() { return g_why_not ? g_why_not : "replayable"; }

void run() {
    restore_frame_start();
    g_replaying = true;
    for (const Cmd& c : g_cmds) c.fn(c);
    g_replaying = false;
    ++g_stats.replays;
}

size_t command_count() { return g_cmds.size(); }
size_t payload_bytes() { return g_arena.size(); }

Stats stats() {
    Stats s = g_stats;
    s.commands = static_cast<unsigned>(g_cmds.size());
    s.bytes = static_cast<unsigned>(g_arena.size());
    return s;
}
void note_skipped() { ++g_stats.skipped; }

bool defer_delete(Kind kind, int n, const unsigned* names) {
    if (!g_recording || n <= 0 || !names) return false;
    auto& list = g_deferred[static_cast<int>(kind)];
    list.insert(list.end(), names, names + n);
    return true;
}

void flush_deferred_deletes() {
    for (int k = 0; k < 3; ++k) {
        auto& list = g_deferred[k];
        if (list.empty()) continue;
        if (PFN_DeleteNames fn = load_delete(static_cast<Kind>(k))) fn(static_cast<int>(list.size()), list.data());
        list.clear();
    }
}

}  // namespace hw2vr::replay
