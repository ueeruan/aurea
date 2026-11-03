// =============================================================================
//  Aurea / text / TextAnimator.hpp
//
//  Animador de texto (implementação própria): a camada de texto tem uma pilha
//  de animadores; cada um tem um SELETOR (que diz quanto cada caractere,
//  palavra ou linha está "dentro", 0..1 — ou −1..1 no wiggly) e PROPRIEDADES
//  (o valor aplicado no caractere todo dentro). Nada de código por animação:
//  "Pop", "Typewriter", "Karaoke"… são só dados (preset = animadores +
//  keyframes).
//
//  Os valores animáveis (start/end/offset do seletor, posição, escala…) moram
//  na TrackSet da camada como TrackProperty::TextAnimParam, com
//  effectIndex = índice do animador e effectParamIndex = TextAnimParam.
// =============================================================================
#pragma once

#include "aurea/animation/Curve.hpp"
#include "aurea/core/Math.hpp"
#include "aurea/core/Types.hpp"

#include <string>
#include <vector>

namespace aurea {
struct TextData;
struct TextAnimator;
struct Layer;
struct EffectInstance;
class ParameterRegistry;
}

namespace aurea::text {
inline constexpr const char* kAnimatorEffect = "aurea.text.animator";
enum AnimatorEffectParam : u32 {
    aeStart, aeEnd, aeOffset, aeAmount, aeUnit, aeShape, aeEaseHigh, aeEaseLow,
    aeRandom, aeSeed, aeSelector, aeWiggleRate, aePosition, aeScale, aeRotation,
    aeOpacity, aeBlur, aeTracking, aeSkew, aeStrokeWidth, aeFillOn, aeFill,
    aeStrokeOn, aeStroke, aeCount
};
void declare_animator_effect_params(ParameterRegistry& params);

/// 0.4.6 · Text Animator Pro: o animador completo, com o comportamento do
/// animador de texto do After Effects como referência (seletor de intervalo
/// em % ou índice, por letra/letra sem espaço/palavra/linha, modo de
/// combinação, suavidade; seletor de oscilação com mín./máx., correlação e
/// fases; ponto de âncora, agrupamento "tudo" com alinhamento, matiz/
/// saturação/brilho/opacidade do preenchimento, espaçamento entre linhas).
/// Implementação própria (nada de código do AE). Índices dos parâmetros são
/// fixos (salvos no projeto); acrescente só no fim.
inline constexpr const char* kAnimatorProEffect = "aurea.text.animator_pro";
enum AnimatorProParam : u32 {
    apUnits, apBasedOn, apStart, apEnd, apOffset, apMode, apAmount, apShape, apSmoothness, apEaseHigh, apEaseLow,
    apRandom, apSeed,
    apWiggly, apWigglyMode, apWigglyMax, apWigglyMin, apWigglyRate, apCorrelation, apTemporalPhase, apSpatialPhase,
    apLockDims, apWigglySeed,
    apAnchor, apPosition, apScale, apSkew, apSkewAxis, apRotation, apOpacity,
    apFillOn, apFill, apFillHue, apFillSaturation, apFillBrightness, apFillOpacity,
    apStrokeOn, apStroke, apStrokeWidth, apTracking, apLineSpacing, apBlur, apGrouping, apGroupAlign,
    // Seletor de expressão (o "Expression Selector" das referências): a
    // Intensidade por eixo (%) é avaliada POR UNIDADE, com textIndex/textTotal
    // na expressão, e SUBSTITUI a seleção de cima (como selectorValue → valor).
    apExprOn, apExprAmount,
    apCount
};
void declare_animator_pro_params(ParameterRegistry& params);
/// O animador simples ou o Pro (os dois agem nos glifos do texto).
[[nodiscard]] bool is_animator_effect(u32 type) noexcept;
/// Combinação de seletores (modo do AE): soma, subtrai, interseção, mín., máx., diferença.
[[nodiscard]] f32 combine_selector(f32 above, f32 value, u32 mode) noexcept;
/// Seletor de intervalo do Pro para a unidade `unit` de `count`: 0..1 (sem a intensidade).
/// `units` 0 = %, 1 = índice. `smoothness` 0..1 (só na forma quadrada).
[[nodiscard]] f32 pro_range_weight(f32 start, f32 end, f32 offset, u32 units, u32 shape, f32 smoothness,
                                   f32 easeHigh, f32 easeLow, u32 unit, u32 count) noexcept;
/// Oscilação do Pro: −1..1 por unidade e dimensão, entre `minAmount` e `maxAmount` (−1..1).
/// `correlation` 0..1 (1 = todas iguais), fases em graus.
[[nodiscard]] f32 pro_wiggle(u32 seed, u32 unit, u32 dimension, f64 timeSec, f32 rate, f32 correlation,
                             f32 temporalPhase, f32 spatialPhase, f32 minAmount, f32 maxAmount) noexcept;
bool has_animator_effect(const Layer& layer) noexcept;
f32 animator_effect_padding(const Layer& layer, f64 time, f32 size, u32 chars);

/// Parâmetros animáveis de um animador (effectParamIndex da trilha).
enum TextAnimParam : u32 {
    kSelStart = 0, kSelEnd, kSelOffset, kSelAmount, kSelEaseHigh, kSelEaseLow,
    kFillR = 6, kFillG, kFillB,
    kPosX = 10, kPosY, kPosZ, kScaleX, kScaleY, kRotX, kRotY, kRotZ, kOpacity, kTracking, kBlur, kSkew, kStrokeWidth, kCharOffset,
    kFillMix, kWiggleRate, kSkewAxis, kTrackingEm, kAnchorGrouping,
    kStrokeR = 29, kStrokeG, kStrokeB,
};

/// Unidade de um glifo para os seletores.
struct GlyphUnits {
    u32 charIndex = 0, wordIndex = 0, lineIndex = 0;
};

/// Resultado por glifo: transform em px da layer (em volta do pivô, com Z) e
/// estilo. Aplicado sobre o glifo parado.
struct GlyphAnim {
    Vec3 translate{0, 0, 0};
    Vec2 scale{1, 1};
    Vec3 rotation{0, 0, 0};   ///< graus
    f32  opacity = 1.0f;
    f32  blur = 0.0f;         ///< px
    f32  skew = 0.0f;         ///< graus
    Mat4 skewTransform = Mat4::identity();
    u32 anchorGrouping = 0;   ///< 0 glyph, 1 word, 2 line, 3 tudo (Pro)
    Vec2 anchor{0, 0};        ///< ponto de âncora do Pro (px): o glifo gira/escala em volta de pivô + âncora
    Vec2 groupAlign{0, 0};    ///< alinhamento do agrupamento do Pro (−1..1 da meia caixa)
    f32  strokeAdd = 0.0f;    ///< px
    f32  trackingShift = 0.0f;
    Vec4 fill{0, 0, 0, 0};    ///< a = peso da mistura com a cor do animador (sRGB)
    f32 fillOpacity = 1.0f;   ///< alpha da cor de preenchimento, separado do peso da mistura
    Vec4 stroke{0, 0, 0, 0};
};
/// Aplica um Text Animator Pro (TextAnimatorPro.cpp); chamado por evaluate_animator_effects.
void evaluate_animator_pro(const Layer& layer, const EffectInstance& fx, f64 time, f64 fps, const std::vector<GlyphUnits>& units,
                           u32 chars, u32 words, u32 lines, std::vector<GlyphAnim>& out);
void evaluate_animator_effects(const Layer& layer, f64 time, f64 fps, const std::vector<GlyphUnits>& units,
                              u32 chars, u32 words, u32 lines, std::vector<GlyphAnim>& out);

/// Valor de um parâmetro no instante (fracionário: sub-quadro do desfoque).
[[nodiscard]] f32 anim_param(const TrackSet& tracks, u32 animator, u32 param, f64 local, f32 fallback) noexcept;

/// Quanto a unidade `unit` (de `count`) está selecionada no instante: 0..1
/// (intervalo) ou −1..1 (wiggly). `timeSec` = tempo da camada em segundos.
[[nodiscard]] f32 selector_weight(const TextAnimator& a, u32 animIndex, const TrackSet& tracks, f64 local, f64 timeSec, u32 unit,
                                  u32 count) noexcept;

/// Avalia a pilha de animadores para todos os glifos (ordem visual). `counts`
/// = {caracteres, palavras, linhas}. Tracking desloca os glifos seguintes da
/// mesma linha.
void evaluate_text_animators(const TextData& t, const TrackSet& tracks, f64 local, f64 fps, const std::vector<GlyphUnits>& units,
                             u32 chars, u32 words, u32 lines, std::vector<GlyphAnim>& out);

/// Deslocamento de caractere (A→B→C…): o texto com as letras trocadas no
/// instante, ou o próprio texto se nenhum animador desloca.
[[nodiscard]] std::string apply_char_offset(const TextData& t, const TrackSet& tracks, f64 local, f64 fps);

/// Algum animador ativo?
[[nodiscard]] bool has_animators(const TextData& t) noexcept;

/// 0.4.6: texto dinâmico (TextData::dynamic): troca {#} {tc} {t} {f} pelos
/// valores no instante. `local` = quadro local da camada, `duration` = quadros
/// da camada, `compFrame` = quadro da composição. Sem marcas: o próprio texto.
[[nodiscard]] std::string expand_dynamic(const TextData& t, f64 local, f64 duration, f64 compFrame, f64 fps);
/// O número do contador formatado (casas, separador de milhar com ponto e
/// vírgula decimal — o formato da marca do Aurea; o sinal fica na frente).
[[nodiscard]] std::string format_counter(f64 value, u32 decimals, bool grouping);

/// Presets nativos (dados): 0 Pop, 1 Bounce, 2 Slide, 3 Scale, 4 Fade,
/// 5 Blur Reveal, 6 Word Highlight, 7 Karaoke, 8 Typewriter, 9 Wave,
/// 10 Elastic. Escreve animadores + keyframes na camada (substitui os
/// existentes). `startLocal`/`durationFrames` = trecho da animação.
inline constexpr u32 kTextPresetCount = 27;   ///< 25 = Text Explosion, 26 = Salto suave
[[nodiscard]] const char* text_preset_name(u32 id) noexcept;
bool apply_text_preset(u32 id, TextData& t, TrackSet& tracks, i64 startLocal, i64 durationFrames, f64 fps);

} // namespace aurea::text
