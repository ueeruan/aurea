// =============================================================================
//  Aurea / audio / Spectrum.hpp
//
//  Espectro de UMA janela de som, em faixas: o que o efeito Espectro de áudio
//  desenha. Função PURA do trecho de amostras — mesma janela, mesmos números,
//  no preview, no scrubbing e no export.
//
//  Janela de 2048 amostras a 48 kHz (~43 ms, centrada no instante do quadro),
//  Hann, FFT radix-2, e as faixas em escala LOGARÍTMICA de 30 Hz a 16 kHz —
//  o ouvido e os analisadores de espectro dividem assim; em escala linear
//  quase toda a música cabe nas duas primeiras faixas.
//
//  A magnitude de cada faixa sai em dB comprimidos para 0..1 (0 = −60 dB,
//  1 = seno em escala cheia): é a régua da altura das barras.
// =============================================================================
#pragma once

#include "aurea/core/Types.hpp"

namespace aurea::audio {

/// Amostras da janela (potência de dois; o tamanho do FFT).
inline constexpr u32 kSpectrumWindow = 2048;
/// Faixas no máximo (bate com `kAudioSpectrumMaxBands` de effects/Effect.hpp).
inline constexpr u32 kSpectrumMaxBands = 128;
inline constexpr f32 kSpectrumLowHz = 30.0f;
inline constexpr f32 kSpectrumHighHz = 16000.0f;
/// Faixa dinâmica das barras: −60 dB vira 0.
inline constexpr f32 kSpectrumRangeDb = 60.0f;

/// Frequência central da faixa `band` de `bands` (Hz, escala logarítmica).
[[nodiscard]] f32 spectrum_band_center_hz(u32 band, u32 bands) noexcept;

/// `bands` magnitudes 0..1 (graves → agudos) e o nível geral (RMS em 0..1,
/// mesma régua de dB) da janela `mono` de `kSpectrumWindow` amostras a
/// 48 kHz. `gain` multiplica a amplitude antes da régua (1 = 0 dB). Janela
/// curta demais conta como silêncio no que falta.
void analyze_spectrum(const f32* mono, u32 count, u32 bands, f32 gain, f32* outBands, f32* outLevel) noexcept;

} // namespace aurea::audio
