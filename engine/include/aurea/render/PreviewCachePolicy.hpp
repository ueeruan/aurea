#pragma once
#include "aurea/core/Types.hpp"
#include <algorithm>
#include <cmath>

namespace aurea {
// Composed RGBA16F frames. The device memory allocation is
// authoritative; even a single frame must fit before we reserve GPU storage.
inline u32 preview_cache_capacity(u32 width, u32 height, u64 budget) noexcept {
    if (!width || !height) return 0;
    const u64 pixels = static_cast<u64>(width) * height;
    if (pixels > budget / 8) return 0;
    return static_cast<u32>(std::min<u64>(30, budget / (pixels * 8)));
}
inline u32 preview_cache_target(f64 fps, u32 capacity, i64 remaining, f32 speed = 1.f) noexcept {
    if (!capacity || remaining <= 0) return 0;
    // Match the decoder's short look-ahead instead of blocking every Play for
    // half a second of expensive effects. Faster playback needs more frames.
    const f64 rate = std::clamp(std::isfinite(speed) ? std::abs(f64(speed)) : 1., .05, 16.);
    const auto startup = static_cast<u32>(std::clamp(std::ceil(
        std::clamp(std::isfinite(fps) ? fps : 30., 1., 240.) * .18 * rate), 1., 30.));
    return static_cast<u32>(std::min<i64>(std::min(capacity, startup), remaining));
}
inline bool preview_buffer_expired(u64 elapsedNs, u32 ready) noexcept {
    // A partial usable buffer starts promptly; missing media/AI gets a finite
    // first-frame allowance. This never labels incomplete frames as cached.
    return elapsedNs >= (ready ? 350'000'000ull : 1'200'000'000ull);
}
inline u32 preview_buffer_status(u32 ready, u32 target, bool buffering, bool limited = false) noexcept {
    return std::min(ready, 255u) | (std::min(target, 255u) << 8)
        | (buffering ? 0x80000000u : 0u) | (limited ? 0x40000000u : 0u);
}

// Idle preparation has its own clock. It never advances playback or extends
// the cache budget; a whole window must fit in the existing composition cache.
inline u32 preview_idle_target(f64 fps, u32 capacity, i64 remaining) noexcept {
    if (!capacity || remaining <= 0) return 0;
    const u32 second = static_cast<u32>(std::clamp(std::ceil(
        std::isfinite(fps) ? fps : 30.), 1., 30.));
    return static_cast<u32>(std::min<i64>(std::min(capacity, second), remaining));
}

struct PreviewIdleKey {
    u64 session = 0, revision = 0, composition = 0, mediaEpoch = 0;
    i64 playhead = 0;
    bool operator==(const PreviewIdleKey&) const = default;
};

class PreviewIdleBuffer {
    PreviewIdleKey key_{};
    u64 due_ = 0, blockedMedia_ = 0;
    bool active_ = false, done_ = false, blocked_ = false;
public:
    void observe(bool eligible, PreviewIdleKey key, u64 now, bool changed) noexcept {
        if (!eligible) { *this = {}; return; }
        if (!active_ || key != key_ || changed) {
            key_ = key; active_ = true; done_ = blocked_ = false;
            due_ = now + 650'000'000ull;
        }
    }
    u64 deadline(u64 mediaReady) const noexcept {
        return active_ && !done_ && (!blocked_ || mediaReady != blockedMedia_) ? due_ : 0;
    }
    bool ready(u64 now, u64 mediaReady) const noexcept {
        const u64 due = deadline(mediaReady);
        return due && now >= due;
    }
    void attempted(u64 now, u64 workNs, bool complete, u64 mediaReady) noexcept {
        blocked_ = !complete; blockedMedia_ = mediaReady;
        // Give interactive work priority even when a composition is cheap.
        // Pending media parks until a completion callback instead of polling GPU.
        const u64 rest = complete ? std::clamp<u64>(workNs, 32'000'000ull, 250'000'000ull)
                                  : 100'000'000ull;
        due_ = now + rest;
    }
    void finish() noexcept { done_ = true; due_ = 0; }
};
}
