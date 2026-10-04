#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace aurea {
// About 180 ms of source video, bounded by decoder leases and memory capacity.
// Keep this rule common to paused warm-up and playing refill.
inline std::uint32_t preview_buffer_frames(std::int64_t frameUs, float speed, std::uint32_t capacity) noexcept {
    if(frameUs<=0||!capacity)return 0;
    const double rate=std::isfinite(speed)?std::clamp(double(std::abs(speed)),.05,16.):1.;
    const auto desired=std::uint32_t(std::clamp(std::ceil(180000.*rate/double(frameUs)),1.,8.));
    return std::min(capacity,desired);
}
}
