#pragma once

#include "aurea/effects/Effect.hpp"

namespace aurea::deep_glow_reference {

// Slots 0..20 belong to the saved Aurea rendering algorithms.
enum ParamIndex : u32 {
    kMode = 21, kRadius, kExposure, kThreshold, kThresholdSmooth,
    kMaskLayer, kMaskMode, kMaskInvert, kBlend, kSmoothBlending, kSpread,
    kAutoGamma, kGamma, kAspect, kAngleEnabled, kAngle,
    kChromaticEnabled, kChannels, kChromaticAmount,
    kTintEnabled, kTintColor, kTintMode, kTintMix,
    kFixedSteps, kSteps, kDownsample, kStepsMultiplier, kAutoIterations,
    kIterations, kBufferExpansion, kAuxExpansion, kDitherEnabled,
    kDitherMono, kDitherAmount, kView, kSourceOpacity, kUnmult,
    kEdgeBehaviour, kLeft, kTop, kRight, kBottom, kCount
};

[[nodiscard]] bool active(const EffectEval& eval) noexcept;
[[nodiscard]] bool hidden(const EffectInstance* instance, u32 index) noexcept;
void declare_parameters(ParameterRegistry& parameters);
void pipelines(std::vector<PipelineKey>& out, SurfaceFormat format);
[[nodiscard]] f32 input_margin(const EffectEval& eval) noexcept;
[[nodiscard]] Status build(EffectBuildContext& context, const EffectEval& eval,
                           const LayerImage& input, f32 margin, LayerImage& out);

} // namespace aurea::deep_glow_reference
