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
    enum : u32 { kBandHeight = 0, kDisplace, kSpike, kChannelSplit, kFrequency, kSeed,
                 kFreeze, kMix, kVertical, kColorCorruption };

    const EffectInfo& info() const noexcept override {
        static const EffectInfo i{effect_keys::kGlitchify, "Glitchify", "Glitch", EffectClass::Domain};
        return i;
    }
    void declare_parameters(ParameterRegistry& p) const override {
        p.add_float("band_height", "Altura da faixa", 24.0f, 1.0f, 300.0f, kParamAnimatable | kParamPixels, "px");
        p.add_float("displace", "Deslocamento", 30.0f, 0.0f, 500.0f, kParamAnimatable | kParamPixels, "px");
        p.add_float("spike", "Picos", 15.0f, 0.0f, 100.0f, kParamAnimatable | kParamPercent, "%");
        p.add_float("channel_split", "Separação RGB", 8.0f, 0.0f, 200.0f, kParamAnimatable | kParamPixels, "px");
        p.add_float("frequency", "Frequência", 3.0f, 0.25f, 60.0f, kParamAnimatable, "quadros");
        p.add_int("seed", "Semente", 11, 0, 9999);
        p.add_bool("freeze", "Travar o quadro", false);
        p.add_float("mix", "Mistura", 100.0f, 0.0f, 100.0f, kParamAnimatable | kParamPercent, "%");
        p.add_bool("vertical", "Blocos verticais", false);
        p.add_float("color_corruption", "Corrupção de cor", 30.0f, 0.0f, 100.0f, kParamAnimatable | kParamPercent, "%");
    }
    bool is_identity(const EffectEval& e) const noexcept override {
        return e.f(kMix) < 0.01f || (e.f(kDisplace) < 0.01f && e.f(kChannelSplit) < 0.01f && e.f(kSpike) < 0.01f);
    }
    f32 input_margin(const EffectEval& e) const noexcept override {
        return e.f(kDisplace) * 7.0f + e.f(kChannelSplit) * 2.0f;
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
        const f32 reach = e.f(kDisplace) * 7.0f + e.f(kChannelSplit) * 2.0f;
        const Rect region = spread_region(input.region, reach, reach, e.placement, margin);
        u32 w = 0, h = 0;
        ctx.region_size(region, input.texel_scale_x(), w, h);

        EffectUniforms u;
        u.uvMap = EffectBuildContext::uv_map(region, input.region);
        u.texel = Vec4{region.w > 0.0f ? 1.0f / region.w : 0.0f, region.h > 0.0f ? 1.0f / region.h : 0.0f,
                       input.texel_scale_x(), input.texel_scale_y()};
        u.p0 = Vec4{e.f(kBandHeight), e.f(kDisplace), e.f(kSpike) / 100.0f, e.f(kChannelSplit)};
        u.p1 = Vec4{std::max(0.25f, e.f(kFrequency)), static_cast<f32>(e.e(kSeed)),
                    e.b(kFreeze) ? 1.0f : 0.0f, e.f(kMix) / 100.0f};
        u.p2 = Vec4{e.b(kVertical) ? 1.0f : 0.0f, e.f(kColorCorruption) / 100.0f, 0.0f, 0.0f};
        u.p3 = Vec4{static_cast<f32>(e.localTime.value), 0.0f, 0.0f, 0.0f};

        out = LayerImage{ctx.texture("glitchify", w, h), region, w, h};
        if (ctx.fullscreen_pass("glitchify", PassStage::Transform, out.texture, ShaderId::effects_glitchify_frag,
                                {PassTexture{input.texture, {}, CommonSampler::LinearBorder}},
                                &u, sizeof(u)) == kInvalidIndex) {
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
        p.add_float("drift", "Deriva", 0.4f, -20.0f, 20.0f, kParamAnimatable | kParamPixels, "px/quadro");
        p.add_float("band_height", "Altura da banda", 40.0f, 2.0f, 400.0f, kParamAnimatable | kParamPixels, "px");
        p.add_float("noise", "Ruído de sinal", 30.0f, 0.0f, 100.0f, kParamAnimatable | kParamPercent, "%");
        p.add_int("seed", "Semente", 23, 0, 9999);
        p.add_float("frequency", "Frequência", 4.0f, 0.25f, 60.0f, kParamAnimatable, "quadros");
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
        p.add_float("frequency", "Frequência", 2.0f, 0.25f, 60.0f, kParamAnimatable, "quadros");
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
// Ordenar pixels
// -----------------------------------------------------------------------------
class PixelSort final : public Effect {
public:
    enum : u32 { kThresholdLow = 0, kThresholdHigh, kLength, kRandomness, kDirection, kReverse,
                 kMode, kSeed, kBandMode, kStep };

    const EffectInfo& info() const noexcept override {
        static const EffectInfo i{effect_keys::kPixelSort, "Ordenar pixels", "Estilizar", EffectClass::Domain};
        return i;
    }
    void declare_parameters(ParameterRegistry& p) const override {
        static const char* const kDirs[] = {"Horizontal", "Vertical", "Diagonal", "Radial"};
        static const char* const kModes[] = {"Luz", "Tom", "Vermelho", "Verde", "Azul"};
        p.add_float("threshold_low", "Limiar baixo", 25.0f, 0.0f, 100.0f, kParamAnimatable | kParamPercent, "%");
        p.add_float("threshold_high", "Limiar alto", 90.0f, 0.0f, 100.0f, kParamAnimatable | kParamPercent, "%");
        p.add_float("length", "Comprimento", 60.0f, 0.0f, 100.0f, kParamAnimatable | kParamPercent, "%");
        p.add_float("randomness", "Aleatoriedade", 25.0f, 0.0f, 100.0f, kParamAnimatable | kParamPercent, "%");
        p.add_enum("direction", "Direção", kDirs, 4, 0);
        p.add_bool("reverse", "Sentido inverso", false);
        p.add_enum("mode", "Ordenar por", kModes, 5, 0);
        p.add_int("seed", "Semente", 71, 0, 9999);
        p.add_bool("band_mode", "Por faixa de tom", false);
        p.add_float("step", "Passo", 1.0f, 1.0f, 16.0f, kParamAnimatable | kParamPixels, "px");
    }
    bool is_identity(const EffectEval& e) const noexcept override { return e.f(kLength) < 0.005f; }
    f32 input_margin(const EffectEval& e) const noexcept override {
        // A ordenação lê a linha inteira na direção escolhida.
        const f32 reach = e.f(kLength) * 96.0f * e.f(kStep);
        return std::min(reach, 4096.0f);
    }
    bool demo_values(EffectInstance&, std::vector<ParamValue>& v) const noexcept override {
        v[kThresholdLow] = ParamValue::scalar(48.0f);
        v[kThresholdHigh] = ParamValue::scalar(100.0f);
        v[kLength] = ParamValue::scalar(45.0f);
        return true;
    }
    Status build(EffectBuildContext& ctx, const EffectEval& e, const LayerImage& input, f32 margin,
                 LayerImage& out) const override {
        const f32 reach = std::min(e.f(kLength) * 96.0f * e.f(kStep), 4096.0f);
        const Rect region = spread_region(input.region, reach, reach, e.placement, margin);
        u32 w = 0, h = 0;
        ctx.region_size(region, input.texel_scale_x(), w, h);

        EffectUniforms u;
        u.uvMap = EffectBuildContext::uv_map(region, input.region);
        u.texel = Vec4{region.w > 0.0f ? 1.0f / region.w : 0.0f, region.h > 0.0f ? 1.0f / region.h : 0.0f,
                       input.texel_scale_x(), input.texel_scale_y()};
        u.p0 = Vec4{e.f(kThresholdLow) / 100.0f, e.f(kThresholdHigh) / 100.0f, e.f(kLength) / 100.0f,
                    e.f(kRandomness) / 100.0f};
        u.p1 = Vec4{static_cast<f32>(e.e(kDirection)), e.b(kReverse) ? 1.0f : 0.0f,
                    static_cast<f32>(e.e(kMode)), static_cast<f32>(e.e(kSeed))};
        u.p2 = Vec4{e.b(kBandMode) ? 1.0f : 0.0f, 0.0f, 0.0f, 0.0f};
        u.p3 = Vec4{e.f(kStep), 0.0f, 0.0f, 0.0f};

        out = LayerImage{ctx.texture("ordenar-pixels", w, h), region, w, h};
        if (ctx.fullscreen_pass("ordenar-pixels", PassStage::Transform, out.texture,
                                ShaderId::effects_pixel_sort_frag,
                                {PassTexture{input.texture, {}, CommonSampler::LinearBorder}},
                                &u, sizeof(u)) == kInvalidIndex) {
            return Errc::PipelineCompileFailed;
        }
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
