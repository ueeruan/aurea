#pragma once
#include "aurea/core/Math.hpp"
#include <algorithm>
#include <cmath>

namespace aurea {
/// Captured once on pointer-count changes. Screen deltas use composition pixels.
/// Keeping the basis fixed prevents queued transforms from feeding back into drag.
[[nodiscard]] inline Vec3 preview_gesture_value(const f32* basis, f32 dx, f32 dy, bool rotate) noexcept {
    if (!basis || !std::isfinite(dx) || !std::isfinite(dy)) return {};
    if (rotate) return Vec3{basis[9] - dy * basis[12], basis[10] + dx * basis[12], basis[11]};
    return Vec3{basis[0] + dx * basis[3] + dy * basis[6],
                basis[1] + dx * basis[4] + dy * basis[7],
                basis[2] + dx * basis[5] + dy * basis[8]};
}
/// Uniform pinch scaling preserves signs, aspect ratio and (for 3D) depth.
/// Existing scales outside the gesture range must not jump on first contact:
/// include factor 1 in the interval, and only allow motion toward the range.
[[nodiscard]] inline f32 clamp_pinch_factor(f32 desired, f32 x, f32 y, f32 z, bool threeD) noexcept {
    if (!std::isfinite(desired) || desired <= 0) return 1.f;
    const f32 axes[]{x, y, z};
    f64 low = 0.0, high = 1e6;
    bool nonzero = false;
    for (u32 i = 0; i < (threeD ? 3u : 2u); ++i) {
        if (!std::isfinite(axes[i])) return 1.f;
        const f64 magnitude = std::fabs(static_cast<f64>(axes[i]));
        if (magnitude == 0) continue; // A flat/zero axis stays zero, never divides by zero.
        nonzero = true;
        low = std::max(low, std::min(1.0, .001 / magnitude));
        high = std::min(high, std::max(1.0, 100.0 / magnitude));
    }
    return nonzero ? static_cast<f32>(std::clamp(static_cast<f64>(desired), low, high)) : 1.f;
}
}
