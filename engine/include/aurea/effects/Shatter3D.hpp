#pragma once
// =============================================================================
//  Aurea / effects / Shatter3D.hpp
//
//  3D Shatter nos dois lugares em que a camada pode estar:
//    * composição 2D — o efeito desenha as peças na imagem da camada, com uma
//      câmera própria (effects/shatter3d.vert);
//    * plano da cena 3D — o efeito devolve a camada intacta e o grupo 3D
//      desenha as peças como geometria da cena (scene3d/shatter.vert): a
//      câmera, a profundidade dos modelos, as luzes e a gravidade do mundo.
//  As duas leituras dos parâmetros são esta.
// =============================================================================
#include "aurea/effects/Effect.hpp"

namespace aurea::shatter3d {

/// Espelho do bloco `Params` de effects/shatter3d.vert (e do fim do bloco de
/// scene3d/shatter.vert). `dst` e `view.z` só valem no desenho 2D.
struct Uniforms {
    Vec4 src;      ///< região da camada (px): x, y, largura, altura
    Vec4 dst;      ///< região da saída (px) — 2D
    Vec4 grid;     ///< colunas, linhas, forma (0 quadrado, 1 triângulo, 2 vidro), semente
    Vec4 motion;   ///< explosão (0..1), força (px), giro (rad), gravidade (px)
    Vec4 shape;    ///< espessura (px), para a câmera (−1..1), aleatoriedade, onda
    Vec4 view;     ///< centro da força (px) xy, distância da câmera (px) — 2D, luz (0..1)
    Vec4 misc;     ///< esmaecer, largura e altura da célula (px), maior distância ao centro (px)
};
static_assert(sizeof(Uniforms) == 112);

/// As peças de um plano da cena. `pieces` 0 = sem explosão.
struct ScenePieces {
    Uniforms uniforms{};
    u32 pieces = 0;
    [[nodiscard]] u32 vertex_count() const noexcept { return pieces * 36u; }
};

[[nodiscard]] bool is_shatter(EffectTypeId type) noexcept;
/// Parâmetros do efeito → bloco, para a imagem `input` (região em px da
/// camada). Devolve o número de peças; 0 = parado (camada intacta).
[[nodiscard]] u32 fill(const EffectEval& e, const LayerImage& input, Uniforms& out) noexcept;

} // namespace aurea::shatter3d
