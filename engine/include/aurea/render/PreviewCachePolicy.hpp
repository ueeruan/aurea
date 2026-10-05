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
}
