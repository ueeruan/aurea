// =============================================================================
//  Aurea / audio / Beats.hpp
//
//  Detecção de batidas, pura e determinística (mesma entrada → mesmas marcas):
//
//   1. Envelope de ataques: fluxo espectral (STFT 1024/480 a 48 kHz = 100
//      quadros/s, magnitude log-comprimida, só o aumento de energia por banda),
//      menos a média local, meia-onda.
//   2. Tempo: autocorrelação do envelope entre 60 e 200 BPM, ponderada por uma
//      preferência log-gaussiana em torno de 120 BPM (evita o dobro/metade).
//   3. Batidas: programação dinâmica (Ellis 2007) — cada batida soma a força
//      do ataque e paga pelo desvio do período; o caminho de maior pontuação é
//      a grade de batidas que melhor explica os ataques.
// =============================================================================
#pragma once

#include "aurea/core/Types.hpp"

#include <vector>

namespace aurea::audio {

struct BeatResult {
    f64 bpm = 0.0;
    std::vector<f64> beats;    ///< segundos desde o início do sinal, crescentes
};

/// `mono` a 48 kHz. Menos de ~3 s ou sem ataques = resultado vazio.
/// Allocations propagate to the importing/analysis boundary, which reports OOM.
[[nodiscard]] BeatResult detect_beats(const f32* mono, usize count);

/// Mesma detecção de `detect_beats` (mesmas batidas e BPM), mais a força de
/// ataque de cada batida e a fase da grade.
struct BeatAnalysis {
    f64 bpm = 0.0;
    f64 phaseSec = 0.0;        ///< fase da grade em [0, 60/bpm): média circular das batidas
    std::vector<f64> beats;    ///< idêntico a BeatResult::beats
    std::vector<f32> strength; ///< por batida: pico do envelope de ataques / mediana, em [0.25, 2]
};
[[nodiscard]] BeatAnalysis detect_beats_ex(const f32* mono, usize count);

} // namespace aurea::audio
