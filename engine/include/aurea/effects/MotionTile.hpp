// =============================================================================
//  Aurea / effects / MotionTile.hpp
//
//  A geometria do Motion Tile, exposta para teste.
//
//  PORTE do Aurea antigo (`motion_tile_pass.dart`): `ParametrosDoMotionTile` e
//  `fatoresQueCobremMotionTile`. A regra que custou caro para acertar, e que
//  estes testes travam:
//
//    A CÓPIA CENTRAL É A PRÓPRIA LAYER — MESMO TAMANHO, MESMO LUGAR. Os
//    controles de saída e a cobertura automática mudam a ÁREA ladrilhada, e
//    NUNCA a escala nem a posição da layer. As cópias nascem para fora.
//
//  Os três defeitos antigos que isto impede de voltar:
//    - "dá zoom para baixo": a região crescia e a layer encolhia junto;
//    - "não ladrilha no preview": a repetição cobria só a caixa da layer, e
//      com a layer reduzida o quadro ficava com moldura vazia;
//    - "muda a posição da layer": a região expandida era ancorada no canto.
//
//  O porte generaliza um ponto: o antigo supunha a âncora no centro da layer
//  (posição = centro). Aqui a conta inverte a matriz completa da layer, então
//  vale para qualquer âncora — e com a âncora no centro dá o mesmo número.
// =============================================================================
#pragma once

#include "aurea/effects/Effect.hpp"

namespace aurea::motion_tile {

/// Os números do ladrilho nas unidades do shader.
struct Params {
    f32  tileX = 1.0f, tileY = 1.0f;      ///< fração da fonte (1 = a layer inteira)
    f32  centerX = 0.5f, centerY = 0.5f;  ///< fração da fonte
    f32  outputX = 1.0f, outputY = 1.0f;  ///< saída pedida (fração da layer)
    bool mirror = false;
    bool clamp = false;                   ///< "esticar bordas" (vence o espelho)
    bool horizontalPhase = false;
    f32  phaseTurns = 0.0f;               ///< graus / 360

    [[nodiscard]] bool identity_params() const noexcept;
};

/// Índices dos parâmetros do efeito (ordem de declaração).
enum ParamIndex : u32 {
    kCenter = 0, kTileWidth, kTileHeight, kOutputWidth, kOutputHeight,
    kMirror, kClampEdges, kPhase, kHorizontalPhase,
};

[[nodiscard]] Params params_from(const EffectEval& eval) noexcept;

/// Teto do fator de cobertura: cada fator a mais é área que o shader preenche
/// por quadro. 24x a layer cobre uma composição com a layer em 1/24.
inline constexpr f32 kMaxCoverage = 24.0f;

/// Fatores (largura, altura, em múltiplos da layer) que fazem a região
/// ladrilhada cobrir o quadro inteiro da composição DEPOIS do transform da
/// layer — o maior entre isso e o que a pessoa pediu em "Largura/Altura da
/// saída". Nunca NaN, nunca infinito, nunca abaixo de 0.01.
[[nodiscard]] Vec2 coverage_factors(const Params& p, const LayerPlacement& placement) noexcept;

/// A região ladrilhada, em pixels da layer: centrada no CENTRO da layer, do
/// tamanho dos fatores de cobertura. A cópia central continua em
/// (0,0)–(largura,altura).
[[nodiscard]] Rect tiled_region(const Params& p, const LayerPlacement& placement) noexcept;

/// Avaliação de referência em CPU da grade (mesma conta do shader, sem a
/// trava de meio texel). Devolve a coordenada normalizada da fonte que o pixel
/// na posição `pos` (normalizada na fonte) mostra. Usada pelos testes para
/// conferir o shader contra uma conta independente.
[[nodiscard]] Vec2 reference_lookup(const Params& p, Vec2 pos) noexcept;

} // namespace aurea::motion_tile
