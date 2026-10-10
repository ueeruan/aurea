#pragma once

#include "aurea/core/Math.hpp"
#include <algorithm>
#include <cmath>
#include <span>

namespace aurea {

// An exposure only needs pixels touched by its actual submitted samples.
// Keep the crop on the composition's output-pixel grid so rasterization and
// subsequent filtering use exactly the same pixel centers as a full target.
struct MotionBlurBounds {
    Rect region{};
    u32 width = 0, height = 0;
};

[[nodiscard]] inline MotionBlurBounds motion_blur_bounds(Rect source, std::span<const Mat4> matrices,
    const Mat4& fold, u32 compWidth, u32 compHeight, u32 targetWidth, u32 targetHeight) noexcept {
    const MotionBlurBounds full{Rect{0, 0, static_cast<f32>(compWidth), static_cast<f32>(compHeight)},
        targetWidth, targetHeight};
    if (!compWidth || !compHeight || !targetWidth || !targetHeight || matrices.empty()) return full;
    const f64 sx = static_cast<f64>(targetWidth) / compWidth, sy = static_cast<f64>(targetHeight) / compHeight;
    f64 left = targetWidth, top = targetHeight, right = 0, bottom = 0;
    for (const Mat4& sample : matrices) {
        const Mat4 matrix = sample * fold;
        for (const Vec2 corner : {Vec2{source.x, source.y}, Vec2{source.x + source.w, source.y},
                Vec2{source.x, source.y + source.h}, Vec2{source.x + source.w, source.y + source.h}}) {
            const Vec4 q = matrix * Vec4{corner.x, corner.y, 0, 1};
            // An edge crossing the camera plane has no finite corner bound.
            // Preserve the original full-frame clipping in that case.
            if (!std::isfinite(q.x) || !std::isfinite(q.y) || !std::isfinite(q.w) || q.w <= 1e-5f) return full;
            const f64 x = static_cast<f64>(q.x) / q.w * sx, y = static_cast<f64>(q.y) / q.w * sy;
            left = std::min(left, x); top = std::min(top, y);
            right = std::max(right, x); bottom = std::max(bottom, y);
        }
    }
    // Two transparent output texels protect bilinear filtering at a cropped
    // edge. At a composition edge the original full target also ends there.
    left = std::clamp(std::floor(left - 2), 0.0, static_cast<f64>(targetWidth - 1));
    top = std::clamp(std::floor(top - 2), 0.0, static_cast<f64>(targetHeight - 1));
    right = std::clamp(std::ceil(right + 2), left + 1, static_cast<f64>(targetWidth));
    bottom = std::clamp(std::ceil(bottom + 2), top + 1, static_cast<f64>(targetHeight));
    return {Rect{static_cast<f32>(left / sx), static_cast<f32>(top / sy),
                 static_cast<f32>((right - left) / sx), static_cast<f32>((bottom - top) / sy)},
        static_cast<u32>(right - left), static_cast<u32>(bottom - top)};
}

} // namespace aurea
