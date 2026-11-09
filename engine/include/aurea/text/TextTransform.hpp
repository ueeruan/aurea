#pragma once
#include "aurea/text/Text.hpp"
#include "aurea/text/TextAnimator.hpp"
#include "aurea/effects/EffectRegistry.hpp"

#include <string>

namespace aurea { struct Layer; struct TextData; }
namespace aurea::text {
inline constexpr const char* kTransformEffect = "aurea.text.transform";
enum TransformParam : u32 {
    kRangeStart, kRangeEnd, kPhase, kComponent, kAnchor, kOffset, kAngle,
    kScale, kStretch, kAlpha, kOverrideFill, kFillColor, kEaseIn, kEaseOut,
    kOverlap, kShape, kRandomOrder, kSeed, kTransformParamCount
};
void declare_transform_params(ParameterRegistry& params);
/// Margem extra dos animadores no modelo do Motion.
[[nodiscard]] f32 rig_padding(const Layer& layer, f64 time, f32 extent);
/// Animadores no modelo do Motion: estilo por glifo e matriz por unidade.
void evaluate_text_rigs(const Layer& layer, f64 time, const TextLayout& layout, std::vector<GlyphAnim>& styles,
                        std::vector<Mat4>& matrices, const Vec3* layerPivot = nullptr);

/// 0.5.0: quanto a unidade na posição `rank` (de `count` unidades com tinta)
/// recebe o efeito, 0..1, com o comportamento do Text Transform do Alight
/// Motion: as unidades ocupam janelas iguais numa linha 0..1 e a fase as
/// desloca. `start`, `end`, `phase` em frações; `overlap` em frações (0 = uma
/// janela por unidade); `shape` 0 quadrado, 1 suave (gaussiana), 2 triângulo.
/// `easeIn`/`easeOut` −1..1 viram uma curva de Bézier sobre o peso.
[[nodiscard]] f32 transform_influence(f32 start, f32 end, f32 phase, f32 overlap, u32 shape, f32 easeIn, f32 easeOut,
                                      u32 rank, u32 count) noexcept;

// Geometry effects are evaluated before raster effects, in their authored order.
// All coordinates are in the text layer, including its raster padding.
// Mesh text uses the same selectors and transforms in a 100 px/em coordinate
// system, with an explicit layer pivot instead of a raster padding offset.

// -----------------------------------------------------------------------------
// 0.5.0: efeitos que mudam o CONTEÚDO do texto antes do layout, com o
// comportamento dos efeitos de texto do Alight Motion. Índices salvos no
// projeto: acrescente só no fim.
// -----------------------------------------------------------------------------
inline constexpr const char* kProgressEffect = "aurea.text.progress";
enum ProgressParam : u32 { kProgStart, kProgEnd, kProgCursor, kProgBlink, kProgressParamCount };
inline constexpr u32 kProgressCursorCount = 9;   ///< 0 = sem cursor

inline constexpr const char* kRandomizerEffect = "aurea.text.randomizer";
enum RandomizerParam : u32 { kRandAmount, kRandEvolution, kRandSeed, kRandStart, kRandEnd, kRandCharset, kRandPreserveSpace, kRandomizerParamCount };
inline constexpr u32 kRandomizerCharsetCount = 15;

inline constexpr const char* kSpacingEffect = "aurea.text.spacing";
enum SpacingParam : u32 { kSpaceLetter, kSpaceLine, kSpacingParamCount };

void declare_progress_params(ParameterRegistry& params);
void declare_randomizer_params(ParameterRegistry& params);
void declare_spacing_params(ParameterRegistry& params);

/// Um dos três efeitos de conteúdo?
[[nodiscard]] bool is_content_effect(u32 type) noexcept;

/// Partes puras, separadas para teste. Índices em código de caracteres.
[[nodiscard]] std::string progress_text(const std::string& content, f32 start, f32 end, u32 cursor, bool showCursor);
[[nodiscard]] std::string randomize_text(const std::string& content, f32 amount, f32 evolution, f32 seed, f32 start, f32 end,
                                         u32 charset, bool preserveSpace);
}
