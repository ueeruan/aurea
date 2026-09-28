// =============================================================================
//  Aurea / animation / KeyframeOptimize.hpp
//
//  Otimizar keyframes (ferramenta trazida do app antigo): tira os keyframes
//  que não mudam o movimento. Um keyframe do meio sai quando, sem ele, a curva
//  continua a no máximo `tolerance` (unidades do valor) da ORIGINAL em todos
//  os quadros — a comparação é sempre com a curva de antes, então o erro não
//  acumula de uma remoção para a outra.
// =============================================================================
#pragma once

#include "aurea/animation/Curve.hpp"

namespace aurea::animation {

/// Tira os keyframes redundantes da track. Devolve quantos saíram. O
/// primeiro e o último ficam sempre (o intervalo animado não muda).
u32 optimize_track(Track& track, f32 tolerance) noexcept;

/// Maior distância (quadros inteiros do intervalo de `reference`) entre as
/// duas tracks, só pelos keyframes (sem expressão).
[[nodiscard]] f32 max_deviation(const Track& reference, const Track& candidate) noexcept;

/// Amplitude dos valores da track (máximo − mínimo pelos quadros do intervalo).
[[nodiscard]] f32 value_range(const Track& track) noexcept;

} // namespace aurea::animation
