#pragma once

// Keeps the game's world markers (selection brackets, health bars, names) at a steady depth from frame to frame.
// Each frame a marker's depth comes from the depth grid behind it (stereo/depth_probe.h) or, when the grid finds
// nothing there, from the marker drawn just before it; either can flip between two values from one frame to the
// next, and a marker that changes depth every frame jumps between the eyes ("vibrating"). Each marker is matched to
// the previous frame's marker in the same direction (a frame that doesn't turn with the head, so head motion doesn't
// break the match): small changes are eased in, a big change is taken only once it has held for a few frames, and a
// marker the grid misses keeps the depth it had. Pure: covered by tests/math_test.cpp.
#include <cmath>
#include <utility>
#include <vector>

#include "common/vec3.h"

namespace hw2vr::hud {

class MarkerMemory {
public:
    // dir: unit direction of the marker's centre; size: its angular radius (radians); measured: the grid's depth
    // when anchored; fallback: the depth to use for a marker never seen before that the grid missed.
    float resolve(const Vec3& dir, float size, bool anchored, float measured, float fallback) {
        const Entry* prev = match(dir, size);
        Entry e{dir, size, anchored ? measured : fallback, 0.0f, 0};
        if (prev) {
            e.depth = prev->depth;
            e.pending = prev->pending;
            e.pending_frames = prev->pending_frames;
            if (anchored) {
                if (std::fabs(measured - prev->depth) <= kSmallChange * prev->depth) {
                    e.depth = prev->depth + kEase * (measured - prev->depth);
                    e.pending_frames = 0;
                } else if (prev->pending_frames > 0 && std::fabs(measured - prev->pending) <= kSmallChange * prev->pending) {
                    e.pending_frames = prev->pending_frames + 1;
                    if (e.pending_frames >= kHoldFrames) e.depth = measured, e.pending_frames = 0;
                    else ++held_;
                } else {
                    e.pending = measured;
                    e.pending_frames = 1;
                    ++held_;
                }
            }
        }
        cur_.push_back(e);
        return e.depth;
    }

    // After the frame: its markers become the ones the next frame is matched against.
    void end_frame() {
        prev_ = std::move(cur_);
        cur_.clear();
    }

    void clear() { prev_.clear(), cur_.clear(); }
    unsigned held() const { return held_; }  // big depth changes held back so far (each a jump that didn't happen)

private:
    static constexpr float kMinMatch = 0.0105f;  // radians (0.6 degrees)
    static constexpr float kSmallChange = 0.25f; // relative depth change that is eased in rather than held back
    static constexpr float kEase = 0.3f;
    static constexpr int kHoldFrames = 3;

    struct Entry {
        Vec3 dir;
        float size;
        float depth;
        float pending;       // a different depth seen lately
        int pending_frames;  // ... for this many frames in a row
    };

    const Entry* match(const Vec3& dir, float size) const {
        const Entry* best = nullptr;
        float best_angle = 0.0f;
        for (const Entry& e : prev_) {
            if (e.size > 2.0f * size + kMinMatch || size > 2.0f * e.size + kMinMatch) continue;  // a different marker
            const float angle = std::acos(std::fmin(1.0f, dot(dir, e.dir)));
            const float limit = std::fmax(kMinMatch, 0.6f * std::fmax(size, e.size));
            if (angle <= limit && (!best || angle < best_angle)) best = &e, best_angle = angle;
        }
        return best;
    }

    std::vector<Entry> prev_, cur_;
    unsigned held_ = 0;
};

}  // namespace hw2vr::hud
