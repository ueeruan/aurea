// =============================================================================
//  Aurea / render / MaskRaster.hpp
//
//  Máscaras de camada (roto): do caminho bezier ao bloco que a GPU rasteriza.
//
//  A CPU só achata o caminho num polígono (fórmula de Wang: segmentos por
//  cúbica pela curvatura, na tolerância de 0,2 texel) e empacota arestas. A
//  GPU (shaders/mask/mask_raster.frag) resolve, por texel, o enrolamento
//  (regra não-zero) e a DISTÂNCIA ASSINADA ao polígono — dela sai tudo:
//
//    antisserrilhado  cobertura = clamp(d·k + ½) (exata para borda reta)
//    expansão         d + expansão (dilata/erode com cantos redondos)
//    feather          ½·(1 + erf(d / σ√2)), σ = feather/4 — o gaussiano de um
//                     semiplano: exato para borda reta, cantos arredondados
//
//  As máscaras combinam na ORDEM da pilha (Add/Subtract/Intersect/Difference;
//  None fica de fora), com inversão e opacidade por máscara. O resultado é uma
//  cobertura R8 na resolução de trabalho da camada, em cache enquanto o bloco
//  (a chave) não muda.
// =============================================================================
#pragma once

#include "aurea/timeline/Layer.hpp"

#include <vector>

namespace aurea::mask {

/// vec4 por máscara no cabeçalho do bloco (casa com mask_raster.frag).
inline constexpr u32 kHeaderVec4 = 3;

/// A máscara entra na cobertura? (fechada, ≥ 2 pontos, modo ≠ None)
[[nodiscard]] bool active(const Mask& m) noexcept;

/// Caminho no instante LOCAL fracionário da camada (keys interpolados).
void evaluate_path(const Mask& m, f64 localFrame, std::vector<MaskPoint>& out);

/// Bezier → arestas (x0, y0, x1, y1) somadas de `offset`. `tolerance` em px:
/// distância máxima entre a curva e a corda. Caminho fechado inclui a aresta
/// de volta ao primeiro ponto.
void flatten(const std::vector<MaskPoint>& pts, bool closed, f32 tolerance, Vec2 offset, std::vector<Vec4>& edges);

/// Bloco GPU das máscaras da camada no instante, ACRESCENTADO a `out`:
///   por máscara ativa, 3 vec4 de cabeçalho
///     h0 = (modo, invertida, opacidade, σ do feather em px)
///     h1 = (expansão px, 1ª aresta relativa ao início do bloco, nº de arestas, 0)
///     h2 = caixa das arestas (x0, y0, x1, y1)
///   e depois as arestas de todas.
/// `offset` leva px da camada para px da fonte (margem do texto).
/// Devolve o nº de máscaras ativas; `start` = valor inicial da cobertura (1
/// se a primeira ativa subtrai/intersecta, senão 0); `key` = hash do bloco.
[[nodiscard]] u32 build_block(const Layer& l, f64 localFrame, Vec2 offset, f32 tolerance,
                              std::vector<Vec4>& out, f32& start, u64& key);

/// Cobertura de referência na CPU (mesma matemática do shader) no ponto `p`
/// (px da fonte), com `texelsPerPx` de densidade. Testes e o palco.
[[nodiscard]] f32 coverage_at(const Vec4* block, u32 count, f32 start, Vec2 p, f32 texelsPerPx) noexcept;

} // namespace aurea::mask
