// =============================================================================
//  Aurea / effects / MotionTile.hpp
//
//  A geometria do Motion Tile, exposta para teste.
//
//  O visual e os controles são os do app antigo (o editor anterior do dono):
//
//    - a grade é presa à LAYER: cada ladrilho é a layer INTEIRA reduzida por
//      Largura/Altura do ladrilho, e o centro do ladrilho está em "Centro";
//    - a fase desloca cada LINHA na horizontal (fase × índice da linha — com
//      180° vira tijolo); com "Deslocamento de fase horizontal" ligado, desloca
//      cada COLUNA na vertical;
//    - "Espelhar bordas" vira os ladrilhos ímpares, para as emendas casarem;
//    - Largura/Altura da SAÍDA são uma janela centrada no QUADRO (fração da
//      composição, 0%..500%): fora dela não se desenha nada. Em 100% ou mais a
//      janela é o quadro inteiro.
//
//  E a regra do Aurea que continua valendo: A CÓPIA CENTRAL É A PRÓPRIA LAYER —
//  mesmo tamanho, mesmo lugar — e a região ladrilhada cresce para fora até
//  cobrir o quadro, qualquer que seja o transform da layer.
//
//  Projetos gravados com o Motion Tile ANTERIOR do Aurea (9 slots de
//  parâmetro; saída como múltiplo da layer; fase alternada por coluna; "esticar
//  bordas") são convertidos ao abrir por `upgrade_legacy_layout`.
// =============================================================================
#pragma once

#include "aurea/effects/Effect.hpp"

namespace aurea {
struct Layer;
}

namespace aurea::motion_tile {

/// Os números do ladrilho nas unidades do shader.
struct Params {
    f32  tileX = 1.0f, tileY = 1.0f;      ///< fração da fonte (1 = a layer inteira)
    f32  centerX = 0.5f, centerY = 0.5f;  ///< fração da fonte
    f32  outputX = 1.0f, outputY = 1.0f;  ///< janela de saída (fração do QUADRO)
    bool mirror = false;
    bool horizontalPhase = false;         ///< fase por coluna (em Y) em vez de por linha (em X)
    f32  phaseTurns = 0.0f;               ///< graus / 360
    /// "Esticar bordas" do Motion Tile anterior do Aurea. Nenhum projeto liga
    /// mais isto: `params_from` não lê o slot e `upgrade_legacy_layout` o zera
    /// (oculto, era um defeito sem saída nos projetos antigos). Fica só para a
    /// conta de referência.
    bool legacyClamp = false;

    [[nodiscard]] bool identity_params() const noexcept;
};

/// Índices dos parâmetros do efeito (ordem de declaração). Os slots 0..8 são
/// os do Motion Tile anterior (keyframes e expressões continuam endereçados);
/// o 6 ficou oculto, o 9 marca a disposição nova e o 10 é a escala uniforme.
enum ParamIndex : u32 {
    kCenter = 0, kTileWidth, kTileHeight, kOutputWidth, kOutputHeight,
    kMirror, kLegacyClamp, kPhase, kHorizontalPhase, kLayout, kScale,
};

/// Quantos slots tinha o Motion Tile anterior (sem `kLayout`).
inline constexpr u32 kLegacyParamCount = 9;

[[nodiscard]] Params params_from(const EffectEval& eval) noexcept;

/// Teto do fator de cobertura: cada fator a mais é área que o shader preenche
/// por quadro. 24x a layer cobre uma composição com a layer em 1/24.
inline constexpr f32 kMaxCoverage = 24.0f;

/// Teto do ladrilho digitado (10x a layer). O slider vai a 5x, como no app
/// antigo; acima disso é amostragem, não custo nem memória.
inline constexpr f32 kMaxTileScale = 10.0f;

/// Teto da janela de saída (500% do quadro, a faixa do app antigo).
inline constexpr f32 kMaxOutput = 5.0f;

/// Fatores (largura, altura, em múltiplos da layer) que fazem a região
/// ladrilhada cobrir o quadro inteiro da composição DEPOIS do transform da
/// layer. Nunca menor que a layer (1), nunca NaN, nunca infinito, nunca acima
/// de kMaxCoverage. A janela de saída não mexe nisto: ela só recorta.
[[nodiscard]] Vec2 coverage_factors(const Params& p, const LayerPlacement& placement) noexcept;

/// Visible part of a projected plane, in source pixels. Empty at a horizon or
/// behind the camera; callers retain their bounded fallback in that case.
[[nodiscard]] Rect projected_region(const LayerPlacement& placement) noexcept;

/// A região ladrilhada, em pixels da layer: centrada no CENTRO da layer, do
/// tamanho dos fatores de cobertura. A cópia central continua em
/// (0,0)–(largura,altura).
[[nodiscard]] Rect tiled_region(const Params& p, const LayerPlacement& placement) noexcept;

/// Avaliação de referência em CPU da grade (mesma conta do shader, sem a
/// trava de meio texel). Devolve a coordenada normalizada da fonte que o pixel
/// na posição `pos` (normalizada na fonte) mostra.
[[nodiscard]] Vec2 reference_lookup(const Params& p, Vec2 pos) noexcept;

/// A janela de saída contém o ponto `compUv` (0..1 no quadro)?
[[nodiscard]] bool inside_output(const Params& p, Vec2 compUv) noexcept;

/// Converte, NO LUGAR, um Motion Tile gravado na disposição anterior (menos de
/// `kLayout + 1` slots) para a atual, junto com os keyframes dele na layer:
///   - saída: antes múltiplo da layer que só ampliava a cobertura automática
///     (abaixo de 100% não fazia nada) → agora janela no quadro; todo valor
///     antigo vira no mínimo 100% (o quadro inteiro, como era desenhado);
///   - fase: antes deslocava colunas alternadas na vertical (ou linhas, com o
///     "horizontal") no sentido oposto → agora linhas em X (ou colunas em Y,
///     com a opção): o eixo troca e o sinal inverte, e a coluna/linha vizinha
///     fica onde estava (em 180°, o tijolo, o desenho é o mesmo);
///   - acrescenta o slot `kLayout`, que marca a conversão como feita.
/// Em QUALQUER disposição (também 10/11 slots, já convertidos por builds
/// anteriores), zera o "Esticar bordas" oculto (`kLegacyClamp`): constante,
/// expressão e keyframes. Os demais controles da pessoa ficam como estão.
/// Devolve true se mudou algo. Outro tipo de efeito ou nada a fazer: false.
bool upgrade_legacy_layout(Layer& layer, EffectInstance& effect) noexcept;

} // namespace aurea::motion_tile
