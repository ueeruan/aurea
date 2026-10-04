#pragma once
#include "aurea/core/Types.hpp"
#include <algorithm>
#include <cmath>

namespace aurea {
// Half a second of composed RGBA16F frames. The device memory allocation is
// authoritative; even a single frame must fit before we reserve GPU storage.
inline u32 preview_cache_capacity(u32 width, u32 height, u64 budget) noexcept {
    if (!width || !height) return 0;
    const u64 pixels = static_cast<u64>(width) * height;
    if (pixels > budget / 8) return 0;
    return static_cast<u32>(std::min<u64>(30, budget / (pixels * 8)));
}
inline u32 preview_cache_target(f64 fps, u32 capacity, i64 remaining) noexcept {
    if (!capacity || remaining <= 0) return 0;
    const u32 halfSecond = static_cast<u32>(std::ceil(std::clamp(std::isfinite(fps) ? fps : 30., 1., 60.) * .5));
    return static_cast<u32>(std::min<i64>(std::min(capacity, halfSecond), remaining));
}
inline u32 preview_buffer_status(u32 ready, u32 target, bool buffering, bool limited = false) noexcept {
    return std::min(ready, 255u) | (std::min(target, 255u) << 8)
        | (buffering ? 0x80000000u : 0u) | (limited ? 0x40000000u : 0u);
}
}
