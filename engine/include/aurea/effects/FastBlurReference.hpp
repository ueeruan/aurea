#pragma once
#include "aurea/effects/Effect.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace aurea::fast_blur_reference {
inline constexpr const char *kKey = "aurea.blur.fast_blur_reference";
inline constexpr u32 kMaxSegments = 24;
struct Segment {
    i32 low = 0, high = 0;
    f64 anchor = 0, direction = 1, scale = 1;
    f64 a = 0, b = 0, c = 0;
};

// Three fractional boxes equal a weighted combination of four integer-box
// convolutions. On either half of their support, at most one excluded interval
// contributes. Coefficients are formed in double precision before GPU upload.
inline std::vector<Segment> box_segments(f64 radius) {
    const i32 n = static_cast<i32>(std::floor(radius));
    const f64 fraction = radius - n;
    const f64 denominator = std::pow(2 * radius + 1, 3);
    const i32 length = 2 * n + 1;
    std::vector<i32> boundaries{0};
    for (i32 k = 0; k <= 3; ++k) {
        const i32 support = 3 * n + k;
        boundaries.push_back(support + 1);
        if (support - length >= 0)
            boundaries.push_back(support - length + 1);
        if (support - length - 2 >= 0)
            boundaries.push_back(support - length - 1);
    }
    std::sort(boundaries.begin(), boundaries.end());
    boundaries.erase(std::unique(boundaries.begin(), boundaries.end()), boundaries.end());
    std::vector<Segment> result;
    constexpr f64 combinations[]{1, 3, 3, 1};
    for (usize index = 0; index + 1 < boundaries.size(); ++index) {
        const i32 low = boundaries[index], high = boundaries[index + 1] - 1;
        f64 a = 0, b = 0, c = 0;
        for (i32 k = 0; k <= 3; ++k) {
            const f64 weight =
                combinations[k] * std::pow(fraction, k) * std::pow(1 - fraction, 3 - k) / denominator;
            const i32 support = 3 * n + k;
            auto add = [&](i32 offset, f64 multiplier) {
                if (low > offset)
                    return;
                a += weight * multiplier * .5 * (offset + 1.) * (offset + 2.);
                b -= weight * multiplier * (offset + 1.5);
                c += weight * multiplier * .5;
            };
            add(support, 1);
            add(support - length, -(3 - k));
            add(support - length - 2, -k);
        }
        if (a == 0 && b == 0 && c == 0)
            continue;
        const f64 scale = high - low + 1.;
        const f64 atOuter = (c * high + b) * high + a;
        const f64 inward = -(b + 2 * c * high) * scale;
        const f64 quadratic = c * scale * scale;
        result.push_back({low, high, static_cast<f64>(high), -1, scale, atOuter, inward, quadratic});
        if (high > 0)
            result.push_back(
                {-high, -std::max(low, 1), static_cast<f64>(-high), 1, scale, atOuter, inward, quadratic});
    }
    return result;
}

struct GaussianKernel {
    std::array<f32, 4> weights{};
    u32 radius = 0;
};
inline GaussianKernel gaussian_kernel(f64 radius) noexcept {
    GaussianKernel kernel;
    kernel.radius = static_cast<u32>(std::ceil(radius));
    f64 sum = 255;
    kernel.weights[0] = 255;
    for (u32 i = 1; i <= kernel.radius && i < kernel.weights.size(); ++i) {
        const f64 position = i / (radius + 1);
        const f64 value = std::floor(255 * (1 - 1.102 * (1 - std::exp(-2.378 * position * position))));
        kernel.weights[i] = static_cast<f32>(value);
        sum += 2 * value;
    }
    for (f32 &value : kernel.weights)
        value = static_cast<f32>(value / sum);
    return kernel;
}

struct Request {
    f32 radiusX = 0, radiusY = 0;
    bool repeatEdges = true;
    bool forceBox = false;
    // Clamp to the source extent before every box pass instead of padding once.
    // Block Dissolve's feather (AE 2027 oracle) blurs its mask this way.
    bool clampEachPass = false;
    f32 density = 1;
    Rect outRegion{}; // Empty selects input.region. Source pixels remain uncropped.
};
void pipelines(std::vector<PipelineKey> &out, SurfaceFormat work);
[[nodiscard]] Status build(EffectBuildContext &, const LayerImage &, const Request &, LayerImage &);
} // namespace aurea::fast_blur_reference
