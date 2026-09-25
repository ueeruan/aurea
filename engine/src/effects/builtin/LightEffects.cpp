// =============================================================================
//  Efeitos de luz e cor (Fase 7.3 §29, §35, §37, §41, §37).
//
//  Brilho profundo · Raios · Faixa de luz · Desfoque de lente · Colorama
//
//  Brilho profundo e desfoque de lente são os dois que montam MAIS DE UM
//  passe: o brilho precisa de dois halos em raios diferentes, e a lente desenha
//  um disco de amostras que fica caro se o raio crescer. Nos dois casos o
//  segredo é reduzir a imagem antes — um halo de raio r na escala 1/k é um halo
//  de raio r/k, e o desenho não muda.
// =============================================================================
#include "BuiltinEffects.hpp"

#include <algorithm>
#include <cmath>

namespace aurea::builtin {
namespace {

/// Quanto reduzir para que um raio em pixels caiba no orçamento do shader.
u32 reduction_for(f32 radius, f32 limit) noexcept {
    if (!(radius > limit) || limit <= 0.0f) return 1;
    u32 k = 1;
    while (radius / static_cast<f32>(k) > limit && k < 8) ++k;
    return k;
}

// -----------------------------------------------------------------------------
// Brilho profundo — dois halos em raios diferentes
// -----------------------------------------------------------------------------
class DeepGlow final : public Effect {
public:
    enum : u32 { kThreshold = 0, kCoreRadius, kHaloRadius, kCoreIntensity, kHaloIntensity,
                 kColor, kPreserveShadows, kScreen, kTintCore, kTintHalo, kOnlyGlow, kClip };

    const EffectInfo& info() const noexcept override {
        static const EffectInfo i{effect_keys::kDeepGlow, "Brilho profundo", "Luz", EffectClass::Neighborhood};
        return i;
    }
    void declare_parameters(ParameterRegistry& p) const override {
        p.add_float("threshold", "Limite", 55.0f, 0.0f, 100.0f, kParamAnimatable | kParamPercent, "%");
        p.add_float("core_radius", "Raio do núcleo", 12.0f, 0.0f, 400.0f, kParamAnimatable | kParamPixels, "px");
        p.add_float("halo_radius", "Raio do halo", 70.0f, 0.0f, 800.0f, kParamAnimatable | kParamPixels, "px");
        p.add_float("core_intensity", "Força do núcleo", 1.4f, 0.0f, 8.0f);
        p.add_float("halo_intensity", "Força do halo", 0.8f, 0.0f, 8.0f);
        p.add_color("glow_color", "Cor do brilho", Vec4{1, 1, 1, 1});
        p.add_bool("preserve_shadows", "Preservar as sombras", true);
        p.add_bool("screen_halo", "Halo em tela", true);
        p.add_bool("tint_core", "Tingir o núcleo", false);
        p.add_bool("tint_halo", "Tingir o halo", true);
        p.add_bool("only_glow", "Só o brilho", false);
        p.add_float("clip", "Estouro", 100.0f, 10.0f, 400.0f, kParamAnimatable | kParamPercent, "%");
    }
    bool is_identity(const EffectEval& e) const noexcept override {
        return (e.f(kCoreIntensity) < 1e-3f && e.f(kHaloIntensity) < 1e-3f);
    }
    f32 input_margin(const EffectEval& e) const noexcept override {
        return std::max(e.f(kCoreRadius), e.f(kHaloRadius));
    }
    void pipelines(std::vector<PipelineKey>& out, SurfaceFormat work) const override {
        out.push_back(PipelineKey::fullscreen(ShaderId::effects_bright_pass_frag, work));
        out.push_back(PipelineKey::fullscreen(ShaderId::effects_gaussian_blur_frag, work));
        out.push_back(PipelineKey::fullscreen(ShaderId::effects_downsample_frag, work));
        out.push_back(PipelineKey::fullscreen(ShaderId::effects_deep_glow_combine_frag, work));
    }
    bool demo_values(EffectInstance&, std::vector<ParamValue>& v) const noexcept override {
        // Calibrado na foto das prévias (clara): limite alto para só as luzes
        // acenderem; com 48% / 1,6 o card estourava 83% dos pixels.
        v[kThreshold] = ParamValue::scalar(92.0f);
        v[kCoreRadius] = ParamValue::scalar(8.0f);
        v[kHaloRadius] = ParamValue::scalar(40.0f);
        v[kCoreIntensity] = ParamValue::scalar(0.6f);
        v[kHaloIntensity] = ParamValue::scalar(0.5f);
        v[kColor] = ParamValue::color(0.75f, 0.88f, 1.0f, 1.0f);
        return true;
    }

    /// Redução da imagem clara para um halo de raio `radius` (px da layer):
    /// o bastante para o gaussiano caber no orçamento (48 px, a regra de antes)
    /// e, com o raio ≥ 12 TEXELS da resolução de trabalho, pelo menos 1/2 — um
    /// borrão de σ ≥ 4 texels não tem detalhe que a meia resolução perca (o
    /// diff do export está no relatório 8E: só as curvas de nível do "estouro"
    /// andam alguns px; média 0,08/255).
    static u32 bright_reduction(f32 radius, f32 texelScale) noexcept {
        return std::max(reduction_for(std::max(radius, 1.0f), 48.0f), radius * texelScale >= 12.0f ? 2u : 1u);
    }

    /// A imagem clara (limiar com joelho), já reduzida por `r`.
    static Status bright(EffectBuildContext& ctx, const EffectEval& e, const LayerImage& input, const Rect& region, u32 r,
                         const char* label, LayerImage& out) {
        const f32 k = input.texel_scale_x();
        u32 bw = 0, bh = 0;
        ctx.region_size(region, k / static_cast<f32>(r), bw, bh);
        const FGTexture tex = ctx.texture(label, bw, bh);
        const f32 t = e.f(kThreshold) / 100.0f;
        const f32 knee = std::max(1e-4f, t * 0.5f);
        struct {
            Vec4 uvMap;
            Vec4 texel;
            Vec4 knee;
        } ub{};
        ub.uvMap = EffectBuildContext::uv_map(region, input.region);
        ub.texel = Vec4{1.0f / static_cast<f32>(input.width), 1.0f / static_cast<f32>(input.height), 0, 0};
        ub.knee = Vec4{t, knee, 1.0f / (4.0f * knee), 0.0f};
        if (ctx.fullscreen_pass(label, PassStage::Effects, tex, ShaderId::effects_bright_pass_frag,
                                {PassTexture{input.texture, {}, CommonSampler::LinearBorder}},
                                &ub, sizeof(ub)) == kInvalidIndex) {
            return Errc::PipelineCompileFailed;
        }
        out = LayerImage{tex, region, bw, bh};
        return OkStatus;
    }

    /// Um halo a partir da imagem clara: o gaussiano segue a PIRÂMIDE a partir
    /// dela (build_gaussian reduz por 2 enquanto σ passar de 8 texels).
    static Status blur_halo(EffectBuildContext& ctx, const LayerImage& brightImage, const Rect& region, f32 radius,
                            LayerImage& out) {
        out = brightImage;
        if (radius <= 0.5f) return OkStatus;
        BlurRequest req;
        // O sigma é medido em pixels da LAYER: o gaussiano o converte pela
        // densidade da imagem que recebe (já reduzida). Aqui NÃO se multiplica
        // pela redução — seria desfocar r² vezes.
        req.sigmaX = req.sigmaY = radius / 3.0f;
        req.repeatEdges = false;
        req.outRegion = region;
        req.label = "brilho-profundo";
        return build_gaussian(ctx, brightImage, req, out);
    }

    Status build(EffectBuildContext& ctx, const EffectEval& e, const LayerImage& input, f32 margin,
                 LayerImage& out) const override {
        const f32 coreR = std::max(0.0f, e.f(kCoreRadius));
        const f32 haloR = std::max(0.0f, e.f(kHaloRadius));
        const Rect region = spread_region(input.region, std::max(coreR, haloR), std::max(coreR, haloR),
                                          e.placement, margin);

        // Núcleo e halo (8E): o núcleo largo (≥ 8 texels) vive em 1/2 — antes
        // era borrado em resolução cheia (≈ 70% do custo do efeito). Quando os
        // dois caem na MESMA redução, a imagem clara é uma só (o limiar não
        // depende do raio); com reduções diferentes, cada um tem a sua, como
        // antes — o halo sai idêntico.
        const bool wantCore = e.f(kCoreIntensity) > 1e-4f, wantHalo = e.f(kHaloIntensity) > 1e-4f;
        LayerImage core{input.texture, input.region, input.width, input.height};
        LayerImage haloImage{input.texture, input.region, input.width, input.height};
        const f32 k = input.texel_scale_x();
        const u32 rCore = bright_reduction(coreR, k), rHalo = bright_reduction(haloR, k);
        LayerImage coreBright, haloBright;
        if (wantCore) {
            if (const Status s = bright(ctx, e, input, region, rCore, "brilho-nucleo", coreBright); !s.ok()) return s;
            if (const Status s = blur_halo(ctx, coreBright, region, coreR, core); !s.ok()) return s;
        }
        if (wantHalo) {
            if (wantCore && rHalo == rCore) haloBright = coreBright;
            else if (const Status s = bright(ctx, e, input, region, rHalo, "brilho-halo", haloBright); !s.ok()) return s;
            if (const Status s = blur_halo(ctx, haloBright, region, haloR, haloImage); !s.ok()) return s;
        }

        u32 w = 0, h = 0;
        ctx.region_size(region, input.texel_scale_x(), w, h);
        EffectUniforms u;
        u.uvMap = EffectBuildContext::uv_map(region, input.region);
        u.texel = Vec4{w > 0 ? 1.0f / static_cast<f32>(w) : 0.0f, h > 0 ? 1.0f / static_cast<f32>(h) : 0.0f, 0, 0};
        u.p0 = Vec4{e.f(kCoreIntensity), e.f(kHaloIntensity), e.f(kClip) / 100.0f,
                    e.b(kPreserveShadows) ? 1.0f : 0.0f};
        u.p1 = Vec4{e.b(kScreen) ? 1.0f : 0.0f, e.b(kTintCore) ? 1.0f : 0.0f,
                    e.b(kTintHalo) ? 1.0f : 0.0f, e.b(kOnlyGlow) ? 1.0f : 0.0f};
        u.color = e.color(kColor);

        out = LayerImage{ctx.texture("brilho-profundo", w, h), region, w, h};
        if (ctx.fullscreen_pass("brilho-profundo", PassStage::Effects, out.texture,
                                ShaderId::effects_deep_glow_combine_frag,
                                {PassTexture{input.texture, {}, CommonSampler::LinearBorder},
                                 PassTexture{core.texture, {}, CommonSampler::LinearClamp},
                                 PassTexture{haloImage.texture, {}, CommonSampler::LinearClamp}},
                                &u, sizeof(u)) == kInvalidIndex) {
            return Errc::PipelineCompileFailed;
        }
        return OkStatus;
    }
};

// Film-style warm highlight bleed: positive blurred highlight energy minus
// its local source. Uniform bright interiors stay neutral; dark-side edges
// receive a warm halo instead of a second broad bloom layer.
class Halation final : public Effect {
public:
    enum : u32 { kThreshold, kRadius, kAmount, kSoftness, kEdges, kTint, kOnlyHalo };
    const EffectInfo& info() const noexcept override {
        static const EffectInfo i{effect_keys::kHalation, "Halation de filme", "Luz", EffectClass::Neighborhood};
        return i;
    }
    void declare_parameters(ParameterRegistry& p) const override {
        p.add_float("threshold", "Limite das luzes", 60.0f, 0.0f, 100.0f, kParamAnimatable | kParamPercent, "%");
        p.add_float("radius", "Raio", 18.0f, 0.0f, 300.0f, kParamAnimatable | kParamPixels, "px");
        p.add_float("amount", "Intensidade", 100.0f, 0.0f, 400.0f, kParamAnimatable | kParamPercent, "%");
        p.add_float("softness", "Suavidade do limite", 10.0f, 0.0f, 50.0f, kParamAnimatable | kParamPercent, "%");
        p.add_float("edges", "Preservar núcleo", 100.0f, 0.0f, 100.0f, kParamAnimatable | kParamPercent, "%");
        p.add_color("tint", "Cor do halo", Vec4{1.0f, 0.08f, 0.015f, 1.0f});
        p.add_bool("only_halo", "Só o halo", false);
    }
    bool is_identity(const EffectEval& e) const noexcept override {
        return !e.b(kOnlyHalo) && e.f(kAmount) <= 1e-4f;
    }
    f32 input_margin(const EffectEval& e) const noexcept override { return std::max(0.0f, e.f(kRadius)); }
    void pipelines(std::vector<PipelineKey>& out, SurfaceFormat work) const override {
        for (ShaderId id : {ShaderId::effects_halation_extract_frag, ShaderId::effects_gaussian_blur_frag,
                            ShaderId::effects_downsample_frag, ShaderId::effects_halation_combine_frag}) {
            out.push_back(PipelineKey::fullscreen(id, work));
        }
    }
    Status build(EffectBuildContext& ctx, const EffectEval& e, const LayerImage& input, f32 margin,
                 LayerImage& out) const override {
        const f32 radius = std::max(0.0f, e.f(kRadius));
        const Rect region = spread_region(input.region, radius, radius, e.placement, margin);
        const u32 reduction = reduction_for(radius * input.texel_scale_x(), 24.0f);
        u32 bw = 0, bh = 0;
        ctx.region_size(region, input.texel_scale_x() / static_cast<f32>(reduction), bw, bh);
        LayerImage mask{ctx.texture("halation-highlights", bw, bh), region, bw, bh};
        EffectUniforms extract;
        extract.uvMap = EffectBuildContext::uv_map(region, input.region);
        extract.p0 = Vec4{e.f(kThreshold) / 100.0f, e.f(kSoftness) / 100.0f, 0, 0};
        if (ctx.fullscreen_pass("halation-highlights", PassStage::Effects, mask.texture,
            ShaderId::effects_halation_extract_frag,
            {PassTexture{input.texture, {}, CommonSampler::LinearBorder}}, &extract, sizeof(extract)) == kInvalidIndex) {
            return Errc::PipelineCompileFailed;
        }
        LayerImage blurred = mask;
        if (radius > 0.0f) {
            BlurRequest request;
            request.sigmaX = request.sigmaY = radius / 3.0f;
            request.repeatEdges = false;
            request.outRegion = region;
            request.label = "halation-diffusion";
            if (const Status status = build_gaussian(ctx, mask, request, blurred); !status.ok()) return status;
        }
        u32 w = 0, h = 0;
        ctx.region_size(region, input.texel_scale_x(), w, h);
        out = LayerImage{ctx.texture("halation", w, h), region, w, h};
        EffectUniforms combine;
        combine.uvMap = EffectBuildContext::uv_map(region, input.region);
        combine.p0 = Vec4{e.f(kAmount) / 100.0f, e.f(kEdges) / 100.0f, e.b(kOnlyHalo) ? 1.0f : 0.0f, 0};
        combine.p1 = EffectBuildContext::uv_map(region, mask.region);
        combine.p2 = EffectBuildContext::uv_map(region, blurred.region);
        combine.color = e.color(kTint);
        if (ctx.fullscreen_pass("halation", PassStage::Effects, out.texture, ShaderId::effects_halation_combine_frag,
            {PassTexture{input.texture, {}, CommonSampler::LinearBorder},
             PassTexture{mask.texture, {}, CommonSampler::LinearBorder},
             PassTexture{blurred.texture, {}, CommonSampler::LinearBorder}}, &combine, sizeof(combine)) == kInvalidIndex) {
            return Errc::PipelineCompileFailed;
        }
        return OkStatus;
    }
};

// -----------------------------------------------------------------------------
// Raios
// -----------------------------------------------------------------------------
class Rays final : public Effect {
public:
    enum : u32 { kIntensity = 0, kLength, kThreshold, kDecay, kCenter, kSamples, kKnee,
                 kKeepSource, kColorShift, kColor };

    const EffectInfo& info() const noexcept override {
        static const EffectInfo i{effect_keys::kRays, "Raios de luz", "Luz", EffectClass::Neighborhood};
        return i;
    }
    void declare_parameters(ParameterRegistry& p) const override {
        p.add_float("intensity", "Intensidade", 1.0f, 0.0f, 8.0f, kParamAnimatable);
        p.add_float("length", "Comprimento", 70.0f, 0.0f, 100.0f, kParamAnimatable | kParamPercent, "%");
        p.add_float("threshold", "Limite", 45.0f, 0.0f, 100.0f, kParamAnimatable | kParamPercent, "%");
        p.add_float("decay", "Decaimento", 65.0f, 0.0f, 100.0f, kParamAnimatable | kParamPercent, "%");
        p.add_point2("center", "Ponto de luz", Vec2{0.5f, 0.5f}, -1.0f, 2.0f, kParamAnimatable | kParamRelative);
        p.add_float("samples", "Amostras", 32.0f, 8.0f, 64.0f);
        p.add_float("knee", "Suavidade do limite", 10.0f, 1.0f, 50.0f, kParamAnimatable | kParamPercent, "%");
        p.add_bool("keep_source_color", "Guardar a cor da fonte", true);
        p.add_angle("color_shift", "Girar a cor", 0.0f);
        p.add_color("color", "Cor dos raios", Vec4{1, 1, 1, 1});
    }
    bool is_identity(const EffectEval& e) const noexcept override { return e.f(kIntensity) < 1e-3f; }
    f32 input_margin(const EffectEval&) const noexcept override { return 0.0f; }
    bool demo_values(EffectInstance&, std::vector<ParamValue>& v) const noexcept override {
        v[kIntensity] = ParamValue::scalar(1.6f);
        v[kThreshold] = ParamValue::scalar(52.0f);
        v[kLength] = ParamValue::scalar(85.0f);
        v[kCenter] = ParamValue::vec2(0.30f, 0.86f);   // o clarão da cartela
        return true;
    }
    Status build(EffectBuildContext& ctx, const EffectEval& e, const LayerImage& input, f32,
                 LayerImage& out) const override {
        EffectUniforms u = base_uniforms(input);
        // O ponto de luz é relativo; o shader trabalha em uv, então basta
        // passar as coordenadas relativas.
        const Vec2 c = e.p2(kCenter);
        u.p0 = Vec4{e.f(kIntensity), e.f(kLength) / 100.0f, e.f(kThreshold) / 100.0f, e.f(kDecay) / 100.0f};
        // Amostras ao longo do raio: o preview adaptativo reduz (mín. 8); export = as do usuário.
        const f32 samples = std::max(std::min(8.0f, e.f(kSamples)), std::round(e.f(kSamples) * ctx.resources().effect_quality()));
        u.p1 = Vec4{c.x, c.y, samples, e.f(kKnee) / 100.0f};
        u.p2 = Vec4{e.b(kKeepSource) ? 1.0f : 0.0f, e.f(kColorShift), 0.0f, 0.0f};
        u.color = e.color(kColor);
        return single_pass(ctx, ShaderId::effects_rays_frag, input, u, "raios", out);
    }
};

// -----------------------------------------------------------------------------
// Faixa de luz
// -----------------------------------------------------------------------------
class LightSweep final : public Effect {
public:
    enum : u32 { kCenter = 0, kWidth, kIntensity, kSoftness, kAngle, kRelief, kMultiply,
                 kFollowImage, kColor };

    const EffectInfo& info() const noexcept override {
        static const EffectInfo i{effect_keys::kLightSweep, "Faixa de luz", "Luz", EffectClass::Neighborhood};
        return i;
    }
    void declare_parameters(ParameterRegistry& p) const override {
        p.add_float("center", "Posição", -50.0f, -100.0f, 200.0f, kParamAnimatable | kParamPercent, "%");
        p.add_float("width", "Largura", 25.0f, 0.0f, 200.0f, kParamAnimatable | kParamPercent, "%");
        p.add_float("intensity", "Intensidade", 1.2f, 0.0f, 8.0f, kParamAnimatable);
        p.add_float("softness", "Suavidade da borda", 15.0f, 0.0f, 100.0f, kParamAnimatable | kParamPercent, "%");
        p.add_angle("angle", "Ângulo", 90.0f);
        p.add_float("relief", "Relevo", 0.0f, 0.0f, 100.0f, kParamAnimatable | kParamPercent, "%");
        p.add_bool("multiply", "Multiplicar", false);
        p.add_bool("follow_image", "Só onde a imagem é clara", false);
        p.add_color("color", "Cor da luz", Vec4{1, 0.97f, 0.92f, 1.0f});
    }
    bool is_identity(const EffectEval& e) const noexcept override { return e.f(kIntensity) < 1e-3f || e.f(kWidth) < 1e-3f; }
    f32 input_margin(const EffectEval& e) const noexcept override { return e.f(kRelief) > 0.0f ? 4.0f : 0.0f; }
    bool demo_values(EffectInstance&, std::vector<ParamValue>& v) const noexcept override {
        v[kCenter] = ParamValue::scalar(35.0f);
        v[kWidth] = ParamValue::scalar(22.0f);
        v[kAngle] = ParamValue::scalar(70.0f);
        v[kRelief] = ParamValue::scalar(45.0f);
        return true;
    }
    Status build(EffectBuildContext& ctx, const EffectEval& e, const LayerImage& input, f32,
                 LayerImage& out) const override {
        EffectUniforms u = base_uniforms(input);
        // O centro vive em -1..2; o shader projeta sobre o eixo do ângulo, e o
        // que entra é a posição normalizada nesse eixo.
        u.p0 = Vec4{e.f(kCenter) / 100.0f + 0.5f, e.f(kWidth) / 100.0f, e.f(kIntensity), e.f(kSoftness) / 100.0f * 0.5f};
        u.p1 = Vec4{e.f(kAngle), e.f(kRelief) / 100.0f, e.b(kMultiply) ? 1.0f : 0.0f, e.b(kFollowImage) ? 1.0f : 0.0f};
        u.color = e.color(kColor);
        return single_pass(ctx, ShaderId::effects_light_sweep_frag, input, u, "faixa-de-luz", out);
    }
};

// -----------------------------------------------------------------------------
// Desfoque de lente
// -----------------------------------------------------------------------------
class LensBlur final : public Effect {
public:
    enum : u32 { kRadius = 0, kHighlightBoost, kIrisSides, kIrisRotation, kQuality,
                 kIrisSharpness, kOnlyBlur, kMix };

    const EffectInfo& info() const noexcept override {
        static const EffectInfo i{effect_keys::kLensBlur, "Desfoque de lente", "Desfoque", EffectClass::Neighborhood};
        return i;
    }
    void declare_parameters(ParameterRegistry& p) const override {
        p.add_float("radius", "Raio", 18.0f, 0.0f, 400.0f, kParamAnimatable | kParamPixels, "px");
        p.add_float("highlight_boost", "Ganho das luzes", 60.0f, 0.0f, 300.0f, kParamAnimatable | kParamPercent, "%");
        p.add_int("iris_sides", "Lados da íris", 0, 0, 9);
        p.add_angle("iris_rotation", "Rotação da íris", 0.0f);
        p.add_float("quality", "Qualidade", 2.0f, 1.0f, 6.0f);
        p.add_float("iris_sharpness", "Suavidade", 100.0f, 10.0f, 100.0f, kParamAnimatable | kParamPercent, "%");
        p.add_bool("only_blur", "Só o desfoque", false);
        p.add_float("mix", "Mistura", 100.0f, 0.0f, 100.0f, kParamAnimatable | kParamPercent, "%");
    }
    bool is_identity(const EffectEval& e) const noexcept override { return e.f(kRadius) < 0.5f || e.f(kMix) < 0.01f; }
    f32 input_margin(const EffectEval& e) const noexcept override { return e.f(kRadius); }
    void pipelines(std::vector<PipelineKey>& out, SurfaceFormat work) const override {
        out.push_back(PipelineKey::fullscreen(ShaderId::effects_lens_blur_frag, work));
    }
    bool demo_values(EffectInstance&, std::vector<ParamValue>& v) const noexcept override {
        v[kRadius] = ParamValue::scalar(22.0f);
        v[kIrisSides] = ParamValue::scalar(6.0f);
        return true;
    }
    Status build(EffectBuildContext& ctx, const EffectEval& e, const LayerImage& input, f32 margin,
                 LayerImage& out) const override {
        const f32 radius = std::max(0.0f, e.f(kRadius));
        // Um disco de raio r na escala 1/k é um disco de raio r/k: reduzir não
        // muda o desenho e prende o custo por pixel.
        const u32 k = reduction_for(radius, 20.0f);
        const Rect region = spread_region(input.region, radius, radius, e.placement, margin);
        u32 w = 0, h = 0;
        ctx.region_size(region, input.texel_scale_x() / static_cast<f32>(k), w, h);

        EffectUniforms u;
        u.uvMap = EffectBuildContext::uv_map(region, input.region);
        const f32 texelScaleX = (w > 0 && region.w > 0.0f) ? static_cast<f32>(w) / region.w : 1.0f;
        const f32 texelScaleY = (h > 0 && region.h > 0.0f) ? static_cast<f32>(h) / region.h : 1.0f;
        u.texel = Vec4{w > 0 ? 1.0f / static_cast<f32>(w) : 0.0f, h > 0 ? 1.0f / static_cast<f32>(h) : 0.0f,
                       texelScaleX, texelScaleY};
        // O raio é medido em TEXELS da textura reduzida: por isso a divisão.
        u.p0 = Vec4{radius * texelScaleX / static_cast<f32>(k), e.f(kHighlightBoost) / 100.0f,
                    static_cast<f32>(e.e(kIrisSides)), e.f(kIrisSharpness) / 100.0f};
        // Preview adaptativo (8E): a qualidade (anéis do disco) cai junto com
        // `effect_quality`; o export e a prévia do catálogo usam 1 (intocados).
        const f32 quality = std::clamp(e.f(kQuality) * ctx.resources().effect_quality(), 1.0f, 6.0f);
        u.p1 = Vec4{e.f(kIrisRotation), std::round(6.0f + quality * 3.0f), std::round(quality),
                    e.f(kMix) / 100.0f};
        u.p3 = Vec4{e.b(kOnlyBlur) ? 1.0f : 0.0f, 0.0f, 0.0f, 0.0f};

        out = LayerImage{ctx.texture("desfoque-lente", w, h), region, w, h};
        if (ctx.fullscreen_pass("desfoque-lente", PassStage::Effects, out.texture, ShaderId::effects_lens_blur_frag,
                                {PassTexture{input.texture, {}, CommonSampler::LinearBorder}},
                                &u, sizeof(u)) == kInvalidIndex) {
            return Errc::PipelineCompileFailed;
        }
        return OkStatus;
    }
};

// -----------------------------------------------------------------------------
// Colorama
// -----------------------------------------------------------------------------
class Colorama final : public Effect {
public:
    enum : u32 { kPhase = 0, kCycles, kSaturation, kBrightness, kInput, kMix, kInvert,
                 kChroma, kOffset, kGain, kTint };

    const EffectInfo& info() const noexcept override {
        // Por pixel, mas com passe próprio (arco-íris mapeado), sem ColorOp:
        // como PerPixel o EffectGraph o tirava do plano e ele não desenhava.
        static const EffectInfo i{effect_keys::kColorama, "Colorama", "Cor", EffectClass::Neighborhood};
        return i;
    }
    void declare_parameters(ParameterRegistry& p) const override {
        static const char* const kInputs[] = {"Luminância", "Matiz", "Vermelho", "Verde", "Azul"};
        p.add_float("phase", "Fase", 0.0f, -10.0f, 10.0f, kParamAnimatable, "voltas");
        p.add_float("cycles", "Ciclos", 1.0f, 0.05f, 20.0f, kParamAnimatable);
        p.add_float("saturation", "Saturação", 100.0f, 0.0f, 200.0f, kParamAnimatable | kParamPercent, "%");
        p.add_float("brightness", "Brilho", 100.0f, 0.0f, 200.0f, kParamAnimatable | kParamPercent, "%");
        p.add_enum("input", "Entrada", kInputs, 5, 0);
        p.add_float("mix", "Mistura", 100.0f, 0.0f, 100.0f, kParamAnimatable | kParamPercent, "%");
        p.add_bool("invert", "Inverter o arco-íris", false);
        p.add_float("chroma", "Peso do croma", 100.0f, 0.0f, 100.0f, kParamAnimatable | kParamPercent, "%");
        p.add_float("offset", "Deslocamento", 0.0f, -1.0f, 1.0f, kParamAnimatable);
        p.add_float("gain", "Ganho", 1.0f, 0.05f, 5.0f, kParamAnimatable);
        p.add_color("tint", "Tingir", Vec4{1, 1, 1, 1});
    }
    bool is_identity(const EffectEval& e) const noexcept override { return e.f(kMix) < 0.01f; }
    bool demo_values(EffectInstance&, std::vector<ParamValue>& v) const noexcept override {
        v[kCycles] = ParamValue::scalar(1.6f);
        v[kSaturation] = ParamValue::scalar(130.0f);
        return true;
    }
    Status build(EffectBuildContext& ctx, const EffectEval& e, const LayerImage& input, f32,
                 LayerImage& out) const override {
        EffectUniforms u = base_uniforms(input);
        u.p0 = Vec4{e.f(kPhase), e.f(kCycles), e.f(kSaturation) / 100.0f, e.f(kBrightness) / 100.0f};
        u.p1 = Vec4{static_cast<f32>(e.e(kInput)), e.f(kMix) / 100.0f, e.b(kInvert) ? 1.0f : 0.0f,
                    e.f(kChroma) / 100.0f};
        u.p2 = Vec4{e.f(kOffset), e.f(kGain), 0.0f, 0.0f};
        u.color = e.color(kTint);
        return single_pass(ctx, ShaderId::effects_colorama_frag, input, u, "colorama", out);
    }
};

} // namespace

void register_light_effects(EffectRegistry& r) {
    (void)r.add(std::make_unique<DeepGlow>());
    (void)r.add(std::make_unique<Halation>());
    (void)r.add(std::make_unique<Rays>());
    (void)r.add(std::make_unique<LightSweep>());
    (void)r.add(std::make_unique<LensBlur>());
    (void)r.add(std::make_unique<Colorama>());
}

} // namespace aurea::builtin
