#pragma once

// Synchronized stereo by GL-level replay. While the game renders a frame (the left eye), every GL call that affects
// rendering is recorded; at wglSwapBuffers the recorded stream is replayed with the right eye's camera, so both
// eyes show the same moment every frame. The engine is never re-entered, so no game state advances twice
// (docs/FINDINGS.md "Engine frame structure").
//
// x86 stdcall passes every GL argument as raw 32-bit stack words (doubles as two), so a call is stored as its
// argument words plus the function that replays it with the original types (Sig<>::hook / Sig<>::replay below).
// Pointer arguments (buffer data, uniform arrays) are copied into a per-frame arena. Anything the recorder does not
// capture is counted (exports.cpp counting stubs, counting thunks for extensions) and makes the frame unreplayable.
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>
#include <utility>

namespace hw2vr::replay {

struct Cmd;
using ReplayFn = void (*)(const Cmd&);
constexpr int kMaxWords = 11;
struct Cmd {
    ReplayFn fn;
    uint32_t w[kMaxWords];
};

extern bool g_recording;  // hot-path flag, read by every recording hook
extern bool g_replaying;

void push(const Cmd& cmd);
uint32_t store(const void* data, size_t bytes);  // copy a pointer payload into the arena; returns its offset
const void* payload(uint32_t offset);
constexpr uint32_t kNull = 0xFFFFFFFFu;          // "pointer was NULL"
void mark_unreplayable(const char* why);         // static strings only

void begin_frame(bool record);  // after the real swap: snapshot GL state, reset the buffers
void end_recording();           // at wglSwapBuffers, before any of our own GL work
bool replayable();
const char* why_not();
void run();                     // replay the recorded frame (restores the frame-start state first)
size_t command_count();
size_t payload_bytes();

struct Stats {
    unsigned replays;    // frames replayed for the second eye
    unsigned skipped;    // frames that could not be replayed (fallback)
    unsigned commands;   // in the last recorded frame
    unsigned bytes;      // payload bytes in the last recorded frame
};
Stats stats();
void note_skipped();

// Deletes during a recorded frame are deferred until after the replay, so the names stay valid for it.
enum class Kind { Buffers, VertexArrays, Textures };
bool defer_delete(Kind kind, int n, const unsigned* names);  // false: not recording, delete now
void flush_deferred_deletes();

// ---- typed record/replay -----------------------------------------------------------------------------------------
template <class T>
constexpr int kWords = static_cast<int>((sizeof(T) + 3) / 4);

template <class... A>
struct Layout {
    static constexpr int total = (0 + ... + kWords<A>);
    static constexpr std::array<int, sizeof...(A)> offsets = [] {
        std::array<int, sizeof...(A)> o{};
        int acc = 0, i = 0;
        ((o[i++] = acc, acc += kWords<A>), ...);
        return o;
    }();
};

template <class T>
T load_word(const uint32_t* w) {
    T v;
    std::memcpy(&v, w, sizeof(T));
    return v;
}

template <class... A>
void pack(uint32_t* w, A... a) {
    int i = 0;
    ((std::memcpy(w + i, &a, sizeof(A)), i += kWords<A>), ...);
}

template <class Fn>
struct Sig;
template <class R, class... A>
struct Sig<R(__stdcall*)(A...)> {
    static_assert(Layout<A...>::total <= kMaxWords, "too many argument words for a Cmd");

    template <auto& Fn, size_t... I>
    static void call(const uint32_t* w, std::index_sequence<I...>) {
        Fn(load_word<A>(w + Layout<A...>::offsets[I])...);
    }
    // Replays a recorded call through Fn (a function-pointer variable: a real GL entry point or one of our hooks).
    template <auto& Fn>
    static void replay(const Cmd& c) {
        call<Fn>(c.w, std::index_sequence_for<A...>{});
    }
    // Records a call that will later be replayed through Fn.
    template <auto& Fn>
    static void record(A... a) {
        Cmd c;
        c.fn = &replay<Fn>;
        pack(c.w, a...);
        push(c);
    }
    // A complete scalar-argument hook: record while recording, then call the real function.
    template <auto& Real>
    static R __stdcall hook(A... a) {
        if (g_recording) record<Real>(a...);
        return Real(a...);
    }
};

template <auto& Fn>
using SigOf = Sig<std::remove_cvref_t<decltype(Fn)>>;

}  // namespace hw2vr::replay
