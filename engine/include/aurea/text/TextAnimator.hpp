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
}

namespace aurea::text {

/// Parâmetros animáveis de um animador (effectParamIndex da trilha).
enum TextAnimParam : u32 {
    kSelStart = 0, kSelEnd, kSelOffset, kSelAmount, kSelEaseHigh, kSelEaseLow,
    kPosX = 10, kPosY, kPosZ, kScaleX, kScaleY, kRotX, kRotY, kRotZ, kOpacity, kTracking, kBlur, kSkew, kStrokeWidth, kCharOffset,
    kFillMix, kWiggleRate,
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
    f32  strokeAdd = 0.0f;    ///< px
    f32  trackingShift = 0.0f;
    Vec4 fill{0, 0, 0, 0};    ///< a = peso da mistura com a cor do animador (sRGB)
    Vec4 stroke{0, 0, 0, 0};
};

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

/// Presets nativos (dados): 0 Pop, 1 Bounce, 2 Slide, 3 Scale, 4 Fade,
/// 5 Blur Reveal, 6 Word Highlight, 7 Karaoke, 8 Typewriter, 9 Wave,
/// 10 Elastic. Escreve animadores + keyframes na camada (substitui os
/// existentes). `startLocal`/`durationFrames` = trecho da animação.
inline constexpr u32 kTextPresetCount = 11;
[[nodiscard]] const char* text_preset_name(u32 id) noexcept;
bool apply_text_preset(u32 id, TextData& t, TrackSet& tracks, i64 startLocal, i64 durationFrames, f64 fps);

} // namespace aurea::text
