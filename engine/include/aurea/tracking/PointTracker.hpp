// =============================================================================
//  Aurea / tracking / PointTracker.hpp
//
//  Rastreio de ponto por correlação cruzada normalizada (NCC), quadro a
//  quadro: o bloco em volta do ponto no quadro anterior é procurado numa
//  janela do quadro atual; o pico da NCC ganha refino subpixel (parábola em x
//  e em y). NCC não liga para brilho/contraste globais (exposição mudando não
//  arrasta o ponto). Pontuação < limiar = ponto perdido.
// =============================================================================
#pragma once

#include "aurea/core/Types.hpp"
#include "aurea/core/Math.hpp"

#include <vector>

namespace aurea::tracking {

struct Gray {
    u32 width = 0, height = 0;
    std::vector<f32> px;   ///< luma 0..1
    [[nodiscard]] f32 at(i32 x, i32 y) const noexcept {
        x = x < 0 ? 0 : (x >= static_cast<i32>(width) ? static_cast<i32>(width) - 1 : x);
        y = y < 0 ? 0 : (y >= static_cast<i32>(height) ? static_cast<i32>(height) - 1 : y);
        return px[static_cast<usize>(y) * width + static_cast<usize>(x)];
    }
};

/// RGBA8 (sRGB) → luma.
[[nodiscard]] Gray to_gray(const u8* rgba, u32 width, u32 height);

struct TrackStep {
    Vec2 pos{0, 0};
    f32  score = 0.0f;   ///< NCC do pico (−1..1)
};

/// Procura, em `cur`, o bloco (2·half+1)² de `prev` centrado em `from`, numa
/// janela de ±`radius` px. Devolve a posição subpixel e a pontuação.
[[nodiscard]] TrackStep track_step(const Gray& prev, const Gray& cur, Vec2 from, i32 half = 8, i32 radius = 24);

} // namespace aurea::tracking
