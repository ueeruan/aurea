#pragma once
#include "aurea/core/Types.hpp"
#include <algorithm>
#include <cmath>
#include <vector>

namespace aurea::ai {
// Area filtering keeps thin features stable when the source moves by less than
// one network pixel. Preview and export must use this same full-image resize.
inline void foreground_rgb(const u8* pixels, u32 w, u32 h, u32 stride, u32 channels,
                           u32 size, std::vector<u8>& rgb) {
    rgb.resize(static_cast<usize>(size) * size * 3);
    const double dx = std::max(1.0, double(w) / size), dy = std::max(1.0, double(h) / size);
    for (u32 y = 0; y < size; ++y) for (u32 x = 0; x < size; ++x) {
        const double cx = (x + .5) * w / size, cy = (y + .5) * h / size;
        const double x0 = std::max(0.0, cx - dx / 2), x1 = std::min(double(w), cx + dx / 2);
        const double y0 = std::max(0.0, cy - dy / 2), y1 = std::min(double(h), cy + dy / 2);
        double sum[3]{};
        for (u32 sy = u32(y0); sy < u32(std::ceil(y1)); ++sy) {
            const double wy = std::min(y1, double(sy + 1)) - std::max(y0, double(sy));
            for (u32 sx = u32(x0); sx < u32(std::ceil(x1)); ++sx) {
                const double weight = wy * (std::min(x1, double(sx + 1)) - std::max(x0, double(sx)));
                const u8* p = pixels + static_cast<usize>(sy) * stride + sx * channels;
                for (u32 c = 0; c < 3; ++c) sum[c] += p[c] * weight;
            }
        }
        const double area = (x1 - x0) * (y1 - y0);
        for (u32 c = 0; c < 3; ++c) rgb[(y * size + x) * 3 + c] = u8(std::clamp(std::lround(sum[c] / area), 0l, 255l));
    }
}

// A fixed probability scale prevents an almost-empty prediction from being
// stretched into an opaque foreground on just one frame.
inline f32 foreground_probability(f32 value) noexcept {
    return std::isfinite(value) ? std::clamp(value, 0.f, 1.f) : 0.f;
}

// A finite, symmetric source-time window: seek, reverse, cache eviction and
// export order all produce the same matte. Colour correspondence rejects cuts
// and newly exposed background instead of dragging an old silhouette along.
inline void stabilize_foreground(const std::vector<f32>& current, const std::vector<u8>& rgb,
                                 const std::vector<f32>& previous, const std::vector<u8>& previousRgb,
                                 const std::vector<f32>& next, const std::vector<u8>& nextRgb,
                                 u32 size, std::vector<f32>& out) {
    out = current;
    const usize count = static_cast<usize>(size) * size;
    if (current.size() != count || rgb.size() != count * 3) return;
    auto usable = [&](const auto& map, const auto& guide) {
        if (map.size() != count || guide.size() != rgb.size()) return false;
        u64 difference = 0;
        for (usize i = 0; i < rgb.size(); ++i) difference += std::abs(int(rgb[i]) - int(guide[i]));
        return double(difference) / rgb.size() < 36.0;
    };
    const bool hasPrev = usable(previous, previousRgb), hasNext = usable(next, nextRgb);
    auto matched = [&](u32 x, u32 y, const auto& map, const auto& guide, bool valid) {
        const usize at = static_cast<usize>(y) * size + x;
        if (!valid) return current[at];
        int best = 100000; usize index = at;
        for (int dy = -2; dy <= 2; ++dy) for (int dx = -2; dx <= 2; ++dx) {
            const int sx = int(x) + dx, sy = int(y) + dy;
            if (sx < 0 || sy < 0 || sx >= int(size) || sy >= int(size)) continue;
            const usize j = static_cast<usize>(sy) * size + u32(sx);
            int d = 3 * (dx * dx + dy * dy);
            for (u32 c = 0; c < 3; ++c) d += std::abs(int(rgb[at * 3 + c]) - int(guide[j * 3 + c]));
            if (d < best) { best = d; index = j; }
        }
        return best <= 45 ? map[index] : current[at];
    };
    for (u32 y = 0; y < size; ++y) for (u32 x = 0; x < size; ++x) {
        const usize i = static_cast<usize>(y) * size + x;
        const f32 a = matched(x, y, previous, previousRgb, hasPrev);
        const f32 b = matched(x, y, next, nextRgb, hasNext);
        out[i] = std::max(std::min(a, current[i]), std::min(std::max(a, current[i]), b));
    }
}
} // namespace aurea::ai
