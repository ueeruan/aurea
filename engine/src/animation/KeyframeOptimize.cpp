// =============================================================================
//  Aurea / animation / KeyframeOptimize.cpp
// =============================================================================
#include "aurea/animation/KeyframeOptimize.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

namespace aurea::animation {
namespace {
/// Intervalos longos demais não são varridos quadro a quadro (nada sai).
constexpr i64 kMaxSpan = 500000;

f32 deviation_in(const Track& candidate, const std::vector<f32>& original, i64 first, i64 from, i64 to) noexcept {
    f32 worst = 0.0f;
    for (i64 f = from; f <= to; ++f) {
        const f32 d = std::fabs(candidate.sample_keys(FrameIndex{f}) - original[static_cast<usize>(f - first)]);
        if (!(d <= worst)) worst = std::isfinite(d) ? d : 1e30f;
    }
    return worst;
}
} // namespace

f32 max_deviation(const Track& reference, const Track& candidate) noexcept {
    if (reference.keys.empty()) return 0.0f;
    const i64 first = reference.keys.front().time.value, last = reference.keys.back().time.value;
    f32 worst = 0.0f;
    for (i64 f = first; f <= last && f - first <= kMaxSpan; ++f) {
        const f32 d = std::fabs(candidate.sample_keys(FrameIndex{f}) - reference.sample_keys(FrameIndex{f}));
        if (!(d <= worst)) worst = std::isfinite(d) ? d : 1e30f;
    }
    return worst;
}

f32 value_range(const Track& track) noexcept {
    if (track.keys.empty()) return 0.0f;
    const i64 first = track.keys.front().time.value, last = track.keys.back().time.value;
    f32 lo = track.keys.front().value, hi = lo;
    for (i64 f = first; f <= last && f - first <= kMaxSpan; ++f) {
        const f32 v = track.sample_keys(FrameIndex{f});
        if (!std::isfinite(v)) continue;
        lo = std::min(lo, v);
        hi = std::max(hi, v);
    }
    return hi - lo;
}

u32 optimize_track(Track& track, f32 tolerance) noexcept {
    if (track.keys.size() < 3 || !(tolerance >= 0.0f)) return 0;
    const i64 first = track.keys.front().time.value, last = track.keys.back().time.value;
    if (last - first > kMaxSpan) return 0;
    std::vector<f32> original(static_cast<usize>(last - first + 1));
    for (i64 f = first; f <= last; ++f) original[static_cast<usize>(f - first)] = track.sample_keys(FrameIndex{f});

    Track work = track;
    const usize before = work.keys.size();
    usize i = 1;
    while (i + 1 < work.keys.size()) {
        Track candidate = work;
        candidate.keys.erase(candidate.keys.begin() + static_cast<std::ptrdiff_t>(i));
        candidate.lastIndex = 0;
        // Janela com um vizinho a mais de cada lado: tangentes automáticas
        // podem depender dos vizinhos do trecho que mudou.
        const usize lo = i >= 2 ? i - 2 : 0;
        const usize hi = std::min(i + 1, candidate.keys.size() - 1);
        const i64 from = candidate.keys[lo].time.value, to = candidate.keys[hi].time.value;
        if (deviation_in(candidate, original, first, from, to) <= tolerance) {
            work = std::move(candidate);   // o próximo candidato é o que caiu no índice i
        } else {
            ++i;
        }
    }
    if (work.keys.size() == before) return 0;
    // Garantia: a curva inteira ainda está dentro da tolerância.
    if (deviation_in(work, original, first, first, last) > tolerance) return 0;
    const u32 removed = static_cast<u32>(before - work.keys.size());
    track.keys = std::move(work.keys);
    track.lastIndex = 0;
    return removed;
}

} // namespace aurea::animation
