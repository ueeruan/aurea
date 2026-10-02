#pragma once
#include "aurea/text/Text.hpp"
#include "aurea/text/TextAnimator.hpp"
#include "aurea/effects/EffectRegistry.hpp"

namespace aurea { struct Layer; }
namespace aurea::text {
inline constexpr const char* kTransformEffect = "aurea.text.transform";
enum TransformParam : u32 {
    kRangeStart, kRangeEnd, kPhase, kComponent, kAnchor, kOffset, kAngle,
    kScale, kStretch, kAlpha, kOverrideFill, kFillColor, kEaseIn, kEaseOut,
    kOverlap, kShape, kRandomOrder, kSeed, kTransformParamCount
};
void declare_transform_params(ParameterRegistry& params);
bool has_transform_effect(const Layer& layer) noexcept;
f32 transform_padding(const Layer& layer, const EffectRegistry& registry, f64 time, f32 extent);
// Geometry effects are evaluated before raster effects, in their authored order.
// All coordinates are in the text layer, including its raster padding.
void evaluate_transform_effects(const Layer& layer, const EffectRegistry& registry, f64 time,
                                const TextLayout& layout, std::vector<GlyphAnim>& styles,
                                std::vector<Mat4>& matrices);
// Mesh text uses the same selectors and transforms in a 100 px/em coordinate
// system, with an explicit layer pivot instead of a raster padding offset.
void evaluate_transform_effects(const Layer& layer, const ParameterRegistry& specs, f64 time,
                                const TextLayout& layout, std::vector<GlyphAnim>& styles,
                                std::vector<Mat4>& matrices, const Vec3* layerPivot);
}
