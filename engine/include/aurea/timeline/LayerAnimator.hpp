// =============================================================================
//  Aurea / timeline / LayerAnimator.hpp
//
//  Animadores de CAMADA (qualquer tipo): entrada "a partir de", saída e
//  wiggle. O modelo mora em Layer::layerAnimators (Layer.hpp); os valores
//  animáveis ficam na TrackSet como TrackProperty::LayerAnimParam, com
//  effectIndex = índice do animador e effectParamIndex = LayerAnimParam.
//
//  Onde vale:
//    - camada que não é texto: sempre na camada inteira (entra no transform e
//      na opacidade que o Renderer avalia — o mesmo caminho do export);
//    - texto: unidade 0 = camada inteira; 1/2/3 = cada letra/palavra/linha,
//      somado aos animadores de texto no GlyphAnim de cada glifo.
//
//  Conta por unidade i (de n): relógio t_i = t − i·atraso; progresso p = a
//  trilha de progresso em t_i (0..100 %), com a curva `ease`; peso
//  w = força · (1 − p). O estado da unidade = o próprio + (from − repouso)·w.
//  Saída: a ordem das unidades se inverte (o progresso vai de 100 a 0 no fim
//  da camada — ver Engine::set_layer_animator).
// =============================================================================
#pragma once

#include "aurea/core/Math.hpp"
#include "aurea/core/Types.hpp"
#include "aurea/text/TextAnimator.hpp"

#include <vector>

namespace aurea {
struct Layer;
struct LayerAnimator;
class TrackSet;
}

namespace aurea::layeranim {

/// Parâmetros animáveis (effectParamIndex da trilha LayerAnimParam).
enum Param : u32 {
    kProgress = 0, kStrength, kDelay,
    kFromOpacity, kFromPosX, kFromPosY, kFromScale, kFromScaleY, kFromRotation, kFromRotX, kFromRotY, kFromTracking,
    kWigglePosX, kWigglePosY, kWiggleScale, kWiggleRotation, kWiggleSpeed, kWiggleHold,
    kParamCount,
};

/// Campo parado do parâmetro (nulo = parâmetro inexistente).
[[nodiscard]] f32* param_ref(LayerAnimator& a, u32 param) noexcept;
[[nodiscard]] f32 param_static(const LayerAnimator& a, u32 param) noexcept;
/// Faixa válida de cada parâmetro.
[[nodiscard]] f32 clamp_param(u32 param, f32 v) noexcept;
/// Valor no instante local fracionário (keyframes e/ou expressão; senão `fallback`).
[[nodiscard]] f32 param_at(const TrackSet& tracks, u32 animator, u32 param, f64 local, f32 fallback) noexcept;

/// Deslocamento do estado: somado ao transform (px, graus), escala e
/// opacidade multiplicam.
struct Offset {
    Vec3 translate{0.0f, 0.0f, 0.0f};
    Vec3 rotation{0.0f, 0.0f, 0.0f};   ///< graus (X, Y, Z)
    Vec2 scale{1.0f, 1.0f};
    f32  opacity = 1.0f;
    f32  tracking = 0.0f;              ///< px (texto)
};

/// Este animador age na camada inteira? (não-texto: sempre; texto: unidade 0)
[[nodiscard]] bool acts_on_whole(const Layer& l, const LayerAnimator& a) noexcept;
/// Algum animador ligado na camada inteira / por unidade de texto?
[[nodiscard]] bool has_whole(const Layer& l) noexcept;
[[nodiscard]] bool has_units(const Layer& l) noexcept;

/// Estado da camada inteira no tempo LOCAL fracionário (quadros).
[[nodiscard]] Offset whole_offset(const Layer& l, f64 local, f64 fps) noexcept;

/// Texto por unidade: soma o efeito dos animadores de letra/palavra/linha em
/// `out` (um por glifo, já com os animadores de texto). `align` = 0 esquerda,
/// 0,5 centro, 1 direita (o tracking abre a linha a partir dele).
void apply_to_glyphs(const Layer& l, f64 local, f64 fps, const std::vector<text::GlyphUnits>& units, u32 chars, u32 words,
                     u32 lines, f32 align, std::vector<text::GlyphAnim>& out);

/// Quanto (px) os animadores por unidade podem levar uma letra para fora da
/// caixa do texto (margem da fonte).
[[nodiscard]] f32 glyph_padding(const Layer& l, f32 textSize, usize glyphs) noexcept;

/// Ruído do wiggle (−1..1), suave, com `hold` (0..1) de cada passo parado.
[[nodiscard]] f32 wiggle_noise(u32 seed, u32 unit, u32 channel, f64 t, f32 hold) noexcept;

} // namespace aurea::layeranim
