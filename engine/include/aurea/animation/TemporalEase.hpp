// =============================================================================
//  Aurea / animation / TemporalEase.hpp
//
//  Velocidade e influência de um trecho de keyframes, como no After Effects,
//  sobre as alças normalizadas da bézier. Velocidade em unidades da propriedade
//  por segundo; influência em % da duração do trecho.
// =============================================================================
#pragma once

#include "aurea/core/Types.hpp"

#include <algorithm>
#include <cmath>

namespace aurea {

struct TemporalEase {
    f32 speedOut = 0.0f, influenceOut = 33.3333f;
    f32 speedIn = 0.0f, influenceIn = 33.3333f;
};

[[nodiscard]] inline bool temporal_ease_from_handles(f32 x1, f32 y1, f32 x2, f32 y2, f32 seconds, f32 delta, TemporalEase& out) noexcept {
    if (!(seconds > 0.0f)) return false;
    const f32 in = 1.0f - x2;
    out.influenceOut = x1 * 100.0f;
    out.influenceIn = in * 100.0f;
    out.speedOut = x1 > 1e-5f ? y1 * delta / (x1 * seconds) : 0.0f;
    out.speedIn = in > 1e-5f ? (1.0f - y2) * delta / (in * seconds) : 0.0f;
    return true;
}

/// Falso se o valor não muda no trecho: as alças normalizadas não guardam velocidade com delta zero.
[[nodiscard]] inline bool temporal_ease_to_handles(const TemporalEase& e, f32 seconds, f32 delta, f32 handles[4]) noexcept {
    if (!(seconds > 0.0f) || std::fabs(delta) < 1e-6f) return false;
    const f32 out = std::clamp(e.influenceOut / 100.0f, 0.001f, 1.0f), in = std::clamp(e.influenceIn / 100.0f, 0.001f, 1.0f);
    handles[0] = out;
    handles[1] = std::clamp(e.speedOut * out * seconds / delta, -4.0f, 5.0f);
    handles[2] = 1.0f - in;
    handles[3] = std::clamp(1.0f - e.speedIn * in * seconds / delta, -4.0f, 5.0f);
    return true;
}

} // namespace aurea
