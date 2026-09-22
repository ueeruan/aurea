#include "aurea/tracking/PointTracker.hpp"

#include <algorithm>
#include <cmath>

namespace aurea::tracking {

Gray to_gray(const u8* rgba, u32 width, u32 height) {
    Gray g;
    g.width = width;
    g.height = height;
    g.px.resize(static_cast<usize>(width) * height);
    for (usize i = 0; i < g.px.size(); ++i) {
        const u8* p = rgba + i * 4;
        g.px[i] = (0.2126f * p[0] + 0.7152f * p[1] + 0.0722f * p[2]) / 255.0f;
    }
    return g;
}

namespace {

/// Bloco bilinear em (cx, cy) — o ponto anterior é subpixel.
std::vector<f32> patch(const Gray& g, Vec2 c, i32 half) {
    const i32 n = 2 * half + 1;
    std::vector<f32> out(static_cast<usize>(n * n));
    const f32 fx = c.x - std::floor(c.x), fy = c.y - std::floor(c.y);
    const i32 ix = static_cast<i32>(std::floor(c.x)), iy = static_cast<i32>(std::floor(c.y));
    for (i32 y = -half; y <= half; ++y) {
        for (i32 x = -half; x <= half; ++x) {
            const f32 a = g.at(ix + x, iy + y), b = g.at(ix + x + 1, iy + y);
            const f32 cc = g.at(ix + x, iy + y + 1), d = g.at(ix + x + 1, iy + y + 1);
            out[static_cast<usize>((y + half) * n + (x + half))] =
                (a * (1 - fx) + b * fx) * (1 - fy) + (cc * (1 - fx) + d * fx) * fy;
        }
    }
    return out;
}

} // namespace

TrackStep track_step(const Gray& prev, const Gray& cur, Vec2 from, i32 half, i32 radius) {
    TrackStep best;
    best.pos = from;
    best.score = -2.0f;
    if (prev.px.empty() || cur.px.empty()) return best;
    const i32 n = 2 * half + 1;
    const std::vector<f32> t = patch(prev, from, half);
    f64 tm = 0;
    for (f32 v : t) tm += v;
    tm /= static_cast<f64>(t.size());
    f64 tv = 0;
    for (f32 v : t) tv += (v - tm) * (v - tm);
    if (tv < 1e-8) return best;   // bloco liso: nada para seguir
    const i32 cx = static_cast<i32>(std::lround(from.x)), cy = static_cast<i32>(std::lround(from.y));
    const i32 w = 2 * radius + 1;
    std::vector<f32> score(static_cast<usize>(w * w), -2.0f);
    i32 bx = 0, by = 0;
    for (i32 dy = -radius; dy <= radius; ++dy) {
        for (i32 dx = -radius; dx <= radius; ++dx) {
            f64 sm = 0;
            for (i32 y = -half; y <= half; ++y) for (i32 x = -half; x <= half; ++x) sm += cur.at(cx + dx + x, cy + dy + y);
            sm /= static_cast<f64>(n * n);
            f64 cross = 0, sv = 0;
            for (i32 y = -half; y <= half; ++y) {
                for (i32 x = -half; x <= half; ++x) {
                    const f64 a = t[static_cast<usize>((y + half) * n + (x + half))] - tm;
                    const f64 b = cur.at(cx + dx + x, cy + dy + y) - sm;
                    cross += a * b;
                    sv += b * b;
                }
            }
            const f32 s = sv > 1e-10 ? static_cast<f32>(cross / std::sqrt(tv * sv)) : -1.0f;
            score[static_cast<usize>((dy + radius) * w + (dx + radius))] = s;
            if (s > best.score) { best.score = s; bx = dx; by = dy; }
        }
    }
    // Refino subpixel: parábola pelos vizinhos do pico.
    auto sc = [&](i32 dx, i32 dy) {
        dx = std::clamp(dx, -radius, radius);
        dy = std::clamp(dy, -radius, radius);
        return score[static_cast<usize>((dy + radius) * w + (dx + radius))];
    };
    auto refine = [](f32 l, f32 c, f32 r) {
        const f32 den = l - 2.0f * c + r;
        return std::fabs(den) > 1e-6f ? std::clamp(0.5f * (l - r) / den, -0.5f, 0.5f) : 0.0f;
    };
    const f32 ox = (bx > -radius && bx < radius) ? refine(sc(bx - 1, by), sc(bx, by), sc(bx + 1, by)) : 0.0f;
    const f32 oy = (by > -radius && by < radius) ? refine(sc(bx, by - 1), sc(bx, by), sc(bx, by + 1)) : 0.0f;
    // O bloco foi tirado CENTRADO em `from` (bilinear): o centro do melhor
    // bloco do quadro atual é onde o ponto está agora.
    best.pos = Vec2{static_cast<f32>(cx + bx) + ox, static_cast<f32>(cy + by) + oy};
    return best;
}

} // namespace aurea::tracking
