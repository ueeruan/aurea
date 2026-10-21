// =============================================================================
//  Efeitos de glitch e de dano de mídia (Fase 7.3 §30, §31, §32, §33, §43,
//  §44, §48, §49, §50).
//
//  Glitchify · VHS · VHS Fita · Sinal · Cruz · HoloMatrix · Dano de filme ·
//  Dano de JPEG · Ordenar pixels
//
//  Todos leem o quadro em POSIÇÕES ERRADAS — é isso que faz um glitch. O
//  sorteio de onde errar vem sempre de uma hash de (faixa, quadro, semente):
//  determinística, igual no preview e no export, e diferente a cada quadro.
//
//  Um efeito de glitch que sorteasse com `rand()` daria um resultado diferente
//  no preview e no export, e outro ao reabrir o projeto. Isso não é um efeito,
//  é um bug que parece bonito.
// =============================================================================
#include "BuiltinEffects.hpp"

#include <algorithm>
#include <cmath>

namespace aurea::builtin {
namespace {

// -----------------------------------------------------------------------------
// Glitchify
// -----------------------------------------------------------------------------
class Glitchify final : public Effect {
public:
    void pipelines(std::vector<PipelineKey>& out, SurfaceFormat work) const override {
        out.push_back(PipelineKey::fullscreen(ShaderId::effects_glitchify_frag, work));
    }
    enum : u32 { kBandHeight = 0, kDisplace, kSpike, kChannelSplit, kFrequency, kSeed,
                 kFreeze, kMix, kVertical, kColorCorruption, kPixelSize, kChannelScale, kBlockWidth, kBlockShift, kTear, kFlicker, kPixelSort, kAmount, kSpeed, kDensity, kPosition, kScale, kCrop, kSplitY, kScaleY, kCenter,
                 kChannels, kColors, kImage, kCompletion, kTransitionSoftness, kDither, kRepeatEdges, kVerticalSort, kStreak };

    const EffectInfo& info() const noexcept override {
        static const EffectInfo i{effect_keys::kGlitchify, "Glitchify", "Glitch", EffectClass::Domain};
        return i;
    }
    void declare_parameters(ParameterRegistry& p) const override {
        p.add_float("band_height", "Altura da faixa", 24.0f, 1.0f, 300.0f, kParamAnimatable | kParamPixels, "px");
        p.add_float("displace", "Deslocamento", 30.0f, 0.0f, 500.0f, kParamAnimatable | kParamPixels, "px");
        p.add_float("spike", "Picos", 15.0f, 0.0f, 100.0f, kParamAnimatable | kParamPercent, "%");
        p.add_float("channel_split", "Separação RGB", 8.0f, 0.0f, 200.0f, kParamAnimatable | kParamPixels, "px");
        p.add_float("frequency", "Frequência", 3.0f, 0.25f, 60.0f, kParamAnimatable | kParamFrames, "quadros");
        p.add_int("seed", "Semente", 11, 0, 9999);
        p.add_bool("freeze", "Travar o quadro", false);
        p.add_float("mix", "Mistura", 100.0f, 0.0f, 100.0f, kParamAnimatable | kParamPercent, "%");
        p.add_bool("vertical", "Blocos verticais", false);
        p.add_float("color_corruption", "Corrupção de cor", 30.0f, 0.0f, 100.0f, kParamAnimatable | kParamPercent, "%");
        p.add_float("pixel_size", "Pixelização", 0.0f, 0.0f, 64.0f, kParamAnimatable | kParamPixels, "px");
        p.add_float("channel_scale", "Escala RGB", 0.0f, 0.0f, 30.0f, kParamAnimatable | kParamPercent, "%");
        p.add_float("block_width", "Largura dos blocos", 64.0f, 4.0f, 512.0f, kParamAnimatable | kParamPixels, "px");
        p.add_float("block_shift", "Deslocamento dos blocos", 0.0f, 0.0f, 500.0f, kParamAnimatable | kParamPixels, "px");
        p.add_float("tear", "Rasgos horizontais", 0.0f, 0.0f, 400.0f, kParamAnimatable | kParamPixels, "px");
        p.add_float("flicker", "Cintilação", 0.0f, 0.0f, 100.0f, kParamAnimatable | kParamPercent, "%");
        p.add_float("pixel_sort", "Ordenação de pixels", 0.0f, 0.0f, 100.0f, kParamAnimatable | kParamPercent, "%");
        p.add_float("amount", "Glitchify Amount", 100, 0, 100, kParamAnimatable | kParamPercent, "%");
        p.add_float("speed", "Glitchify Speed", 10, 0, 60, kParamAnimatable, "Hz");
        p.add_float("randomness", "Randomness", 50, 0, 100, kParamAnimatable | kParamPercent, "%");
        p.add_float("transform_position", "Position Wiggle", 0, 0, 500, kParamAnimatable | kParamPixels, "px");
        p.add_float("transform_scale", "Scale Wiggle", 0, 0, 100, kParamAnimatable | kParamPercent, "%");
        p.add_float("crop", "Crop Wiggle", 0, 0, 100, kParamAnimatable | kParamPercent, "%");
        p.add_float("split_y", "Channel Split Y", 0, 0, 500, kParamAnimatable | kParamPixels, "px");
        p.add_float("scale_y", "Channel Scale Y", 0, 0, 100, kParamAnimatable | kParamPercent, "%");
        p.add_point2("channel_center", "Channel Scale Center", {.5f,.5f}, -1, 2, kParamAnimatable | kParamRelative);
        p.add_bool("channel_enabled", "Channel Glitch", true);
        p.add_bool("color_enabled", "Color Glitch", true);
        p.add_bool("image_enabled", "Image Glitch", true);
        p.add_float("completion", "Transition Completion", 0, 0, 100, kParamAnimatable | kParamPercent, "%");
        p.add_float("transition_softness", "Transition Softness", 10, 0, 100, kParamAnimatable | kParamPercent, "%");
        p.add_float("dither", "Dither Amount", 0, 0, 100, kParamAnimatable | kParamPercent, "%");
        p.add_bool("repeat_edges", "Repeat Edge Pixels", false);
        p.add_bool("vertical_sort", "Vertical Sort", false);
        p.add_float("streak", "Pixel Streak", 0, 0, 100, kParamAnimatable | kParamPercent, "%");
    }
    bool is_identity(const EffectEval& e) const noexcept override {
        return e.f(kMix) < .01f || e.f(kAmount) < .01f;
    }
    f32 input_margin(const EffectEval& e) const noexcept override {
        return (e.f(kDisplace)*7 + e.f(kChannelSplit)*2 + e.f(kBlockShift) + e.f(kTear) + e.f(kPosition)) * e.f(kAmount)*.01f;
    }

    bool demo_values(EffectInstance&, std::vector<ParamValue>& v) const noexcept override {
        v[kBandHeight] = ParamValue::scalar(26.0f);
        v[kDisplace] = ParamValue::scalar(34.0f);
        v[kChannelSplit] = ParamValue::scalar(10.0f);
        v[kSpike] = ParamValue::scalar(18.0f);
        return true;
    }
    Status build(EffectBuildContext& ctx, const EffectEval& e, const LayerImage& input, f32 margin,
                 LayerImage& out) const override {
        const f32 reach = input_margin(e);
        const Rect region = spread_region(input.region, reach, reach, e.placement, margin);
        u32 w = 0, h = 0;
        ctx.region_size(region, input.texel_scale_x(), w, h);

        struct { EffectUniforms base; Vec4 transform, channel, options, image; } uniforms{};
        auto& u = uniforms.base;
        const f32 amount = e.f(kAmount)*.01f;
        const f32 image = e.b(kImage) ? amount : 0.f, channels = e.b(kChannels) ? amount : 0.f;
        u.uvMap = EffectBuildContext::uv_map(region, input.region);
        u.texel = Vec4{input.region.w > 0.0f ? 1.0f / input.region.w : 0.0f,
                       input.region.h > 0.0f ? 1.0f / input.region.h : 0.0f, input.region.x, input.region.y};
        u.p0 = Vec4{e.f(kBandHeight), e.f(kDisplace)*image, e.f(kSpike)*.01f*image, e.f(kChannelSplit)*channels};
        u.p1 = Vec4{std::max(0.25f, e.f(kFrequency)), static_cast<f32>(e.e(kSeed)),
                    e.b(kFreeze) ? 1.0f : 0.0f, e.f(kMix) / 100.0f};
        u.p2 = Vec4{e.b(kVertical) ? 1.0f : 0.0f, e.b(kColors) ? e.f(kColorCorruption)*.01f*amount : 0.f, e.f(kPixelSize)*image, e.f(kChannelScale)*.01f*channels};
        u.p3 = Vec4{static_cast<f32>(e.localTime.value / e.framesPerSecond)*e.f(kSpeed)*3.f, e.f(kBlockWidth), e.f(kBlockShift)*image, e.f(kTear)*image};
        u.color = Vec4{e.b(kColors) ? e.f(kFlicker)*.01f*amount : 0.f, e.f(kPixelSort)*.01f*image, 0.0f, 0.0f};

        uniforms.transform = {e.f(kPosition)*amount, e.f(kScale)*.01f*amount, e.f(kCrop)*.01f*amount, e.f(kDensity)*.01f};
        const auto center = e.p2(kCenter);
        uniforms.channel = {e.f(kSplitY)*channels, e.f(kScaleY)*.01f*channels, center.x, center.y};
        uniforms.options = {e.f(kCompletion)*.01f*amount, e.f(kTransitionSoftness)*.01f, e.f(kDither)*.01f*amount, e.b(kRepeatEdges) ? 1.f : 0.f};
        uniforms.image = {e.b(kVerticalSort) ? 1.f : 0.f, e.f(kStreak)*.01f*image, 0, 0};
        out = LayerImage{ctx.texture("glitchify", w, h), region, w, h};
        if (ctx.fullscreen_pass("glitchify", PassStage::Transform, out.texture, ShaderId::effects_glitchify_frag,
                                {PassTexture{input.texture, {}, CommonSampler::LinearBorder}},
                                &uniforms, sizeof(uniforms)) == kInvalidIndex) {
            return Errc::PipelineCompileFailed;
        }
        return OkStatus;
    }
};

// -----------------------------------------------------------------------------
// VHS realista
// -----------------------------------------------------------------------------
class Vhs final : public Effect {
public:
    enum : u32 { kBlur = 0, kChroma, kTracking, kDropouts, kHeadLines, kNoise, kDegrade, kBleed,
                 kSeed, kTrackingSpeed, kDropoutHeight, kMix };

    const EffectInfo& info() const noexcept override {
        static const EffectInfo i{effect_keys::kVhs, "VHS", "Glitch", EffectClass::Neighborhood};
        return i;
    }
    void declare_parameters(ParameterRegistry& p) const override {
        p.add_float("blur", "Borrado da luma", 3.0f, 0.0f, 40.0f, kParamAnimatable | kParamPixels, "px");
        p.add_float("chroma", "Alargar a cor", 7.0f, 0.0f, 80.0f, kParamAnimatable | kParamPixels, "px");
        p.add_float("tracking", "Instabilidade", 12.0f, 0.0f, 200.0f, kParamAnimatable | kParamPixels, "px");
        p.add_float("dropouts", "Perdas de fita", 20.0f, 0.0f, 100.0f, kParamAnimatable | kParamPercent, "%");
        p.add_float("head_lines", "Varredura de cabeçote", 45.0f, 0.0f, 100.0f, kParamAnimatable | kParamPercent, "%");
        p.add_float("noise", "Ruído", 30.0f, 0.0f, 100.0f, kParamAnimatable | kParamPercent, "%");
        p.add_float("degrade", "Degradação de cor", 40.0f, 0.0f, 100.0f, kParamAnimatable | kParamPercent, "%");
        p.add_float("bleed", "Sangramento", 60.0f, 0.0f, 100.0f, kParamAnimatable | kParamPercent, "%");
        p.add_int("seed", "Semente", 5, 0, 9999);
        p.add_float("tracking_speed", "Velocidade da instabilidade", 1.0f, 0.0f, 10.0f);
        p.add_float("dropout_height", "Altura da perda", 3.0f, 1.0f, 40.0f, kParamAnimatable | kParamPixels, "px");
        p.add_float("mix", "Mistura", 100.0f, 0.0f, 100.0f, kParamAnimatable | kParamPercent, "%");
    }
    bool is_identity(const EffectEval& e) const noexcept override { return e.f(kMix) < 0.01f; }
    f32 input_margin(const EffectEval& e) const noexcept override {
        return e.f(kBlur) + e.f(kChroma) + e.f(kTracking);
    }
    bool demo_values(EffectInstance&, std::vector<ParamValue>& v) const noexcept override {
        v[kBlur] = ParamValue::scalar(2.0f);
        v[kChroma] = ParamValue::scalar(5.0f);
        v[kTracking] = ParamValue::scalar(16.0f);
        v[kDropouts] = ParamValue::scalar(22.0f);
        v[kNoise] = ParamValue::scalar(26.0f);
        v[kDegrade] = ParamValue::scalar(45.0f);
        return true;
    }
    Status build(EffectBuildContext& ctx, const EffectEval& e, const LayerImage& input, f32 margin,
                 LayerImage& out) const override {
        const f32 reach = e.f(kBlur) + e.f(kChroma) + e.f(kTracking);
        const Rect region = spread_region(input.region, reach, reach, e.placement, margin);
        u32 w = 0, h = 0;
        ctx.region_size(region, input.texel_scale_x(), w, h);

        EffectUniforms u;
        u.uvMap = EffectBuildContext::uv_map(region, input.region);
        u.texel = Vec4{region.w > 0.0f ? 1.0f / region.w : 0.0f, region.h > 0.0f ? 1.0f / region.h : 0.0f,
                       input.texel_scale_x(), input.texel_scale_y()};
        u.p0 = Vec4{e.f(kBlur), e.f(kChroma), e.f(kTracking), e.f(kDropouts) / 100.0f};
        u.p1 = Vec4{e.f(kNoise) / 100.0f, e.f(kDegrade) / 100.0f, e.f(kHeadLines) / 100.0f, e.f(kBleed) / 100.0f};
        u.p2 = Vec4{static_cast<f32>(e.e(kSeed)), e.f(kTrackingSpeed), e.f(kDropoutHeight), e.f(kMix) / 100.0f};
        u.p3 = Vec4{static_cast<f32>(e.localTime.value), 0.0f, 0.0f, 0.0f};

        out = LayerImage{ctx.texture("vhs", w, h), region, w, h};
        if (ctx.fullscreen_pass("vhs", PassStage::Transform, out.texture, ShaderId::effects_vhs_frag,
                                {PassTexture{input.texture, {}, CommonSampler::LinearBorder}},
                                &u, sizeof(u)) == kInvalidIndex) {
            return Errc::PipelineCompileFailed;
        }
        return OkStatus;
    }
};

// -----------------------------------------------------------------------------
// VHS Fita (o estilizado)
// -----------------------------------------------------------------------------
class UniVhs final : public Effect {
public:
    enum : u32 { kSplit = 0, kWarp, kDirtyGlow, kVignette, kHeadLines, kNoise, kSaturation,
                 kMix, kSeed, kWarpFrequency };

    const EffectInfo& info() const noexcept override {
        static const EffectInfo i{effect_keys::kUniVhs, "VHS Fita", "Glitch", EffectClass::Neighborhood};
        return i;
    }
    void declare_parameters(ParameterRegistry& p) const override {
        p.add_float("split", "Separação RGB", 9.0f, 0.0f, 120.0f, kParamAnimatable | kParamPixels, "px");
        p.add_float("warp", "Ondulação", 6.0f, 0.0f, 60.0f, kParamAnimatable | kParamPixels, "px");
        p.add_float("dirty_glow", "Brilho sujo", 50.0f, 0.0f, 200.0f, kParamAnimatable | kParamPercent, "%");
        p.add_float("vignette", "Vinheta", 35.0f, 0.0f, 100.0f, kParamAnimatable | kParamPercent, "%");
        p.add_float("head_lines", "Varredura", 40.0f, 0.0f, 100.0f, kParamAnimatable | kParamPercent, "%");
        p.add_float("noise", "Ruído", 25.0f, 0.0f, 100.0f, kParamAnimatable | kParamPercent, "%");
        p.add_float("saturation", "Saturação", 115.0f, 0.0f, 200.0f, kParamAnimatable | kParamPercent, "%");
        p.add_float("mix", "Mistura", 100.0f, 0.0f, 100.0f, kParamAnimatable | kParamPercent, "%");
        p.add_int("seed", "Semente", 17, 0, 9999);
        p.add_float("warp_frequency", "Frequência da ondulação", 2.0f, 0.5f, 12.0f);
    }
    bool is_identity(const EffectEval& e) const noexcept override {
        return e.f(kMix) < 0.01f || (e.f(kSplit) < 0.01f && e.f(kWarp) < 0.01f && e.f(kNoise) < 0.01f);
    }
    f32 input_margin(const EffectEval& e) const noexcept override { return e.f(kSplit) + e.f(kWarp) + 8.0f; }
    void pipelines(std::vector<PipelineKey>& out, SurfaceFormat work) const override {
        out.push_back(PipelineKey::fullscreen(ShaderId::effects_uni_vhs_frag, work));
    }
    bool demo_values(EffectInstance&, std::vector<ParamValue>& v) const noexcept override {
        v[kSplit] = ParamValue::scalar(8.0f);
        v[kWarp] = ParamValue::scalar(7.0f);
        v[kDirtyGlow] = ParamValue::scalar(60.0f);
        v[kSaturation] = ParamValue::scalar(125.0f);
        return true;
    }
    Status build(EffectBuildContext& ctx, const EffectEval& e, const LayerImage& input, f32 margin,
                 LayerImage& out) const override {
        const f32 reach = e.f(kSplit) + e.f(kWarp) + 8.0f;
        const Rect region = spread_region(input.region, reach, reach, e.placement, margin);
        u32 w = 0, h = 0;
        ctx.region_size(region, input.texel_scale_x(), w, h);

        EffectUniforms u;
        u.uvMap = EffectBuildContext::uv_map(region, input.region);
        u.texel = Vec4{region.w > 0.0f ? 1.0f / region.w : 0.0f, region.h > 0.0f ? 1.0f / region.h : 0.0f,
                       input.texel_scale_x(), input.texel_scale_y()};
        u.p0 = Vec4{e.f(kSplit), e.f(kWarp), e.f(kDirtyGlow) / 100.0f, e.f(kVignette) / 100.0f};
        u.p1 = Vec4{e.f(kHeadLines) / 100.0f, e.f(kNoise) / 100.0f, e.f(kSaturation) / 100.0f, e.f(kMix) / 100.0f};
        u.p2 = Vec4{static_cast<f32>(e.e(kSeed)), e.f(kWarpFrequency), 0.0f, 0.0f};
        u.p3 = Vec4{static_cast<f32>(e.localTime.value), 0.0f, 0.0f, 0.0f};

        out = LayerImage{ctx.texture("vhs-fita", w, h), region, w, h};
        if (ctx.fullscreen_pass("vhs-fita", PassStage::Transform, out.texture, ShaderId::effects_uni_vhs_frag,
                                {PassTexture{input.texture, {}, CommonSampler::LinearBorder}},
                                &u, sizeof(u)) == kInvalidIndex) {
            return Errc::PipelineCompileFailed;
        }
        return OkStatus;
    }
};

// -----------------------------------------------------------------------------
// Sinal
// -----------------------------------------------------------------------------
class Signal final : public Effect {
public:
    enum : u32 { kBand = 0, kDisplace, kDrift, kBandHeight, kNoise, kSeed, kFrequency,
                 kMix, kSyncLoss, kSplit };

    const EffectInfo& info() const noexcept override {
        static const EffectInfo i{effect_keys::kSignal, "Sinal", "Glitch", EffectClass::Domain};
        return i;
    }
    void declare_parameters(ParameterRegistry& p) const override {
        p.add_float("band", "Bandas perdidas", 25.0f, 0.0f, 100.0f, kParamAnimatable | kParamPercent, "%");
        p.add_float("displace", "Deslocamento", 60.0f, 0.0f, 800.0f, kParamAnimatable | kParamPixels, "px");
        p.add_float("drift", "Deriva", 0.4f, -20.0f, 20.0f, kParamAnimatable | kParamPixels | kParamPerFrame, "px/quadro");
        p.add_float("band_height", "Altura da banda", 40.0f, 2.0f, 400.0f, kParamAnimatable | kParamPixels, "px");
        p.add_float("noise", "Ruído de sinal", 30.0f, 0.0f, 100.0f, kParamAnimatable | kParamPercent, "%");
        p.add_int("seed", "Semente", 23, 0, 9999);
        p.add_float("frequency", "Frequência", 4.0f, 0.25f, 60.0f, kParamAnimatable | kParamFrames, "quadros");
        p.add_float("mix", "Mistura", 100.0f, 0.0f, 100.0f, kParamAnimatable | kParamPercent, "%");
        p.add_bool("sync_loss", "Perder a sincronia", true);
        p.add_float("split", "Separação de cor", 4.0f, 0.0f, 60.0f, kParamAnimatable | kParamPixels, "px");
    }
    bool is_identity(const EffectEval& e) const noexcept override {
        return e.f(kMix) < 0.01f
            || (e.f(kBand) < 0.01f && e.f(kNoise) < 0.01f && std::fabs(e.f(kDrift)) < 0.01f);
    }
    f32 input_margin(const EffectEval& e) const noexcept override { return e.f(kDisplace) * 4.0f + e.f(kSplit) * 2.0f; }
    bool demo_values(EffectInstance&, std::vector<ParamValue>& v) const noexcept override {
        v[kBand] = ParamValue::scalar(30.0f);
        v[kDisplace] = ParamValue::scalar(70.0f);
        v[kBandHeight] = ParamValue::scalar(34.0f);
        v[kNoise] = ParamValue::scalar(32.0f);
        return true;
    }
    Status build(EffectBuildContext& ctx, const EffectEval& e, const LayerImage& input, f32 margin,
                 LayerImage& out) const override {
        const f32 reach = e.f(kDisplace) * 4.0f + e.f(kSplit) * 2.0f;
        const Rect region = spread_region(input.region, reach, reach, e.placement, margin);
        u32 w = 0, h = 0;
        ctx.region_size(region, input.texel_scale_x(), w, h);

        EffectUniforms u;
        u.uvMap = EffectBuildContext::uv_map(region, input.region);
        u.texel = Vec4{region.w > 0.0f ? 1.0f / region.w : 0.0f, region.h > 0.0f ? 1.0f / region.h : 0.0f,
                       input.texel_scale_x(), input.texel_scale_y()};
        u.p0 = Vec4{e.f(kBand) / 100.0f, e.f(kDisplace), e.f(kDrift), e.f(kBandHeight)};
        u.p1 = Vec4{e.f(kNoise) / 100.0f, static_cast<f32>(e.e(kSeed)), std::max(1.0f, e.f(kFrequency)),
                    e.f(kMix) / 100.0f};
        u.p2 = Vec4{e.b(kSyncLoss) ? 1.0f : 0.0f, e.f(kSplit), 0.0f, 0.0f};
        u.p3 = Vec4{static_cast<f32>(e.localTime.value), 0.0f, 0.0f, 0.0f};

        out = LayerImage{ctx.texture("sinal", w, h), region, w, h};
        if (ctx.fullscreen_pass("sinal", PassStage::Transform, out.texture, ShaderId::effects_signal_frag,
                                {PassTexture{input.texture, {}, CommonSampler::LinearBorder}},
                                &u, sizeof(u)) == kInvalidIndex) {
            return Errc::PipelineCompileFailed;
        }
        return OkStatus;
    }
};

// -----------------------------------------------------------------------------
// Glitch em cruz
// -----------------------------------------------------------------------------
class CrossGlitch final : public Effect {
public:
    enum : u32 { kProgress = 0, kThickness, kDisplace, kBackground, kSplit, kSeed, kFrequency,
                 kMix, kVertical, kHorizontal, kLineColor };

    const EffectInfo& info() const noexcept override {
        static const EffectInfo i{effect_keys::kCrossGlitch, "Glitch em cruz", "Transição", EffectClass::Domain};
        return i;
    }
    void declare_parameters(ParameterRegistry& p) const override {
        p.add_float("progress", "Progresso", 50.0f, 0.0f, 100.0f, kParamAnimatable | kParamPercent, "%");
        p.add_float("thickness", "Largura da faixa", 18.0f, 1.0f, 100.0f, kParamAnimatable | kParamPercent, "%");
        p.add_float("displace", "Deslocamento", 45.0f, 0.0f, 500.0f, kParamAnimatable | kParamPixels, "px");
        p.add_float("background", "Ruído de fundo", 20.0f, 0.0f, 100.0f, kParamAnimatable | kParamPercent, "%");
        p.add_float("split", "Separação RGB", 7.0f, 0.0f, 80.0f, kParamAnimatable | kParamPixels, "px");
        p.add_int("seed", "Semente", 31, 0, 9999);
        p.add_float("frequency", "Frequência", 2.0f, 0.25f, 60.0f, kParamAnimatable | kParamFrames, "quadros");
        p.add_float("mix", "Mistura", 100.0f, 0.0f, 100.0f, kParamAnimatable | kParamPercent, "%");
        p.add_bool("vertical_band", "Faixa vertical", true);
        p.add_bool("horizontal_band", "Faixa horizontal", true);
        p.add_color("line_color", "Cor do risco", Vec4{1, 1, 1, 1});
        // kLineColor entra no enum para o índice do uniforme ficar explícito.
    }
    bool is_identity(const EffectEval& e) const noexcept override { return e.f(kMix) < 0.01f; }
    f32 input_margin(const EffectEval& e) const noexcept override { return e.f(kDisplace) * 1.5f + e.f(kSplit) * 2.0f + 10.0f; }
    bool demo_values(EffectInstance&, std::vector<ParamValue>& v) const noexcept override {
        v[kProgress] = ParamValue::scalar(48.0f);
        v[kThickness] = ParamValue::scalar(16.0f);
        v[kDisplace] = ParamValue::scalar(40.0f);
        return true;
    }
    Status build(EffectBuildContext& ctx, const EffectEval& e, const LayerImage& input, f32 margin,
                 LayerImage& out) const override {
        const f32 reach = e.f(kDisplace) * 1.5f + e.f(kSplit) * 2.0f + 10.0f;
        const Rect region = spread_region(input.region, reach, reach, e.placement, margin);
        u32 w = 0, h = 0;
        ctx.region_size(region, input.texel_scale_x(), w, h);

        EffectUniforms u;
        u.uvMap = EffectBuildContext::uv_map(region, input.region);
        u.texel = Vec4{region.w > 0.0f ? 1.0f / region.w : 0.0f, region.h > 0.0f ? 1.0f / region.h : 0.0f,
                       input.texel_scale_x(), input.texel_scale_y()};
        u.p0 = Vec4{e.f(kProgress) / 100.0f, e.f(kThickness) / 100.0f, e.f(kDisplace),
                    e.f(kBackground) / 100.0f};
        u.p1 = Vec4{e.f(kSplit), static_cast<f32>(e.e(kSeed)), std::max(0.25f, e.f(kFrequency)),
                    e.f(kMix) / 100.0f};
        u.p2 = Vec4{e.b(kVertical) ? 1.0f : 0.0f, e.b(kHorizontal) ? 1.0f : 0.0f, 0.0f, 0.0f};
        u.p3 = Vec4{static_cast<f32>(e.localTime.value), 0.0f, 0.0f, 0.0f};
        u.color = e.color(kLineColor);

        out = LayerImage{ctx.texture("glitch-cruz", w, h), region, w, h};
        if (ctx.fullscreen_pass("glitch-cruz", PassStage::Transform, out.texture,
                                ShaderId::effects_cross_glitch_frag,
                                {PassTexture{input.texture, {}, CommonSampler::LinearBorder}},
                                &u, sizeof(u)) == kInvalidIndex) {
            return Errc::PipelineCompileFailed;
        }
        return OkStatus;
    }
};

// -----------------------------------------------------------------------------
// HoloMatrix
// -----------------------------------------------------------------------------
class HoloMatrix final : public Effect {
public:
    enum : u32 { kTintMix = 0, kGrid, kGridCells, kEdgeGlow, kScanPos, kScanWidth, kInterference,
                 kScanSpeed, kInterferenceFreq, kBackground, kHeadLines, kMix, kColor };

    const EffectInfo& info() const noexcept override {
        static const EffectInfo i{effect_keys::kHoloMatrix, "HoloMatrix", "Estilizar", EffectClass::Neighborhood};
        return i;
    }
    void declare_parameters(ParameterRegistry& p) const override {
        p.add_float("tint_mix", "Mistura da cor", 85.0f, 0.0f, 100.0f, kParamAnimatable | kParamPercent, "%");
        p.add_float("grid", "Grade", 45.0f, 0.0f, 200.0f, kParamAnimatable | kParamPercent, "%");
        p.add_float("grid_cells", "Células da grade", 14.0f, 2.0f, 80.0f);
        p.add_float("edge_glow", "Brilho das bordas", 70.0f, 0.0f, 200.0f, kParamAnimatable | kParamPercent, "%");
        p.add_float("scan_position", "Posição da varredura", 50.0f, -20.0f, 120.0f, kParamAnimatable | kParamPercent, "%");
        p.add_float("scan_width", "Largura da varredura", 8.0f, 0.5f, 50.0f, kParamAnimatable | kParamPercent, "%");
        p.add_float("interference", "Interferência", 35.0f, 0.0f, 100.0f, kParamAnimatable | kParamPercent, "%");
        p.add_float("scan_speed", "Velocidade da varredura", 1.0f, -10.0f, 10.0f, kParamAnimatable);
        p.add_float("interference_freq", "Frequência da interferência", 90.0f, 1.0f, 400.0f);
        p.add_float("background", "Fundo aceso", 25.0f, 0.0f, 100.0f, kParamAnimatable | kParamPercent, "%");
        p.add_float("head_lines", "Linhas finas", 30.0f, 0.0f, 100.0f, kParamAnimatable | kParamPercent, "%");
        p.add_float("mix", "Mistura", 100.0f, 0.0f, 100.0f, kParamAnimatable | kParamPercent, "%");
        p.add_color("color", "Cor do holograma", Vec4{0.35f, 0.95f, 0.85f, 1.0f});
    }
    bool is_identity(const EffectEval& e) const noexcept override { return e.f(kMix) < 0.01f; }
    f32 input_margin(const EffectEval&) const noexcept override { return 4.0f; }
    bool demo_values(EffectInstance&, std::vector<ParamValue>& v) const noexcept override {
        v[kTintMix] = ParamValue::scalar(90.0f);
        v[kGrid] = ParamValue::scalar(55.0f);
        v[kEdgeGlow] = ParamValue::scalar(85.0f);
        return true;
    }
    Status build(EffectBuildContext& ctx, const EffectEval& e, const LayerImage& input, f32,
                 LayerImage& out) const override {
        EffectUniforms u = base_uniforms(input);
        u.p0 = Vec4{e.f(kTintMix) / 100.0f, e.f(kGrid) / 100.0f, e.f(kGridCells), e.f(kEdgeGlow) / 100.0f};
        u.p1 = Vec4{e.f(kScanPos) / 100.0f, e.f(kScanWidth) / 100.0f * 0.5f, e.f(kInterference) / 100.0f,
                    e.f(kScanSpeed)};
        u.p2 = Vec4{e.f(kInterferenceFreq), e.f(kBackground) / 100.0f, e.f(kHeadLines) / 100.0f,
                    e.f(kMix) / 100.0f};
        u.p3 = Vec4{static_cast<f32>(e.localTime.value), 0.0f, 0.0f, 0.0f};
        u.color = e.color(kColor);
        return single_pass(ctx, ShaderId::effects_holomatrix_frag, input, u, "holomatrix", out);
    }
};

// -----------------------------------------------------------------------------
// Dano de filme
// -----------------------------------------------------------------------------
class FilmDamage final : public Effect {
public:
    enum : u32 { kDust = 0, kScratches, kFlicker, kGateWeave, kBurn, kSplice, kSeed, kMix,
                 kDustSize, kScratchLength, kSpliceSpeed, kBurnColor };

    const EffectInfo& info() const noexcept override {
        static const EffectInfo i{effect_keys::kFilmDamage, "Dano de filme", "Estilizar", EffectClass::Neighborhood};
        return i;
    }
    void declare_parameters(ParameterRegistry& p) const override {
        p.add_float("dust", "Poeira", 30.0f, 0.0f, 100.0f, kParamAnimatable | kParamPercent, "%");
        p.add_float("scratches", "Riscos", 35.0f, 0.0f, 100.0f, kParamAnimatable | kParamPercent, "%");
        p.add_float("flicker", "Piscar", 25.0f, 0.0f, 100.0f, kParamAnimatable | kParamPercent, "%");
        p.add_float("gate_weave", "Balanço de porta", 3.0f, 0.0f, 30.0f, kParamAnimatable | kParamPixels, "px");
        p.add_float("burn", "Queimado", 20.0f, 0.0f, 100.0f, kParamAnimatable | kParamPercent, "%");
        p.add_bool("splice", "Emenda", true);
        p.add_int("seed", "Semente", 43, 0, 9999);
        p.add_float("mix", "Mistura", 100.0f, 0.0f, 100.0f, kParamAnimatable | kParamPercent, "%");
        p.add_float("dust_size", "Tamanho da poeira", 2.0f, 0.5f, 20.0f, kParamAnimatable | kParamPixels, "px");
        p.add_float("scratch_length", "Comprimento do risco", 60.0f, 5.0f, 100.0f, kParamAnimatable | kParamPercent, "%");
        p.add_float("splice_speed", "Velocidade da emenda", 1.0f, 0.0f, 20.0f);
        p.add_float("burn_color", "Calor do queimado", 0.0f, 0.0f, 100.0f, kParamAnimatable | kParamPercent, "%");
    }
    bool is_identity(const EffectEval& e) const noexcept override { return e.f(kMix) < 0.01f; }
    f32 input_margin(const EffectEval& e) const noexcept override { return e.f(kGateWeave) + 2.0f; }
    bool demo_values(EffectInstance&, std::vector<ParamValue>& v) const noexcept override {
        v[kDust] = ParamValue::scalar(40.0f);
        v[kScratches] = ParamValue::scalar(45.0f);
        v[kFlicker] = ParamValue::scalar(30.0f);
        v[kBurn] = ParamValue::scalar(35.0f);
        return true;
    }
    Status build(EffectBuildContext& ctx, const EffectEval& e, const LayerImage& input, f32 margin,
                 LayerImage& out) const override {
        const f32 weave = e.f(kGateWeave);
        const Rect region = spread_region(input.region, weave + 2.0f, weave + 2.0f, e.placement, margin);
        u32 w = 0, h = 0;
        ctx.region_size(region, input.texel_scale_x(), w, h);

        EffectUniforms u;
        u.uvMap = EffectBuildContext::uv_map(region, input.region);
        u.texel = Vec4{region.w > 0.0f ? 1.0f / region.w : 0.0f, region.h > 0.0f ? 1.0f / region.h : 0.0f,
                       input.texel_scale_x(), input.texel_scale_y()};
        u.p0 = Vec4{e.f(kDust) / 100.0f, e.f(kScratches) / 100.0f, e.f(kFlicker) / 100.0f, weave};
        u.p1 = Vec4{e.f(kBurn) / 100.0f, e.b(kSplice) ? 1.0f : 0.0f, static_cast<f32>(e.e(kSeed)),
                    e.f(kMix) / 100.0f};
        u.p2 = Vec4{e.f(kDustSize), e.f(kScratchLength) / 100.0f, e.f(kSpliceSpeed), e.f(kBurnColor) / 100.0f};
        u.p3 = Vec4{static_cast<f32>(e.localTime.value), 0.0f, 0.0f, 0.0f};

        out = LayerImage{ctx.texture("dano-de-filme", w, h), region, w, h};
        if (ctx.fullscreen_pass("dano-de-filme", PassStage::Transform, out.texture,
                                ShaderId::effects_film_damage_frag,
                                {PassTexture{input.texture, {}, CommonSampler::LinearBorder}},
                                &u, sizeof(u)) == kInvalidIndex) {
            return Errc::PipelineCompileFailed;
        }
        return OkStatus;
    }
};

// -----------------------------------------------------------------------------
// Dano de JPEG
// -----------------------------------------------------------------------------
class JpegDamage final : public Effect {
public:
    enum : u32 { kQuality = 0, kBlocks, kRinging, kChroma, kBlockSize, kSmoothBlock, kEdgeBoost,
                 kMix, kCorruption, kSeed };

    const EffectInfo& info() const noexcept override {
        static const EffectInfo i{effect_keys::kJpegDamage, "Dano de JPEG", "Estilizar", EffectClass::Neighborhood};
        return i;
    }
    void declare_parameters(ParameterRegistry& p) const override {
        p.add_float("quality", "Qualidade", 15.0f, 0.0f, 100.0f, kParamAnimatable | kParamPercent, "%");
        p.add_float("blocks", "Blocos", 80.0f, 0.0f, 100.0f, kParamAnimatable | kParamPercent, "%");
        p.add_float("ringing", "Anelamento", 60.0f, 0.0f, 100.0f, kParamAnimatable | kParamPercent, "%");
        p.add_float("chroma_damage", "Dano de cor", 70.0f, 0.0f, 100.0f, kParamAnimatable | kParamPercent, "%");
        p.add_float("block_size", "Tamanho do bloco", 8.0f, 2.0f, 64.0f, kParamAnimatable | kParamPixels, "px");
        p.add_float("smooth_block", "Suavizar o bloco", 40.0f, 0.0f, 100.0f, kParamAnimatable | kParamPercent, "%");
        p.add_float("edge_boost", "Reforço de borda", 0.0f, 0.0f, 200.0f, kParamAnimatable | kParamPercent, "%");
        p.add_float("mix", "Mistura", 100.0f, 0.0f, 100.0f, kParamAnimatable | kParamPercent, "%");
        p.add_float("corruption", "Blocos corrompidos", 10.0f, 0.0f, 100.0f, kParamAnimatable | kParamPercent, "%");
        p.add_int("seed", "Semente", 57, 0, 9999);
    }
    bool is_identity(const EffectEval& e) const noexcept override { return e.f(kMix) < 0.01f || e.f(kQuality) >= 99.9f; }
    f32 input_margin(const EffectEval& e) const noexcept override { return e.f(kBlockSize) * 4.0f; }
    bool demo_values(EffectInstance&, std::vector<ParamValue>& v) const noexcept override {
        v[kQuality] = ParamValue::scalar(12.0f);
        v[kBlockSize] = ParamValue::scalar(8.0f);
        v[kRinging] = ParamValue::scalar(65.0f);
        return true;
    }
    Status build(EffectBuildContext& ctx, const EffectEval& e, const LayerImage& input, f32 margin,
                 LayerImage& out) const override {
        const f32 reach = e.f(kBlockSize) * 4.0f;
        const Rect region = spread_region(input.region, reach, reach, e.placement, margin);
        u32 w = 0, h = 0;
        ctx.region_size(region, input.texel_scale_x(), w, h);

        EffectUniforms u;
        u.uvMap = EffectBuildContext::uv_map(region, input.region);
        u.texel = Vec4{region.w > 0.0f ? 1.0f / region.w : 0.0f, region.h > 0.0f ? 1.0f / region.h : 0.0f,
                       input.texel_scale_x(), input.texel_scale_y()};
        u.p0 = Vec4{e.f(kQuality) / 100.0f, e.f(kBlocks) / 100.0f, e.f(kRinging) / 100.0f,
                    e.f(kChroma) / 100.0f};
        u.p1 = Vec4{e.f(kBlockSize), e.f(kSmoothBlock) / 100.0f, e.f(kEdgeBoost) / 100.0f, e.f(kMix) / 100.0f};
        u.p2 = Vec4{e.f(kCorruption) / 100.0f, static_cast<f32>(e.e(kSeed)), 0.0f, 0.0f};

        out = LayerImage{ctx.texture("dano-de-jpeg", w, h), region, w, h};
        if (ctx.fullscreen_pass("dano-de-jpeg", PassStage::Effects, out.texture,
                                ShaderId::effects_jpeg_damage_frag,
                                {PassTexture{input.texture, {}, CommonSampler::LinearBorder}},
                                &u, sizeof(u)) == kInvalidIndex) {
            return Errc::PipelineCompileFailed;
        }
        return OkStatus;
    }
};

// -----------------------------------------------------------------------------
// Ordenar pixels (reescrito na 0.3.0)
//
// Uma ordenação de verdade por intervalo (shaders/effects/pixel_sort.comp):
// os pixels que passam no limiar formam intervalos ao longo da direção, e as
// cores de cada intervalo são PERMUTADAS pela chave — nada duplica nem mistura.
// Tudo é medido na camada: o comprimento é a fração da extensão da camada
// naquela direção, o passo e as barreiras sorteadas são px da camada. A
// prévia em 1/2 e o export em 4K cortam e ordenam nos mesmos lugares.
//
// Horizontal e vertical ordenam a própria imagem (exato, sem reamostrar).
// Diagonal, ângulo livre, radial e passo > 1 px vão a uma GRADE em que a
// direção vira as linhas (pixel_sort_space.frag), ordenam, e voltam; onde
// nada se moveu, o pixel original fica intacto.
//
// Antes: cada pixel refazia uma ordenação de 64 amostras fixas por ladrilho
// de 64 texels (emendas, comprimento que dependia da resolução, diagonal e
// radial borrados pela amostragem bilinear, 1,9 ms em 1080p numa RTX 3050).
// -----------------------------------------------------------------------------
class PixelSort final : public Effect {
public:
    enum : u32 { kThresholdLow = 0, kThresholdHigh, kLength, kRandomness, kDirection, kReverse,
                 kMode, kSeed, kBandMode, kStep,
                 // 0.3.0
                 kAngle, kCenter };
    enum : u32 { kHorizontal = 0, kVertical, kDiagonal, kRadial, kFreeAngle };

    const EffectInfo& info() const noexcept override {
        static const EffectInfo i{effect_keys::kPixelSort, "Ordenar pixels", "Estilizar", EffectClass::Neighborhood};
        return i;
    }
    void declare_parameters(ParameterRegistry& p) const override {
        static const char* const kDirs[] = {"Horizontal", "Vertical", "Diagonal", "Radial", "Ângulo"};
        static const char* const kModes[] = {"Luz", "Tom", "Vermelho", "Verde", "Azul", "Saturação"};
        p.add_float("threshold_low", "Limiar baixo", 25.0f, 0.0f, 100.0f, kParamAnimatable | kParamPercent, "%");
        p.add_float("threshold_high", "Limiar alto", 90.0f, 0.0f, 100.0f, kParamAnimatable | kParamPercent, "%");
        p.add_float("length", "Comprimento", 60.0f, 0.0f, 100.0f, kParamAnimatable | kParamPercent, "%");
        p.add_float("randomness", "Aleatoriedade", 25.0f, 0.0f, 100.0f, kParamAnimatable | kParamPercent, "%");
        p.add_enum("direction", "Direção", kDirs, 5, 0);
        p.add_bool("reverse", "Sentido inverso", false);
        p.add_enum("mode", "Ordenar por", kModes, 6, 0);
        p.add_int("seed", "Semente", 71, 0, 9999);
        p.add_bool("band_mode", "Por faixa de tom", false);
        p.add_float("step", "Passo", 1.0f, 1.0f, 64.0f, kParamAnimatable | kParamPixels, "px");
        p.add_angle("angle", "Ângulo", 30.0f);
        p.add_point2("center", "Centro", Vec2{0.5f, 0.5f}, -1.0f, 2.0f, kParamAnimatable | kParamRelative);
    }
    bool is_identity(const EffectEval& e) const noexcept override {
        return e.f(kLength) < 0.005f || e.f(kThresholdLow) > e.f(kThresholdHigh);
    }
    /// Um intervalo pode atravessar a camada inteira: as etapas anteriores
    /// mantêm a imagem toda (não só a parte que aparece no quadro).
    bool needs_full_input() const noexcept override { return true; }
    bool demo_values(EffectInstance&, std::vector<ParamValue>& v) const noexcept override {
        v[kThresholdLow] = ParamValue::scalar(48.0f);
        v[kThresholdHigh] = ParamValue::scalar(100.0f);
        v[kLength] = ParamValue::scalar(45.0f);
        return true;
    }
    void pipelines(std::vector<PipelineKey>& out, SurfaceFormat work) const override {
        out.push_back(PipelineKey::compute_shader(ShaderId::effects_pixel_sort_comp));
        out.push_back(PipelineKey::fullscreen(ShaderId::effects_pixel_sort_space_frag, work));
    }

    /// Ordena as linhas (eixo 0) ou colunas (eixo 1) de `src` em `dst`.
    /// `density` = texels por px da camada; `origin` = px da camada do texel 0.
    static Status sort_pass(EffectBuildContext& ctx, const EffectEval& e, FGTexture src, FGTexture dst,
                            u32 w, u32 h, u32 axis, bool reverse, f32 density, Vec2 origin, f32 extentPx) {
        auto pipeline = ctx.shaders().pipeline(PipelineKey::compute_shader(ShaderId::effects_pixel_sort_comp));
        if (!pipeline.ok()) return pipeline.status();
        struct Uniforms { Vec4 p0, p1, p2, p3; } u{};
        const f32 length = std::clamp(e.f(kLength), 0.0f, 100.0f);
        // Comprimento: fração da extensão da camada na direção (100 % = sem limite).
        const f32 maxElements = length >= 99.5f ? 0.0f
            : std::max(1.0f, std::round(length * 0.01f * extentPx * density));
        u.p0 = Vec4{std::clamp(e.f(kThresholdLow), 0.0f, 100.0f) * 0.01f, std::clamp(e.f(kThresholdHigh), 0.0f, 100.0f) * 0.01f,
                    maxElements, std::clamp(e.f(kRandomness), 0.0f, 100.0f) * 0.01f * 0.15f};
        u.p1 = Vec4{static_cast<f32>(axis), reverse ? 1.0f : 0.0f, static_cast<f32>(std::min<u32>(e.e(kMode), 5u)),
                    static_cast<f32>(std::max(0, static_cast<i32>(e.f(kSeed))))};
        u.p2 = Vec4{e.b(kBandMode) ? 1.0f : 0.0f, density, std::max(1.0f, e.f(kStep)), 0.0f};
        u.p3 = Vec4{origin.x, origin.y, static_cast<f32>(w), static_cast<f32>(h)};
        const u32 lines = axis ? w : h;
        const u32 along = axis ? h : w;
        const u32 chunks = (along + 2047u) / 2048u;
        const auto sampler = ctx.shaders().sampler(CommonSampler::NearestClamp);
        const auto pipe = *pipeline;
        const u32 pass = ctx.graph().add_compute_pass("ordenar-pixels", PassStage::Effects,
            [u, src, dst, sampler, pipe, lines, chunks](PassContext& pc) {
                pc.cmds.bind_pipeline(pipe);
                pc.cmds.bind_texture(0, pc.texture(src), sampler);
                pc.cmds.bind_storage_image(0, pc.texture(dst));
                pc.cmds.set_uniforms(&u, sizeof(u));
                pc.cmds.dispatch(lines, chunks, 1);
            });
        ctx.graph().read(pass, src);
        ctx.graph().write_storage(pass, dst);
        return OkStatus;
    }

    static FGTexture storage_texture(EffectBuildContext& ctx, const char* name, u32 w, u32 h) {
        TextureDesc d;
        d.width = w; d.height = h; d.sampled = true; d.storage = true; d.renderTarget = true;
        d.format = SurfaceFormat::RGBA16F;
        d.debugName = name;
        return ctx.graph().create_texture(name, d);
    }

    Status build(EffectBuildContext& ctx, const EffectEval& e, const LayerImage& input, f32,
                 LayerImage& out) const override {
        const Rect region = input.region;
        const u32 w = input.width, h = input.height;
        if (!w || !h || region.w <= 0.0f || region.h <= 0.0f) { out = input; return OkStatus; }
        const f32 density = input.texel_scale_x();   // texels por px da camada
        const f32 stepPx = std::max(1.0f, e.f(kStep));
        const u32 dir = std::min<u32>(e.e(kDirection), kFreeAngle);
        bool reverse = e.b(kReverse);
        const f32 layerW = e.placement && e.placement->layerWidth ? static_cast<f32>(e.placement->layerWidth) : region.w;
        const f32 layerH = e.placement && e.placement->layerHeight ? static_cast<f32>(e.placement->layerHeight) : region.h;

        // Ângulo da direção (graus, sentido de x para y).
        f32 degrees = dir == kVertical ? 90.0f : dir == kDiagonal ? 45.0f : dir == kFreeAngle ? e.f(kAngle) : 0.0f;
        if (!std::isfinite(degrees)) degrees = 0.0f;
        degrees -= 360.0f * std::floor(degrees / 360.0f);
        // Múltiplos de 90° e passo de um texel: a própria imagem, sem reamostrar.
        const f32 quarter = std::round(degrees / 90.0f);
        const bool aligned = dir != kRadial && std::fabs(degrees - quarter * 90.0f) < 1e-3f;
        // Passo 1 = sem blocos (na densidade da imagem, também acima de 1 texel/px).
        const bool unitStep = stepPx <= 1.0001f;
        if (aligned && unitStep) {
            const u32 q = static_cast<u32>(quarter) % 4u;      // 0 → x, 1 → y, 2 → −x, 3 → −y
            const u32 axis = q % 2u;
            if (q >= 2u) reverse = !reverse;
            out = LayerImage{storage_texture(ctx, "ordenar-pixels", w, h), region, w, h};
            return sort_pass(ctx, e, input.texture, out.texture, w, h, axis, reverse, density,
                             Vec2{region.x, region.y}, axis ? layerH : layerW);
        }

        // GRADE: a direção vira as linhas. Densidade = a da imagem (passo 1) ou
        // um texel por bloco de `passo` px da camada.
        f32 gd = unitStep ? density : std::min(density, 1.0f / stepPx);
        gd = std::clamp(gd, 1e-4f, 16.0f);
        // O centro do giro é o da CAMADA (não o da região, que muda quando uma
        // etapa anterior recorta): a grade fica parada no tempo.
        const Vec2 rel = e.p2(kCenter);
        Vec2 center{layerW * 0.5f, layerH * 0.5f};
        if (dir == kRadial) center = Vec2{std::isfinite(rel.x) ? rel.x * layerW : layerW * 0.5f,
                                          std::isfinite(rel.y) ? rel.y * layerH : layerH * 0.5f};
        const Vec2 corners[4] = {Vec2{region.x, region.y}, Vec2{region.x + region.w, region.y},
                                 Vec2{region.x, region.y + region.h}, Vec2{region.x + region.w, region.y + region.h}};
        struct SpaceUniforms { Vec4 a0, a1, a2, a3; } su{};
        f32 gwf = 1.0f, ghf = 1.0f, extentPx = 0.0f, c = 1.0f, s = 0.0f;
        Vec2 origin{0.0f, 0.0f};
        if (dir == kRadial) {
            // Raio até o canto mais longe da região; uma linha por ângulo.
            f32 rmax = 1.0f;
            for (Vec2 k : corners) rmax = std::max(rmax, std::hypot(k.x - center.x, k.y - center.y));
            gwf = rmax * gd;
            ghf = std::max(8.0f, kTwoPi * rmax * gd);
            extentPx = rmax;
        } else {
            const f32 rad = degrees * kDeg2Rad;
            c = std::cos(rad); s = std::sin(rad);
            // Meia caixa da região no referencial girado (em volta do centro).
            f32 hx = 0.5f, hy = 0.5f;
            for (Vec2 k : corners) {
                const Vec2 d{k.x - center.x, k.y - center.y};
                hx = std::max(hx, std::fabs(c * d.x + s * d.y));
                hy = std::max(hy, std::fabs(-s * d.x + c * d.y));
            }
            gwf = 2.0f * hx * gd;
            ghf = 2.0f * hy * gd;
            extentPx = std::fabs(layerW * c) + std::fabs(layerH * s);
        }
        // Teto do aparelho: a densidade cai junto (a grade inteira cabe).
        const f32 maxTex = static_cast<f32>(std::max<u32>(64u, std::min<u32>(ctx.max_texture_size(), 8192u)));
        if (gwf > maxTex || ghf > maxTex) {
            const f32 k = maxTex / std::max(gwf, ghf);
            gd *= k; gwf *= k; ghf *= k;
        }
        const u32 gw = static_cast<u32>(std::clamp(std::ceil(gwf), 1.0f, maxTex));
        const u32 gh = static_cast<u32>(std::clamp(std::ceil(ghf), 1.0f, maxTex));
        if (dir == kRadial) {
            su.a0 = Vec4{2.0f, 1.0f, 0.0f, gd};
            su.a3 = Vec4{static_cast<f32>(gh), 0, 0, 0};
        } else {
            su.a0 = Vec4{0.0f, c, s, gd};
            // Barreiras em px do referencial girado, a partir do centro.
            origin = Vec2{-0.5f * static_cast<f32>(gw) / gd, -0.5f * static_cast<f32>(gh) / gd};
        }
        su.a1 = Vec4{center.x, center.y, static_cast<f32>(gw), static_cast<f32>(gh)};
        su.a2 = Vec4{region.x, region.y, region.w, region.h};
        // Em blocos (passo, alinhado), a grade é amostrada no ponto; girada, bilinear.
        const CommonSampler gridSampler = aligned ? CommonSampler::NearestClamp : CommonSampler::LinearClamp;
        const FGTexture before = ctx.texture("ordenar-pixels-grade", gw, gh);
        if (ctx.fullscreen_pass("ordenar-pixels-grade", PassStage::Effects, before, ShaderId::effects_pixel_sort_space_frag,
                                {PassTexture{input.texture, {}, gridSampler}}, &su, sizeof(su)) == kInvalidIndex)
            return Errc::PipelineCompileFailed;
        const FGTexture sorted = storage_texture(ctx, "ordenar-pixels-ordenada", gw, gh);
        if (const Status st = sort_pass(ctx, e, before, sorted, gw, gh, 0, reverse, gd, origin, extentPx); !st.ok())
            return st;
        su.a0.x = dir == kRadial ? 3.0f : 1.0f;
        out = LayerImage{ctx.texture("ordenar-pixels", w, h), region, w, h};
        if (ctx.fullscreen_pass("ordenar-pixels-volta", PassStage::Effects, out.texture, ShaderId::effects_pixel_sort_space_frag,
                                {PassTexture{sorted, {}, gridSampler}, PassTexture{before, {}, gridSampler},
                                 PassTexture{input.texture, {}, CommonSampler::NearestClamp}},
                                &su, sizeof(su)) == kInvalidIndex)
            return Errc::PipelineCompileFailed;
        return OkStatus;
    }
};

} // namespace

void register_glitch_effects(EffectRegistry& r) {
    (void)r.add(std::make_unique<Glitchify>());
    (void)r.add(std::make_unique<Vhs>());
    (void)r.add(std::make_unique<UniVhs>());
    (void)r.add(std::make_unique<Signal>());
    (void)r.add(std::make_unique<CrossGlitch>());
    (void)r.add(std::make_unique<HoloMatrix>());
    (void)r.add(std::make_unique<FilmDamage>());
    (void)r.add(std::make_unique<JpegDamage>());
    (void)r.add(std::make_unique<PixelSort>());
}

} // namespace aurea::builtin
