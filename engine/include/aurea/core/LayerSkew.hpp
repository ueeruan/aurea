// =============================================================================
//  Aurea / core / LayerSkew.hpp
//
//  Omega 0.3.2, área 05 (aparência), P0-6: a INCLINAÇÃO da camada na matriz.
//
//  Convenção (a mesma no preview, no export, nos cantos do palco e no
//  hit-test, porque todos passam por `layer_matrix*` do Renderer):
//
//    * Gravado: `Transform::skewX/skewY` e as trilhas SkewX/SkewY em GRAUS,
//      como a rotação. A UI mostra o mesmo número ("Inclinação X 12,0°").
//    * Render: o valor em RADIANOS vai DIRETO como fator de cisalhamento, sem
//      `tan` — é o que a referência faz (`Matrix.preSkew(skew.y, skew.x)` com o
//      valor em radianos; evidência REA ev_e74f28e6…, Transform.getOptiMatrix).
//      Assim um projeto refeito com os números da referência sai igual.
//    * Eixos TROCADOS como na referência: "Inclinação X" (skewX) cisalha na
//      VERTICAL (y' = y + kY·x) e "Inclinação Y" (skewY) na HORIZONTAL
//      (x' = x + kX·y). Ex.: camada 100×100, pivô no canto superior esquerdo,
//      Inclinação X = 45° → o canto superior direito desce 100·π/4 ≈ 78,5 px.
//    * Ordem: T(posição)·R(rotação)·S(escala)·K(inclinação)·T(−âncora). A
//      referência ainda aplica R(orientação)·S(tamanho) depois de T(−pivô); no
//      AUREA o espaço da camada já é o pixel do conteúdo (sem "tamanho" à parte)
//      e a orientação 3D é a própria rotação, então não há termo equivalente.
//
//  Desvio consciente possível (decisão do dono): trocar o fator por
//  `tan(graus)` muda SÓ `skew_factor` abaixo.
// =============================================================================
#pragma once

#include "aurea/core/Math.hpp"

#include <cmath>

namespace aurea {

/// Graus gravados → fator de cisalhamento do render (radianos, sem tan).
[[nodiscard]] inline f32 skew_factor(f32 degrees) noexcept {
    if (!std::isfinite(degrees)) return 0.0f;
    return degrees * kDeg2Rad;
}

/// A matriz K da inclinação (coluna-maior), na convenção acima. Identidade
/// quando as duas inclinações são zero (o caminho de sempre não muda).
[[nodiscard]] inline Mat4 skew_matrix(f32 skewXDeg, f32 skewYDeg) noexcept {
    Mat4 k;
    // x' = x + kx·y (kx vem da Inclinação Y); y' = ky·x + y (ky da Inclinação X).
    k.col[0].y = skew_factor(skewXDeg);
    k.col[1].x = skew_factor(skewYDeg);
    return k;
}

/// Tem inclinação? (atalho para pular a multiplicação no caminho comum)
[[nodiscard]] inline bool has_skew(f32 skewXDeg, f32 skewYDeg) noexcept {
    return (std::isfinite(skewXDeg) && skewXDeg != 0.0f) || (std::isfinite(skewYDeg) && skewYDeg != 0.0f);
}

} // namespace aurea
