// =============================================================================
//  Efeitos de estilização (Fase 7.3 §22, §33, §34, §42, §45, §46, §47).
//
//  Inverter · Varredura de tela · Grão · Meio-tom · Minimax · Máscara de
//  nitidez · Ordenar pixels · Dano de filme · Dano de JPEG · HoloMatrix
//
//  Cada um é uma conta sobre a vizinhança; o miolo é o shader e aqui só ficam
//  a declaração dos parâmetros, a identidade e o preenchimento dos uniforms.
//  Os que precisam de mais de um passe (nitidez, dano de JPEG) montam os
//  passes com as peças que o motor já tem.
// =============================================================================
#include "BuiltinEffects.hpp"

#include <algorithm>
#include <cmath>

namespace aurea::builtin {
namespace {

f32 finite_or(f32 v, f32 fallback) noexcept { return std::isfinite(v) ? v : fallback; }

/// Quanto reduzir a imagem para que um núcleo de `radius` pixels caiba no
/// limite do shader (6 px de raio). Um morfológico/desfoque de raio r na
/// escala 1/k é o mesmo filtro de raio r/k — reduzir não muda o resultado,
/// só o custo.
u32 scale_for_radius(f32 radius, f32 limit) noexcept {
    if (!(radius > limit) || limit <= 0.0f) return 1;
    u32 k = 1;
    while (radius / static_cast<f32>(k) > limit && k < 8) ++k;
    return k;
}

// -----------------------------------------------------------------------------
// Inverter — o negativo. É uma operação de cor, então entra no passe fundido
// (nenhum passe próprio) e sai de graça junto com os vizinhos.
// -----------------------------------------------------------------------------
class Invert final : public Effect {
public:
    enum : u32 { kColor = 0, kAlpha };

    const EffectInfo& info() const noexcept override {
        static const EffectInfo i{effect_keys::kInvert, "Inverter", "Cor", EffectClass::PerPixel};
        return i;
    }
    void declare_parameters(ParameterRegistry& p) const override {
        p.add_bool("invert_color", "Inverter as cores", true);
        p.add_bool("invert_alpha", "Inverter a transparência", false);
    }
    bool is_identity(const EffectEval& e) const noexcept override { return !e.b(kColor) && !e.b(kAlpha); }
    bool color_op(const EffectEval& e, ColorOp& op) const noexcept override {
        op.code = ColorOpCode::Invert;
        op.p[1] = e.b(kColor) ? 1.0f : 0.0f;
        op.p[2] = e.b(kAlpha) ? 1.0f : 0.0f;
        return true;
    }
    bool demo_values(EffectInstance&, std::vector<ParamValue>& v) const noexcept override {
        v[kColor] = ParamValue::boolean(true);
        return true;
    }
};

// -----------------------------------------------------------------------------
// Varredura de tela
// -----------------------------------------------------------------------------
class Scanlines final : public Effect {
public:
    enum : u32 { kHeight = 0, kIntensity, kOffset, kSoftness, kContrast, kSpeed, kChannel, kBlur, kRoll, kColor };

    const EffectInfo& info() const noexcept override {
        // Passe próprio (e desfoque vertical opcional): NÃO é ColorOp do passe
        // fundido. Como PerPixel o EffectGraph o tirava do plano como
        // identidade — o efeito não desenhava no projeto (Fase 8A, custo por efeito).
        static const EffectInfo i{effect_keys::kScanlines, "Varredura", "Estilizar", EffectClass::Neighborhood};
        return i;
    }
    void declare_parameters(ParameterRegistry& p) const override {
        static const char* const kChannels[] = {"Todas", "Vermelho", "Verde", "Azul"};
        p.add_float("height", "Altura da linha", 3.0f, 1.0f, 40.0f, kParamAnimatable | kParamPixels, "px");
        p.add_float("intensity", "Intensidade", 55.0f, 0.0f, 100.0f, kParamAnimatable | kParamPercent, "%");
        p.add_float("offset", "Deslocamento", 0.0f, -100.0f, 100.0f, kParamAnimatable | kParamPixels, "px");
        p.add_float("softness", "Suavidade", 45.0f, 1.0f, 100.0f, kParamAnimatable | kParamPercent, "%");
        p.add_float("contrast", "Contraste", 0.0f, -100.0f, 100.0f, kParamAnimatable | kParamPercent, "%");
        p.add_float("speed", "Velocidade", 0.0f, -20.0f, 20.0f, kParamAnimatable, "px/quadro");
        p.add_enum("channel", "Canal", kChannels, 4, 0);
        p.add_float("blur", "Desfoque vertical", 0.0f, 0.0f, 20.0f, kParamAnimatable | kParamPixels, "px");
        p.add_float("roll", "Rolagem", 0.0f, -100.0f, 100.0f, kParamAnimatable | kParamPixels, "px");
        p.add_color("color", "Cor da varredura", Vec4{0.10f, 0.10f, 0.13f, 1.0f});
    }
    bool is_identity(const EffectEval& e) const noexcept override { return e.f(kIntensity) < 0.01f; }
    Status build(EffectBuildContext& ctx, const EffectEval& e, const LayerImage& input, f32,
                 LayerImage& out) const override {
        EffectUniforms u = base_uniforms(input);
        u.p0 = Vec4{e.f(kHeight), e.f(kIntensity) / 100.0f, e.f(kOffset), e.f(kSoftness) / 100.0f * 4.0f};
        // Contraste no valor codificado: -1..1 vira -1..1 (o shader soma 1).
        u.p1 = Vec4{e.f(kContrast) / 100.0f, e.f(kSpeed) * static_cast<f32>(e.localTime.value),
                    static_cast<f32>(e.e(kChannel)), 0.0f};
        u.p2 = Vec4{e.f(kBlur), e.f(kRoll), 0.0f, 0.0f};
        u.p3 = Vec4{static_cast<f32>(e.localTime.value), 0, 0, 0};
        u.color = e.color(kColor);
        return single_pass(ctx, ShaderId::effects_scanlines_frag, input, u, "varredura", out);
    }
    bool demo_values(EffectInstance&, std::vector<ParamValue>& v) const noexcept override {
        v[kHeight] = ParamValue::scalar(4.0f);
        v[kIntensity] = ParamValue::scalar(65.0f);
        v[kSoftness] = ParamValue::scalar(55.0f);
        return true;
    }
};

// -----------------------------------------------------------------------------
// Grão
// -----------------------------------------------------------------------------
class Grain final : public Effect {
public:
    enum : u32 { kIntensity = 0, kSize, kColorGrain, kRoughness, kShadows, kHighlights, kSeed, kAnimated,
                 kMono, kBlend };

    const EffectInfo& info() const noexcept override {
        // Por pixel, mas com passe próprio (ruído procedural), sem ColorOp:
        // como PerPixel saía do plano e não desenhava (ver Varredura).
        static const EffectInfo i{effect_keys::kGrain, "Grão", "Estilizar", EffectClass::Neighborhood};
        return i;
    }
    void declare_parameters(ParameterRegistry& p) const override {
        p.add_float("intensity", "Intensidade", 0.0f, 0.0f, 100.0f, kParamAnimatable | kParamPercent, "%");
        p.add_float("grain_size", "Tamanho do grão", 1.5f, 0.5f, 12.0f, kParamAnimatable | kParamPixels, "px");
        p.add_float("color_grain", "Grão de cor", 50.0f, 0.0f, 100.0f, kParamAnimatable | kParamPercent, "%");
        p.add_float("roughness", "Rugosidade", 1.0f, 0.2f, 3.0f);
        p.add_float("shadows", "Sombras", 60.0f, 0.0f, 100.0f, kParamAnimatable | kParamPercent, "%");
        p.add_float("highlights", "Luzes", 70.0f, 0.0f, 100.0f, kParamAnimatable | kParamPercent, "%");
        p.add_int("seed", "Semente", 7, 0, 9999);
        p.add_bool("animated", "Grão animado", true);
        p.add_bool("monochrome", "Monocromático", false);
    }
    bool is_identity(const EffectEval& e) const noexcept override { return e.f(kIntensity) < 0.01f; }
    bool demo_values(EffectInstance&, std::vector<ParamValue>& v) const noexcept override {
        v[kIntensity] = ParamValue::scalar(28.0f);
        v[kSize] = ParamValue::scalar(2.0f);
        return true;
    }
    Status build(EffectBuildContext& ctx, const EffectEval& e, const LayerImage& input, f32,
                 LayerImage& out) const override {
        EffectUniforms u = base_uniforms(input);
        u.p0 = Vec4{e.f(kIntensity) / 100.0f * 0.35f, e.f(kSize), e.f(kColorGrain) / 100.0f, e.f(kRoughness)};
        u.p1 = Vec4{e.f(kShadows) / 100.0f, e.f(kHighlights) / 100.0f,
                    static_cast<f32>(e.e(kSeed)), e.b(kAnimated) ? 1.0f : 0.0f};
        u.p2 = Vec4{e.b(kMono) ? 1.0f : 0.0f, 0.0f, 0.0f, 0.0f};
        u.p3 = Vec4{static_cast<f32>(e.localTime.value), 0, 0, 0};
        return single_pass(ctx, ShaderId::effects_grain_frag, input, u, "grao", out);
    }
};

// -----------------------------------------------------------------------------
// Meio-tom
// -----------------------------------------------------------------------------
class Halftone final : public Effect {
public:
    enum : u32 { kCell = 0, kContrast, kAngle, kSoftness, kChannelAngle, kPattern, kSeparate, kPaper, kGain, kColor };

    const EffectInfo& info() const noexcept override {
        static const EffectInfo i{effect_keys::kHalftone, "Meio-tom", "Estilizar", EffectClass::Neighborhood};
        return i;
    }
    void declare_parameters(ParameterRegistry& p) const override {
        static const char* const kPatterns[] = {"Ponto", "Linha", "Losango"};
        p.add_float("cell", "Tamanho do ponto", 8.0f, 2.0f, 60.0f, kParamAnimatable | kParamPixels, "px");
        p.add_float("contrast", "Contraste", 30.0f, -100.0f, 200.0f, kParamAnimatable | kParamPercent, "%");
        p.add_angle("angle", "Ângulo da grade", 15.0f);
        p.add_float("softness", "Suavidade da borda", 30.0f, 1.0f, 100.0f, kParamAnimatable | kParamPercent, "%");
        p.add_angle("channel_angle", "Rotação por canal", 30.0f);
        p.add_enum("pattern", "Padrão", kPatterns, 3, 0);
        p.add_bool("separate_channels", "Grades separadas por canal", true);
        p.add_float("paper", "Fundo claro", 0.0f, 0.0f, 100.0f, kParamAnimatable | kParamPercent, "%");
        p.add_float("dot_gain", "Ganho do ponto", 50.0f, 0.0f, 100.0f, kParamAnimatable | kParamPercent, "%");
        p.add_color("tint", "Cor do papel", Vec4{1, 1, 1, 1});
    }
    bool is_identity(const EffectEval&) const noexcept override { return false; }
    f32 input_margin(const EffectEval& e) const noexcept override { return std::max(2.0f, e.f(kCell)); }
    bool demo_values(EffectInstance&, std::vector<ParamValue>& v) const noexcept override {
        v[kCell] = ParamValue::scalar(7.0f);
        v[kContrast] = ParamValue::scalar(45.0f);
        return true;
    }
    Status build(EffectBuildContext& ctx, const EffectEval& e, const LayerImage& input, f32 margin,
                 LayerImage& out) const override {
        // O ponto lê o centro da célula, que pode cair fora da região: a
        // margem de uma célula inteira garante que a última fileira de pontos
        // não fique com a cor errada.
        const Rect region = spread_region(input.region, e.f(kCell) * 4.0f, e.f(kCell) * 4.0f, e.placement, margin);
        u32 w = 0, h = 0;
        ctx.region_size(region, input.texel_scale_x(), w, h);
        EffectUniforms u;
        u.uvMap = EffectBuildContext::uv_map(region, input.region);
        // A célula é medida em pixels da SAÍDA: a textura pode estar reduzida.
        u.texel = Vec4{region.w > 0.0f ? 1.0f / region.w : 0.0f, region.h > 0.0f ? 1.0f / region.h : 0.0f,
                       input.texel_scale_x(), input.texel_scale_y()};
        u.p0 = Vec4{e.f(kCell) * input.texel_scale_x(), e.f(kContrast) / 100.0f, e.f(kAngle), e.f(kSoftness) / 100.0f};
        u.p1 = Vec4{e.f(kChannelAngle), static_cast<f32>(e.e(kPattern)), e.b(kSeparate) ? 1.0f : 0.0f, 0.0f};
        u.p2 = Vec4{e.f(kPaper) / 100.0f, e.f(kGain) / 50.0f, 0.0f, 0.0f};
        u.color = e.color(kColor);
        out = LayerImage{ctx.texture("meio-tom", w, h), region, w, h};
        if (ctx.fullscreen_pass("meio-tom", PassStage::Effects, out.texture, ShaderId::effects_halftone_frag,
                                {PassTexture{input.texture, {}, CommonSampler::LinearBorder}},
                                &u, sizeof(u)) == kInvalidIndex) {
            return Errc::PipelineCompileFailed;
        }
        return OkStatus;
    }
};

// -----------------------------------------------------------------------------
// Minimax — dilatar / erodir / abrir / fechar
// -----------------------------------------------------------------------------
class Minimax final : public Effect {
public:
    enum : u32 { kRadius = 0, kMode, kShape, kAmount, kChannel };

    const EffectInfo& info() const noexcept override {
        static const EffectInfo i{effect_keys::kMinimax, "Minimax", "Estilizar", EffectClass::Neighborhood};
        return i;
    }
    void declare_parameters(ParameterRegistry& p) const override {
        static const char* const kModes[] = {"Dilatar", "Erodir", "Abrir", "Fechar"};
        static const char* const kShapes[] = {"Cruz", "Quadrado", "Losango"};
        static const char* const kChannels[] = {"Luz e transparência", "Transparência", "Cores", "Cores e transparência"};
        p.add_float("radius", "Raio", 4.0f, 0.0f, 120.0f, kParamAnimatable | kParamPixels, "px");
        p.add_enum("mode", "Operação", kModes, 4, 0);
        p.add_enum("shape", "Forma", kShapes, 3, 1);
        p.add_float("amount", "Intensidade", 100.0f, 0.0f, 100.0f, kParamAnimatable | kParamPercent, "%");
        p.add_enum("channel", "Comparar por", kChannels, 4, 0);
    }
    bool is_identity(const EffectEval& e) const noexcept override { return e.f(kRadius) < 0.5f || e.f(kAmount) < 0.01f; }
    f32 input_margin(const EffectEval& e) const noexcept override { return std::max(0.0f, e.f(kRadius)); }
    void pipelines(std::vector<PipelineKey>& out, SurfaceFormat work) const override {
        out.push_back(PipelineKey::fullscreen(ShaderId::effects_minimax_frag, work));
    }

    /// Um passo do morfológico: reduz a imagem pelo fator que o raio pede e
    /// roda um núcleo de no máximo 6 px.
    static Status one_pass(EffectBuildContext& ctx, const EffectEval& e, const LayerImage& input, bool dilate,
                           f32 margin, LayerImage& out) {
        const f32 radius = std::max(0.0f, e.f(kRadius));
        const u32 k = scale_for_radius(radius, 6.0f);
        const f32 scaled = radius / static_cast<f32>(k);
        const Rect region = spread_region(input.region, radius, radius, e.placement, margin);
        u32 w = 0, h = 0;
        ctx.region_size(region, input.texel_scale_x() / static_cast<f32>(k), w, h);

        EffectUniforms u;
        u.uvMap = EffectBuildContext::uv_map(region, input.region);
        u.texel = Vec4{w > 0 ? 1.0f / static_cast<f32>(w) : 0.0f, h > 0 ? 1.0f / static_cast<f32>(h) : 0.0f,
                       (w > 0 && region.w > 0.0f) ? static_cast<f32>(w) / region.w : 1.0f,
                       (h > 0 && region.h > 0.0f) ? static_cast<f32>(h) / region.h : 1.0f};
        u.p0 = Vec4{scaled, dilate ? 1.0f : 0.0f, static_cast<f32>(e.e(kShape)), e.f(kAmount) / 100.0f};
        u.p1 = Vec4{static_cast<f32>(e.e(kChannel)), 0, 0, 0};

        out = LayerImage{ctx.texture("minimax", w, h), region, w, h};
        if (ctx.fullscreen_pass("minimax", PassStage::Effects, out.texture, ShaderId::effects_minimax_frag,
                                {PassTexture{input.texture, {}, CommonSampler::LinearBorder}},
                                &u, sizeof(u)) == kInvalidIndex) {
            return Errc::PipelineCompileFailed;
        }
        return OkStatus;
    }

    Status build(EffectBuildContext& ctx, const EffectEval& e, const LayerImage& input, f32 margin,
                 LayerImage& out) const override {
        // Abrir = erodir e depois dilatar; fechar = o contrário. É a definição,
        // e sai igual a um núcleo que fizesse as duas contas de uma vez.
        const u32 mode = e.e(kMode);
        const bool firstDilate = mode == 0 || mode == 3;
        const bool secondDilate = mode == 0 || mode == 2;
        LayerImage first;
        if (const Status s = one_pass(ctx, e, input, firstDilate, margin, first); !s.ok()) return s;
        if (mode < 2) {
            out = first;
            return OkStatus;
        }
        return one_pass(ctx, e, first, secondDilate, margin, out);
    }
    bool demo_values(EffectInstance&, std::vector<ParamValue>& v) const noexcept override {
        v[kRadius] = ParamValue::scalar(9.0f);
        v[kMode] = ParamValue::scalar(1.0f);   // erodir: o clarão da cartela encolhe
        return true;
    }
};

// -----------------------------------------------------------------------------
// Máscara de nitidez
// -----------------------------------------------------------------------------
class UnsharpMask final : public Effect {
public:
    enum : u32 { kAmount = 0, kRadius, kThreshold, kOnlyDetail, kBlend };

    const EffectInfo& info() const noexcept override {
        static const EffectInfo i{effect_keys::kUnsharp, "Máscara de nitidez", "Nitidez", EffectClass::Neighborhood};
        return i;
    }
    void declare_parameters(ParameterRegistry& p) const override {
        p.add_float("amount", "Intensidade", 100.0f, 0.0f, 500.0f, kParamAnimatable | kParamPercent, "%");
        p.add_float("radius", "Raio", 2.0f, 0.1f, 100.0f, kParamAnimatable | kParamPixels, "px");
        p.add_float("threshold", "Limiar", 0.0f, 0.0f, 100.0f, kParamAnimatable | kParamPercent, "%");
        p.add_bool("only_detail", "Só o detalhe", false);
        p.add_float("blend", "Mistura", 100.0f, 0.0f, 100.0f, kParamAnimatable | kParamPercent, "%");
    }
    bool is_identity(const EffectEval& e) const noexcept override { return e.f(kAmount) < 0.01f || e.f(kBlend) < 0.01f; }
    f32 input_margin(const EffectEval& e) const noexcept override { return std::max(1.0f, e.f(kRadius) * 2.0f); }
    void pipelines(std::vector<PipelineKey>& out, SurfaceFormat work) const override {
        out.push_back(PipelineKey::fullscreen(ShaderId::effects_gaussian_blur_frag, work));
        out.push_back(PipelineKey::fullscreen(ShaderId::effects_downsample_frag, work));
        out.push_back(PipelineKey::fullscreen(ShaderId::effects_unsharp_combine_frag, work));
    }
    bool demo_values(EffectInstance&, std::vector<ParamValue>& v) const noexcept override {
        v[kAmount] = ParamValue::scalar(180.0f);
        v[kRadius] = ParamValue::scalar(1.6f);
        return true;
    }

    Status build(EffectBuildContext& ctx, const EffectEval& e, const LayerImage& input, f32 margin,
                 LayerImage& out) const override {
        const f32 radius = std::max(0.1f, e.f(kRadius));
        const Rect region = spread_region(input.region, radius * 2.0f, radius * 2.0f, e.placement, margin);

        // O borrado vem do MESMO gaussiano do Desfoque — não há um segundo
        // algoritmo de desfoque no motor.
        BlurRequest req;
        req.sigmaX = req.sigmaY = radius / 3.0f;
        req.repeatEdges = false;
        req.outRegion = region;
        req.label = "mascara-borrado";
        LayerImage blurred;
        if (const Status s = build_gaussian(ctx, input, req, blurred); !s.ok()) return s;

        u32 w = 0, h = 0;
        ctx.region_size(region, input.texel_scale_x(), w, h);
        EffectUniforms u;
        u.uvMap = EffectBuildContext::uv_map(region, input.region);
        u.texel = Vec4{w > 0 ? 1.0f / static_cast<f32>(w) : 0.0f, h > 0 ? 1.0f / static_cast<f32>(h) : 0.0f, 0, 0};
        // O limiar é fração da faixa visível (0..1 em linear).
        const f32 t = std::clamp(e.f(kThreshold) / 100.0f, 0.0f, 1.0f) * 0.5f;
        u.p0 = Vec4{e.f(kAmount) / 100.0f, t, 5.0f, e.b(kOnlyDetail) ? 1.0f : 0.0f};

        out = LayerImage{ctx.texture("mascara-nitidez", w, h), region, w, h};
        if (ctx.fullscreen_pass("mascara-nitidez", PassStage::Effects, out.texture,
                                ShaderId::effects_unsharp_combine_frag,
                                {PassTexture{input.texture, {}, CommonSampler::LinearBorder},
                                 PassTexture{blurred.texture, {}, CommonSampler::LinearClamp}},
                                &u, sizeof(u)) == kInvalidIndex) {
            return Errc::PipelineCompileFailed;
        }
        return OkStatus;
    }
};

} // namespace

void register_stylize_effects(EffectRegistry& r) {
    (void)r.add(std::make_unique<Invert>());
    (void)r.add(std::make_unique<Scanlines>());
    (void)r.add(std::make_unique<Grain>());
    (void)r.add(std::make_unique<Halftone>());
    (void)r.add(std::make_unique<Minimax>());
    (void)r.add(std::make_unique<UnsharpMask>());
}

} // namespace aurea::builtin
