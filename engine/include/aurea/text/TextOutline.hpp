// =============================================================================
//  Aurea / text / TextOutline.hpp
//
//  Contornos BÉZIER do texto, glifo a glifo (lote X: Create Shapes / Masks
//  from Text). Diferente de `text::outline` (Text.hpp), que achata as curvas
//  para o texto 3D, aqui cada contorno sai com os pontos e as alças da fonte
//  (quadráticas elevadas a cúbicas, sem perda), para virar caminho vetorial ou
//  máscara editável.
// =============================================================================
#pragma once

#include "aurea/text/Text.hpp"
#include "aurea/vector/VectorData.hpp"

#include <vector>

namespace aurea::text {

/// Um glifo colocado: de que caractere veio, onde está a origem dele (px da
/// camada, sem margem: a origem é o canto de cima da caixa do texto, Y para
/// baixo) e os contornos fechados, já nesse mesmo espaço.
struct GlyphOutline {
    u32 cluster = 0;        ///< caractere de origem (índice lógico no texto inteiro)
    u32 line = 0;
    f32 penX = 0.0f, baseline = 0.0f, advance = 0.0f;
    std::vector<BezierPath> contours;
};

/// Os glifos com contorno, em ordem visual: o mesmo layout (linhas,
/// alinhamento, entrelinha, espaçamento, trechos) de `layout_quads` e da
/// rasterização. Glifos sem desenho (espaço) ficam de fora. Falso = nenhum.
[[nodiscard]] bool outline_bezier(const Font& font, const TextData& t, std::vector<GlyphOutline>& out);

} // namespace aurea::text
