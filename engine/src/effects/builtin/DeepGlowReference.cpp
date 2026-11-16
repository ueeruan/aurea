#include "BuiltinEffects.hpp"
#include "aurea/effects/DeepGlowReference.hpp"

#include <algorithm>
#include <cmath>

namespace aurea::deep_glow_reference {
namespace {

f32 scalar(const EffectEval& eval, u32 index, f32 fallback, f32 minimum, f32 maximum) noexcept {
    const f32 value = eval.f(index);
    return std::clamp(std::isfinite(value) ? value : fallback, minimum, maximum);
}

struct Uniforms {
    Vec4 sourceUv{};
    Vec4 maskRegion{};
    Vec4 region{};
    Vec4 haloUv{1, 1, 0, 0};
    Vec4 input{};
    Vec4 composite{};
    Vec4 chromatic{};
    Vec4 tintColor{};
    Vec4 tint{};
    Vec4 noise{};
    Vec4 kernel{};
    Vec4 accumulation{};
    Vec4 extra{};
    Mat4 compFromLayer = Mat4::identity();
};
static_assert(sizeof(Uniforms) == 272);

CommonSampler edge_sampler(u32 edge) noexcept {
    switch (edge) {
        case 0: return CommonSampler::LinearRepeat;
        case 1: return CommonSampler::LinearMirror;
        case 2: return CommonSampler::LinearClamp;
        default: return CommonSampler::LinearBorder;
    }
}

Status pass(EffectBuildContext& context, const char* name, const LayerImage& first,
            const LayerImage& second, const LayerImage& third, const Rect& region,
            u32 width, u32 height, const Uniforms& uniforms, CommonSampler sampler,
            LayerImage& out) {
    out = {context.texture(name, width, height), region, width, height};
    const u32 index = context.fullscreen_pass(name, PassStage::Effects, out.texture,
        ShaderId::effects_deep_glow_reference_frag,
        {{first.texture, {}, sampler}, {second.texture, {}, CommonSampler::LinearBorder},
         {third.texture, {}, CommonSampler::LinearBorder}}, &uniforms, sizeof(uniforms));
    return index == kInvalidIndex ? Status{Errc::PipelineCompileFailed} : OkStatus;
}

} // namespace

bool active(const EffectEval& eval) noexcept {
    return eval.count > kMode && eval.e(kMode) == 1;
}

bool hidden(const EffectInstance* instance, u32 index) noexcept {
    const bool reference = !instance || (instance->params.size() > kMode &&
        instance->params[kMode].constant.as_enum() == 1);
    // Keep the saved/imported Spread slot until its native behavior is verified.
    // It currently has no rendering implementation, so it is not an editing control.
    return index == kMode || (reference ? index < kMode || index == kSpread : index > kMode);
}

void declare_parameters(ParameterRegistry& p) {
    static const char* const modes[] = {"Aurea", "1.6"};
    ParamSpec mode;
    mode.id = "render_mode";
    mode.label = "Render mode";
    mode.type = ParamType::Enum;
    mode.enumLabels = modes;
    mode.enumCount = 2;
    mode.minValue = 0;
    mode.maxValue = 1;
    mode.defaultValue = ParamValue::scalar(1);
    mode.flags = kParamHidden | kParamLegacyZero;
    p.add(mode);
    constexpr u16 pixels = kParamAnimatable | kParamPixels;
    constexpr u16 percent = kParamAnimatable | kParamPercent;
    p.add_float("ref_radius", "Radius", 500, 0, 500, pixels, "px"); p.typed_range(0, 2000);
    p.add_float("ref_exposure", "Exposure", 1, 0, 5); p.typed_range(0, 10);
    p.add_float("ref_threshold", "Threshold", 0, 0, 200, percent, "%"); p.typed_range(0, 500);
    p.add_float("ref_threshold_smooth", "Threshold Smooth", 0, 0, 100, percent, "%");
    p.add_layer_ref("ref_mask_layer", "Input Mask");
    static const char* const maskModes[] = {"Alpha", "Luminance"};
    p.add_enum("ref_mask_mode", "Mask Mode", maskModes, 2, 0);
    p.add_bool("ref_mask_invert", "Invert Mask", false);
    static const char* const blendModes[] = {"Screen", "Add"};
    p.add_enum("ref_blend", "Blend Mode", blendModes, 2, 0);
    p.add_bool("ref_smooth_blending", "Smooth Blending", false);
    p.add_float("ref_spread", "Spread", 33, .01f, 100, percent, "%");
    p.add_bool("ref_auto_gamma", "Auto Detect Gamma", true, kParamNone);
    p.add_bool("ref_gamma", "Gamma Correction", false);
    p.add_float("ref_aspect", "Aspect Ratio", 1, 0, 2);
    p.add_bool("ref_angle_enabled", "Enable Angle", false, kParamNone);
    p.add_angle("ref_angle", "Aspect Angle", 0);
    p.add_bool("ref_chromatic_enabled", "Chromatic Aberration", false);
    static const char* const channels[] = {"Red & Green", "Red & Blue", "Green & Blue"};
    p.add_enum("ref_channels", "Channels", channels, 3, 1);
    p.add_float("ref_chromatic_amount", "Chromatic Amount", .25f, -1, 1); p.typed_range(-100, 100);
    p.add_bool("ref_tint_enabled", "Tint", false);
    p.add_color("ref_tint_color", "Tint Color", {1, 0, 0, 1});
    static const char* const tintModes[] = {"Multiply", "Overlay", "Soft Light"};
    p.add_enum("ref_tint_mode", "Tint Mode", tintModes, 3, 0);
    p.add_float("ref_tint_mix", "Tint Mix", 100, 0, 100, percent, "%");
    p.add_bool("ref_fixed_steps", "Fixed Steps", false);
    p.add_float("ref_steps", "Number of Steps", 1, 1, 1000); p.typed_range(1, 10000);
    p.add_float("ref_downsample", "Downsample", 80, 50, 100, percent, "%");
    p.add_float("ref_steps_multiplier", "Steps Multiplier", 1, .01f, 1); p.typed_range(.01f, 3);
    p.add_bool("ref_auto_iterations", "Auto Iterations", true);
    p.add_int("ref_iterations", "Glow Iterations", 6, 6, 10);
    static const char* const bufferModes[] = {"Auto", "Compatibility"};
    p.add_enum("ref_buffer_expansion", "Buffer Expansion", bufferModes, 2, 0);
    p.add_float("ref_aux_expansion", "Aux Expansion", 0, 0, 2000, pixels, "px");
    p.add_bool("ref_dither_enabled", "Dither", true);
    p.add_bool("ref_dither_mono", "Monochromatic", false);
    p.add_float("ref_dither_amount", "Dither Amount", 50, 0, 100, percent, "%"); p.typed_range(0, 10000);
    static const char* const views[] = {"Glow Input", "Final Render"};
    p.add_enum("ref_view", "View", views, 2, 1);
    p.add_float("ref_source_opacity", "Source Opacity", 100, 0, 100, percent, "%");
    p.add_bool("ref_unmult", "Unmult", true);
    static const char* const edges[] = {"Repeat", "Mirror Repeat", "Clamp to Edge", "None"};
    p.add_enum("ref_edge_behaviour", "Edge Behaviour", edges, 4, 1);
    p.add_float("ref_left", "Left Expansion", 0, -1000, 1000, pixels, "px");
    p.add_float("ref_top", "Top Expansion", 0, -1000, 1000, pixels, "px");
    p.add_float("ref_right", "Right Expansion", 0, -1000, 1000, pixels, "px");
    p.add_float("ref_bottom", "Bottom Expansion", 0, -1000, 1000, pixels, "px");
    p.add_section_range("ref_input", "Input", kRadius, kMaskInvert);
    p.add_section_range("ref_style", "Style", kBlend, kChromaticAmount);
    p.add_section_range("ref_tint", "Tint", kTintEnabled, kTintMix, kTintEnabled);
    p.add_section_range("ref_quality", "Quality", kFixedSteps, kAuxExpansion);
    p.add_section_range("ref_output", "Compositing", kDitherEnabled, kBottom);
    p.set_enabled_by(kGamma, kAutoGamma, 2);
    p.set_enabled_by(kAngle, kAngleEnabled, 1);
    p.set_enabled_by(kChannels, kChromaticEnabled, 1);
    p.set_enabled_by(kChromaticAmount, kChromaticEnabled, 1);
    p.set_enabled_by(kSteps, kFixedSteps, 1);
    p.set_enabled_by(kIterations, kAutoIterations, 2);
    p.set_enabled_by(kDitherMono, kDitherEnabled, 1);
    p.set_enabled_by(kDitherAmount, kDitherEnabled, 1);
}

void pipelines(std::vector<PipelineKey>& out, SurfaceFormat format) {
    out.push_back(PipelineKey::fullscreen(ShaderId::effects_deep_glow_reference_frag, format));
}

f32 input_margin(const EffectEval& e) noexcept {
    return scalar(e, kRadius, 500, 0, 2000) + scalar(e, kAuxExpansion, 0, 0, 2000) +
        std::max({std::abs(scalar(e, kLeft, 0, -1000, 1000)),
                  std::abs(scalar(e, kTop, 0, -1000, 1000)),
                  std::abs(scalar(e, kRight, 0, -1000, 1000)),
                  std::abs(scalar(e, kBottom, 0, -1000, 1000))});
}

Status build(EffectBuildContext& context, const EffectEval& e, const LayerImage& input,
              f32 margin, LayerImage& out) {
    const f32 radius = scalar(e, kRadius, 500, 0, 2000);
    if (radius == 0.f) {
        out = input;
        return OkStatus;
    }
    const f32 density = input.texel_scale_x();
    const f32 expansion = radius + scalar(e, kAuxExpansion, 0, 0, 2000);
    // Sampling boundaries belong to the source and its halo. A viewport crop
    // here would make Mirror/Repeat reflect at the composition's border.
    Rect region{input.region.x - expansion, input.region.y - expansion,
                input.region.w + expansion * 2.f, input.region.h + expansion * 2.f};
    const f32 left = scalar(e, kLeft, 0, -1000, 1000);
    const f32 top = scalar(e, kTop, 0, -1000, 1000);
    region.x -= left; region.y -= top;
    region.w = std::max(1.f, region.w + left + scalar(e, kRight, 0, -1000, 1000));
    region.h = std::max(1.f, region.h + top + scalar(e, kBottom, 0, -1000, 1000));
    const f64 requestedWidth = std::ceil(static_cast<f64>(region.w) * density - .001);
    const f64 requestedHeight = std::ceil(static_cast<f64>(region.h) * density - .001);
    const u32 maximumTexture = context.max_texture_size();
    if (!std::isfinite(requestedWidth) || !std::isfinite(requestedHeight) ||
        requestedWidth > maximumTexture || requestedHeight > maximumTexture)
        return Status{Errc::BudgetExceeded, "Deep Glow working surface exceeds the device texture limit"};
    const bool preserveOutput = e.e(kBufferExpansion) == 1 ||
        (e.placement && e.placement->preserveFullExtent);
    const Rect outputRegion = preserveOutput ? region :
        builtin::spread_region(region, 0, 0, e.placement, margin);
    u32 width = 0, height = 0;
    context.region_size(region, density, width, height);
    Uniforms uniforms;
    uniforms.sourceUv = EffectBuildContext::uv_map(region, input.region);
    uniforms.region = {region.x, region.y, region.w, region.h};
    uniforms.input = {scalar(e, kThreshold, 0, 0, 500) * .01f,
        scalar(e, kThresholdSmooth, 0, 0, 100) * .01f,
        scalar(e, kExposure, 1, 0, 10), !e.b(kAutoGamma) && e.b(kGamma) ? 1.f : 0.f};
    uniforms.composite = {static_cast<f32>(e.e(kBlend)), e.b(kSmoothBlending) ? 1.f : 0.f,
        scalar(e, kSourceOpacity, 100, 0, 100) * .01f, e.b(kUnmult) ? 1.f : 0.f};
    uniforms.chromatic = {e.b(kChromaticEnabled) ? 1.f : 0.f, static_cast<f32>(e.e(kChannels)),
        scalar(e, kChromaticAmount, .25f, -100, 100) * .01f, 0};
    uniforms.tintColor = e.color(kTintColor);
    uniforms.tint = {e.b(kTintEnabled) ? 1.f : 0.f, static_cast<f32>(e.e(kTintMode)),
        scalar(e, kTintMix, 100, 0, 100) * .01f, static_cast<f32>(e.e(kMaskMode))};
    uniforms.noise = {e.b(kDitherEnabled) ? 1.f : 0.f, e.b(kDitherMono) ? 1.f : 0.f,
        scalar(e, kDitherAmount, 50, 0, 10000) * .01f, e.b(kMaskInvert) ? 1.f : 0.f};
    uniforms.accumulation.w = static_cast<f32>(e.e(kView));
    const u64 maskId = e.value(kMaskLayer).ref;
    const LayerImage* mask = maskId ? context.layer_input(maskId) : nullptr;
    uniforms.extra = {mask ? 1.f : 0.f, static_cast<f32>(width), static_cast<f32>(height), 0};
    if (mask) uniforms.maskRegion = {mask->region.x, mask->region.y, mask->region.w, mask->region.h};
    if (e.placement) uniforms.compFromLayer = e.placement->compFromLayer;
    LayerImage prepared;
    const f32 kernelRadius = radius * .5f;
    const f32 scaledRadius = kernelRadius * density;
    f32 downsample = std::max(scalar(e, kDownsample, 80, 50, 100) * .01f,
        1.f - std::min(scaledRadius * .001f, .5f));
    u32 preparedWidth = 0, preparedHeight = 0;
    context.region_size(region, density, preparedWidth, preparedHeight);
    if (auto status = pass(context, "deep-glow-input", input, mask ? *mask : input, input, region,
            preparedWidth, preparedHeight, uniforms, CommonSampler::LinearBorder, prepared); !status.ok()) return status;
    if (e.e(kView) == 0) {
        uniforms.kernel.w = 4;
        return pass(context, "deep-glow-input-view", input, prepared, prepared, region, width, height,
            uniforms, CommonSampler::LinearBorder, out);
    }
    const f32 aspect = scalar(e, kAspect, 1, 0, 2);
    const f32 angle = e.b(kAngleEnabled) ? -scalar(e, kAngle, 0, -36000, 36000) * kDeg2Rad : 0.f;
    const f32 cosine = std::cos(angle), sine = std::sin(angle);
    u32 iterations = e.b(kAutoIterations)
        ? 6u + (scaledRadius > 125.f) + (scaledRadius > 250.f) +
            (scaledRadius > 400.f) + (scaledRadius > 600.f)
        : std::clamp(e.e(kIterations), 6u, 10u);
    // Keep every blur buffer large enough to resolve the source silhouettes.
    const f32 minWidth = std::max(2.f, static_cast<f32>(width) * .33f);
    const f32 minHeight = std::max(2.f, static_cast<f32>(height) * .33f);
    for (u32 attempt = 0; attempt + 1 < iterations; ++attempt) {
        const f32 lastScale = std::pow(downsample, static_cast<f32>(iterations - 1));
        if (width * lastScale >= minWidth && height * lastScale >= minHeight) break;
        --iterations;
        downsample += .1f;
    }
    downsample = std::min(downsample, 1.f);
    iterations = std::max(iterations, 6u);
    const CommonSampler sampler = edge_sampler(e.e(kEdgeBehaviour));
    const f32 firstRadius = kernelRadius / std::pow(2.f, static_cast<f32>(iterations - 1));
    const f32 stepsMultiplier = scalar(e, kStepsMultiplier, 1, .01f, 3);
    LayerImage sum = prepared;
    for (u32 level = 0; level < iterations; ++level) {
        const f32 levelScale = std::pow(downsample, static_cast<f32>(level));
        u32 levelWidth = std::max(1u, static_cast<u32>(width * levelScale));
        u32 levelHeight = std::max(1u, static_cast<u32>(height * levelScale));
        f32 texelRadius = firstRadius * std::pow(2.f, static_cast<f32>(level)) * density * levelScale;
        if (level == 0) texelRadius = std::max(texelRadius, 2.f);
        const f32 radiusX = texelRadius * (e.b(kAngleEnabled) ? 1.f : aspect);
        const f32 radiusY = texelRadius * (e.b(kAngleEnabled) ? 1.f : std::abs(2.f - aspect));
        const f32 stepsX = e.b(kFixedSteps) ? scalar(e, kSteps, 1, 1, 10000) :
            std::clamp(std::floor(radiusX * stepsMultiplier), 1.f, 10000.f);
        const f32 stepsY = e.b(kFixedSteps) ? scalar(e, kSteps, 1, 1, 10000) :
            std::clamp(std::floor(radiusY * stepsMultiplier), 1.f, 10000.f);
        uniforms.kernel = {cosine * radiusX / (levelWidth * stepsX),
            sine * radiusX / (levelHeight * stepsX), stepsX, 1};
        LayerImage horizontal;
        if (auto status = pass(context, "deep-glow-horizontal", prepared, prepared, prepared,
                region, levelWidth, levelHeight, uniforms, sampler, horizontal); !status.ok()) return status;
        uniforms.kernel.x = -sine * radiusY / (levelWidth * stepsY);
        uniforms.kernel.y = cosine * radiusY / (levelHeight * stepsY);
        uniforms.kernel.z = stepsY;
        uniforms.kernel.w = 5;
        uniforms.accumulation.z = level > 0 ? 1.f : 0.f;
        LayerImage next;
        if (auto status = pass(context, "deep-glow-vertical-accumulate", horizontal, sum, prepared,
                region, width, height, uniforms, sampler, next); !status.ok()) return status;
        sum = next;
    }
    uniforms.kernel.w = 3;
    uniforms.sourceUv = EffectBuildContext::uv_map(outputRegion, input.region);
    uniforms.haloUv = EffectBuildContext::uv_map(outputRegion, region);
    uniforms.region = {outputRegion.x, outputRegion.y, outputRegion.w, outputRegion.h};
    u32 outputWidth = 0, outputHeight = 0;
    context.region_size(outputRegion, density, outputWidth, outputHeight);
    return pass(context, "deep-glow-output", input, sum, prepared, outputRegion, outputWidth, outputHeight,
        uniforms, CommonSampler::LinearBorder, out);
}

} // namespace aurea::deep_glow_reference
