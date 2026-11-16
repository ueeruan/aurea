// =============================================================================
//  Aurea / animation / KeyframeAssist.hpp
//
//  Lote K (gap_system itens 16, 17 e 19): as contas dos assistentes de
//  keyframe do After Effects, sem motor nem histórico — o Engine resolve a
//  seleção, abre o passo de desfazer e chama daqui. O AE é só a especificação
//  de comportamento.
//
//  - Inverter keyframes no tempo (Time-Reverse Keyframes).
//  - Escala exponencial (Exponential Scale).
//  - Suavizador (Smoother): reajuste por mínimos quadrados com tolerância.
//  - Assar amostras em keys (Converter expressão em keyframes, Motion Sketch).
// =============================================================================
#pragma once

#include "aurea/animation/Curve.hpp"

#include <vector>

namespace aurea::animation {

/// O easing do trecho visto de trás para a frente: g(u) = 1 − f(1 − u). Exato
/// para linear, entrada/saída, bézier e as curvas com parâmetros; Segurar,
/// Degraus e o Quique antigo ficam como estão.
void mirror_segment_ease(Keyframe& k) noexcept;

/// Inverte no tempo os keys `picked` (índices da track, 2 ou mais) dentro do
/// próprio intervalo: t' = primeiro + último − t. Os valores andam com os keys;
/// o trecho entre dois escolhidos que eram vizinhos vira o espelho do original
/// (as alças de entrada e saída trocam de lado). Falso (nada muda) = um key
/// cairia em cima de um não escolhido.
bool time_reverse(Track& track, const std::vector<u32>& picked) noexcept;

/// Escala exponencial entre os keys `first` e `last` (índices): um key por
/// quadro com v(t) = v0 · (v1 / v0)^u, lineares entre si; os keys do meio
/// saem. Falso = os valores das pontas não têm o mesmo sinal (ou um é zero),
/// são iguais, ou o intervalo é curto demais.
bool exponential_scale(Track& track, u32 first, u32 last) noexcept;

/// Suavizador: reajusta a curva entre os keys `first` e `last` com o menor
/// número de keys bézier (alças por mínimos quadrados) que fica a no máximo
/// `tolerance` (unidades do valor) da curva original em todos os quadros. Os
/// keys novos do meio são contínuos (kKeyContinuous). Devolve quantos keys o
/// trecho tem agora; 0 = nada mudou.
u32 smooth(Track& track, u32 first, u32 last, f32 tolerance) noexcept;

/// Grava `values` (um por quadro a partir de `firstFrame`) como keys lineares,
/// trocando os keys que já existiam nesse intervalo. Os de fora ficam.
void write_samples(Track& track, i64 firstFrame, const std::vector<f32>& values) noexcept;

} // namespace aurea::animation
