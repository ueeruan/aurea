// =============================================================================
//  Aurea / hyper / HyperTurbo.hpp
//
//  Peças Turbo do AureaHyper chamáveis do GRAFO (CPU, sem GPU), para as etapas
//  Lite/Max que precisam de um passo clássico depois da rede — ex.: 4× em
//  aparelho fraco = rede ×2 + FSR ×2.
//
//  `fsr_upscale_rgb`: AMD FidelityFX Super Resolution 1 (EASU + RCAS), o MESMO
//  algoritmo dos shaders hyper/fsr_easu.frag e fsr_rcas.frag (portado de
//  ffx_fsr1.h, MIT; aviso em engine/assets/licenses/THIRD_PARTY_HYPER.md), em
//  C++ para R'G'B' 8 bits (os códigos já são perceptuais, como o FSR pede).
// =============================================================================
#pragma once

#include "aurea/core/Types.hpp"

namespace aurea::hyper {

/// Amplia `src` (w×h, R'G'B' 8 bits, stride w·3) para `dst` (dw×dh, stride
/// dw·3) com EASU e afia com RCAS (`sharpness` 0..1; 0,5 = 1 parada; 0 = sem
/// RCAS). Escala livre (até ~2,5× por chamada; 2× é o caso do grafo).
/// `threads` 1..8 (linhas divididas). Não aloca além de um quadro de saída
/// reaproveitado por thread que chama. Thread-safe (sem estado global).
void fsr_upscale_rgb(const u8* src, u32 w, u32 h, u8* dst, u32 dw, u32 dh, f32 sharpness = 0.5f,
                     u32 threads = 2) noexcept;

} // namespace aurea::hyper
