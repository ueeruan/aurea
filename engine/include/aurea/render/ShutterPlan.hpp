#pragma once

#include "aurea/timeline/Composition.hpp"
#include <algorithm>
#include <array>
#include <cmath>

namespace aurea {

// One exposure clock for transforms, glyphs, particles and scene geometry.
// No heap allocations; the caller admits the state needed by each sample.
struct ShutterPlan {
    static constexpr u32 kHardLimit = 256;
    f64 begin = 0.0;
    f64 duration = 0.0;
    u32 count = 1;

    [[nodiscard]] f64 offset(u32 i) const noexcept {
        return duration > 0.0 ? begin + (static_cast<f64>(i) + .5) * duration / std::max(1u, count) : 0.0;
    }
    [[nodiscard]] f32 weight() const noexcept { return 1.f / std::max(1u, count); }
};

inline f32 motion_blur_amount(f32 amount) noexcept {
    return std::isfinite(amount) ? std::clamp(amount, 0.f, 4.f) : 0.f;
}

inline ShutterPlan shutter_window(const MotionBlurSettings& settings, f32 amount = 1.f) noexcept {
    ShutterPlan plan;
    if (!settings.enabled || !std::isfinite(settings.shutterAngle) || !std::isfinite(settings.shutterPhase)
        || !std::isfinite(amount) || amount <= 0.f) return plan;
    const f64 scale = static_cast<f64>(motion_blur_amount(amount)) / 360.0;
    plan.begin = std::clamp(static_cast<f64>(settings.shutterPhase), -360.0, 360.0) * scale;
    plan.duration = std::clamp(static_cast<f64>(settings.shutterAngle), 0.0, 720.0) * scale;
    return plan;
}

inline u32 shutter_sample_count(const MotionBlurSettings& settings, bool finalQuality, f32 quality,
                                f64 pathPixels, bool changing = false, u32 resourceLimit = ShutterPlan::kHardLimit) noexcept {
    if ((!changing && pathPixels < .01) || resourceLimit < 2) return 1;
    quality = finalQuality ? 1.f : std::clamp(std::isfinite(quality) ? quality : .25f, .1f, 1.f);
    u32 limit = std::min(resourceLimit, std::clamp(settings.adaptiveLimit, 2u, ShutterPlan::kHardLimit));
    if (!finalQuality) limit = std::min(limit, std::max(2u, static_cast<u32>(std::ceil(limit * quality))));
    // "Amostras por quadro" é a escolha da pessoa e vale na PRÉVIA também (a
    // folga do aparelho ainda a reduz por `quality`). A prévia lia só o
    // `previewSamples` fixo (16): subir para 32/64 não mudava nada na tela
    // (beta 2140, "não funciona em frequência muito alta").
    const u32 base = std::clamp(settings.samples, 2u, 64u);
    const u32 minimum = std::min(limit, std::max(2u, static_cast<u32>(std::ceil(base * quality))));
    // Preview changes the spacing in output pixels, not the exposure interval.
    // Final integration stays deterministic regardless of playback/thermal state.
    const f64 spacing = finalQuality ? .5 : .75 / quality;
    const f64 wanted = std::isfinite(pathPixels) ? std::ceil(std::max(0.0, pathPixels) / spacing) + 1.0 : limit;
    return std::clamp(static_cast<u32>(std::min<f64>(wanted, limit)), minimum, limit);
}

// Measure corner paths, not just the displacement of the center: rotations,
// scale, perspective and a round trip can all move with a stationary center.
template<class MatrixAt>
f64 shutter_projected_path(const ShutterPlan& window, f32 width, f32 height, MatrixAt matrixAt,
                           f32 pixelScale = 1.f) {
    if (window.duration <= 0.0) return 0.0;
    constexpr u32 probes = 17;
    const std::array<Vec4, 5> points{{{0, 0, 0, 1}, {width, 0, 0, 1}, {0, height, 0, 1},
                                     {width, height, 0, 1}, {width * .5f, height * .5f, 0, 1}}};
    std::array<Vec2, 5> previous{};
    std::array<f64, 5> distance{};
    const bool excludesCenter = window.begin > 0 || window.begin + window.duration < 0;
    if (excludesCenter) {
        const Mat4 center = matrixAt(0.0);
        for (usize p = 0; p < points.size(); ++p) {
            const Vec4 q = center * points[p];
            if (!std::isfinite(q.x) || !std::isfinite(q.y) || !std::isfinite(q.w) || std::fabs(q.w) < 1e-5f) return 1e6;
            previous[p] = Vec2{q.x / q.w,q.y / q.w};
        }
    }
    for (u32 i = 0; i < probes; ++i) {
        // A nonuniform, deterministic probe grid avoids classifying an exact
        // number of revolutions between uniform probes as a stationary layer.
        const f64 unit = (i == 0 || i + 1 == probes) ? static_cast<f64>(i) / (probes - 1)
            : (static_cast<f64>(i) + .23 * std::sin(i * 2.399963229728653)) / (probes - 1);
        const Mat4 matrix = matrixAt(window.begin + window.duration * unit);
        for (usize p = 0; p < points.size(); ++p) {
            const Vec4 q = matrix * points[p];
            if (!std::isfinite(q.x) || !std::isfinite(q.y) || !std::isfinite(q.w) || std::fabs(q.w) < 1e-5f)
                return 1e6;
            const Vec2 current{q.x / q.w, q.y / q.w};
            if (i || excludesCenter) {
                const f64 x = static_cast<f64>(current.x) - previous[p].x;
                const f64 y = static_cast<f64>(current.y) - previous[p].y;
                distance[p] += std::sqrt(x * x + y * y) * std::max(.001f, pixelScale);
            }
            previous[p] = current;
        }
    }
    return *std::max_element(distance.begin(), distance.end());
}

} // namespace aurea
