// =============================================================================
//  Glow — brilho que vaza das áreas claras.
//
//  Três etapas, todas no FrameGraph:
//    1. `bright_pass`: separa o que passa do limiar (joelho suave), já em
//       meia resolução;
//    2. o mesmo gaussiano em pirâmide do Gaussian Blur (reaproveitado, não
//       duplicado);
//    3. `glow_combine`: soma a luz borrada à imagem, em linear.
//
//  A região cresce pelo raio (o halo passa da caixa da layer) e é recortada ao
//  que o quadro mostra.
//
//  Instância nova (algoritmo 1): brilho em oitavas — limiar suave no valor
//  visto, seis oitavas somadas, mapa de tom e "tela" no espaço codificado
//  (`build_octave_glow`, também usado pelo Brilho profundo). O desenho acima
//  fica para as instâncias salvas antes dele (slot "algorithm" ausente = 0).
// =============================================================================
#include "BuiltinEffects.hpp"

#include <array>
#include <algorithm>
#include <cmath>

namespace aurea::builtin {
namespace {

f32 srgb_to_linear(f32 c) noexcept {
    return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
}

class Glow final : public Effect {
public:
    // Índices salvos nos projetos: só se acrescenta no fim.
    enum : u32 { kThreshold = 0, kRadius, kIntensity, kColor, kAlgorithm, kSoftness, kFalloff,
                 kTintAmount, kChromatic, kAddMode, kGlowOnly,
                 // AE 2026 (no fim; só o brilho em oitavas lê):
                 kBasedOn, kComposite, kGlowColors, kColorA, kColorB, kLooping, kLoops, kPhase, kMidpoint,
                 // AE 2026, depois (no fim): Glow Dimensions.
                 kDimensions };

    const EffectInfo& info() const noexcept override {
        static const EffectInfo i{effect_keys::kGlow, "Brilho", "Luz", EffectClass::Neighborhood};
        return i;
    }
    void declare_parameters(ParameterRegistry& p) const override {
        // Padrões do brilho em oitavas (instância nova). Os quatro primeiros
        // slots existem em todo projeto salvo: mudar o padrão não toca neles.
        p.add_float("threshold", "Limiar", 40.0f, 0.0f, 100.0f, kParamAnimatable | kParamPercent, "%");
        p.add_float("radius", "Raio", 55.0f, 0.0f, 500.0f, kParamAnimatable | kParamPixels, "px");
        p.typed_range(0.0f, 3000.0f);   // o mesmo teto da pirâmide do Gaussian Blur
        p.add_float("intensity", "Intensidade", 1.4f, 0.0f, 10.0f);
        p.typed_range(0.0f, 100.0f);    // ganho da soma: custo fixo
        p.add_color("color", "Cor", Vec4{1, 1, 1, 1});
        // Algoritmo (oculto): 1 = brilho em oitavas; 0 = o desenho anterior,
        // que um projeto salvo sem este slot (até a build 2143) mantém.
        p.add_float("algorithm", "Algoritmo", 1.0f, 0.0f, 1.0f, kParamHidden | kParamLegacyZero);
        p.add_float("softness", "Suavidade", 40.0f, 0.0f, 100.0f, kParamAnimatable | kParamPercent, "%");
        p.add_float("falloff", "Decaimento", 15.0f, 0.0f, 100.0f, kParamAnimatable | kParamPercent, "%");
        p.add_float("tint_amount", "Quantidade da cor", 100.0f, 0.0f, 100.0f, kParamAnimatable | kParamPercent, "%");
        p.add_float("chromatic", "Aberração cromática", 0.0f, 0.0f, 100.0f, kParamAnimatable | kParamPercent, "%");
        p.add_bool("add_mode", "Modo somar", false);
        p.add_bool("glow_only", "Só o brilho", false);
        // AE 2026: Glow Based On, Composite Original e Glow Colors A & B (padrões = antes).
        static const char* const kBasedOn[] = {"Valores de cor", "Canal alfa"};
        p.add_enum("glow_based_on", "Brilho baseado em", kBasedOn, 2, 0);
        static const char* const kComposites[] = {"Na frente", "Atrás", "Nenhum"};
        p.add_enum("composite_original", "Compor o original", kComposites, 3, 0);
        static const char* const kColorModes[] = {"Cor única", "Cores A e B"};
        p.add_enum("glow_colors", "Cores do brilho", kColorModes, 2, 0);
        p.add_color("color_a", "Cor A", Vec4{1.0f, 1.0f, 1.0f, 1.0f});
        p.add_color("color_b", "Cor B", Vec4{0.0f, 0.0f, 0.0f, 1.0f});
        static const char* const kLoopings[] = {"Dente de serra A>B", "Dente de serra B>A", "Triângulo A>B>A",
                                                "Triângulo B>A>B"};
        p.add_enum("color_looping", "Repetição das cores", kLoopings, 4, 2);
        p.add_float("color_loops", "Repetições das cores", 1.0f, 1.0f, 10.0f, kParamAnimatable);
        p.add_angle("color_phase", "Fase das cores", 0.0f);
        p.add_float("ab_midpoint", "Ponto médio A e B", 50.0f, 0.0f, 100.0f, kParamAnimatable | kParamPercent, "%");
        for (u32 k : {static_cast<u32>(kColorA), static_cast<u32>(kColorB), static_cast<u32>(kLooping),
                      static_cast<u32>(kLoops), static_cast<u32>(kPhase), static_cast<u32>(kMidpoint)})
            p.set_enabled_by(k, kGlowColors, 2u);
        // AE 2026 (no fim): Glow Dimensions. Padrão = os dois eixos (o de sempre).
        static const char* const kDims[] = {"Horizontal e vertical", "Horizontal", "Vertical"};
        p.add_enum("glow_dimensions", "Dimensões do brilho", kDims, 3, 0);
    }
    bool demo_values(EffectInstance&, std::vector<ParamValue>& v) const noexcept override {
        v[kThreshold] = ParamValue::scalar(70.0f);    // só o clarão entra
        v[kRadius] = ParamValue::scalar(70.0f);
        v[kIntensity] = ParamValue::scalar(2.2f);
        return true;
    }
    void pipelines(std::vector<PipelineKey>& out, SurfaceFormat work) const override {
        out.push_back(PipelineKey::fullscreen(ShaderId::effects_bright_pass_frag, work));
        out.push_back(PipelineKey::fullscreen(ShaderId::effects_glow_combine_frag, work));
        octave_glow_pipelines(out, work);
    }
    static bool octaves(const EffectEval& e) noexcept { return e.f(kAlgorithm) >= 0.5f; }
    bool is_identity(const EffectEval& e) const noexcept override {
        if (octaves(e)) return e.f(kIntensity) < 1e-4f && !e.b(kGlowOnly);
        return e.f(kIntensity) < 1e-4f;
    }
    f32 input_margin(const EffectEval& e) const noexcept override {
        if (octaves(e)) return octave_glow_reach(e.f(kRadius));
        return std::max(0.0f, e.f(kRadius));
    }

    Status build(EffectBuildContext& ctx, const EffectEval& e, const LayerImage& input, f32 margin,
                 LayerImage& out) const override {
        if (octaves(e)) {
            OctaveGlow g;
            g.threshold = e.f(kThreshold) / 100.0f;
            g.softness = e.f(kSoftness) / 100.0f;
            g.radius = e.f(kRadius);
            g.falloff = e.f(kFalloff) / 100.0f;
            g.exposure = e.f(kIntensity);
            g.color = e.color(kColor);
            g.tintAmount = e.f(kTintAmount) / 100.0f;
            g.chromatic = e.f(kChromatic) / 100.0f;
            g.addMode = e.b(kAddMode);
            g.glowOnly = e.b(kGlowOnly);
            if (e.count > kMidpoint) {
                g.basedOnAlpha = e.e(kBasedOn) == 1u;
                g.composite = std::min<u32>(e.e(kComposite), 2u);
                g.abColors = e.e(kGlowColors) == 1u;
                g.colorA = e.color(kColorA);
                g.colorB = e.color(kColorB);
                g.looping = std::min<u32>(e.e(kLooping), 3u);
                g.loops = std::isfinite(e.f(kLoops)) ? std::clamp(e.f(kLoops), 1.0f, 100.0f) : 1.0f;
                g.phase = std::isfinite(e.f(kPhase)) ? e.f(kPhase) / 360.0f : 0.0f;
                g.midpoint = std::isfinite(e.f(kMidpoint)) ? std::clamp(e.f(kMidpoint) / 100.0f, 0.01f, 0.99f) : 0.5f;
            }
            if (e.count > kDimensions) g.dimensions = std::min<u32>(e.e(kDimensions), 2u);
            return build_octave_glow(ctx, g, e.placement, input, margin, out);
        }
        // Algoritmo 0 (projetos salvos até a build 2143): inalterado.
        const f32 radius = std::max(0.0f, e.f(kRadius));
        const u32 dims = e.count > kDimensions ? std::min<u32>(e.e(kDimensions), 2u) : 0u;
        const Rect region = spread_region(input.region, dims == 2 ? 0.0f : radius, dims == 1 ? 0.0f : radius, e.placement, margin);
        const f32 k = input.texel_scale_x();

        // 1. Brilho em meia resolução.
        u32 bw = 0, bh = 0;
        ctx.region_size(region, k * 0.5f, bw, bh);
        const FGTexture bright = ctx.texture("glow-brilho", bw, bh);
        const f32 t = srgb_to_linear(std::clamp(e.f(kThreshold) / 100.0f, 0.0f, 1.0f));
        const f32 knee = std::max(1e-4f, t * 0.5f);
        struct {
            Vec4 uvMap;
            Vec4 texel;
            Vec4 knee;
        } ub{};
        ub.uvMap = EffectBuildContext::uv_map(region, input.region);
        ub.texel = Vec4{1.0f / static_cast<f32>(input.width), 1.0f / static_cast<f32>(input.height), 0, 0};
        ub.knee = Vec4{t, knee, 1.0f / (4.0f * knee), 0.0f};
        if (ctx.fullscreen_pass("glow-brilho", PassStage::Effects, bright, ShaderId::effects_bright_pass_frag,
                                {PassTexture{input.texture, {}, CommonSampler::LinearBorder}},
                                &ub, sizeof(ub)) == kInvalidIndex) {
            return Errc::PipelineCompileFailed;
        }

        // 2. Borra o brilho (mesmo construtor do Gaussian Blur).
        LayerImage brightImage{bright, region, bw, bh};
        LayerImage blurred = brightImage;
        if (radius > 0.01f) {
            BlurRequest req;
            req.sigmaX = dims == 2 ? 0.0f : radius / 3.0f;
            req.sigmaY = dims == 1 ? 0.0f : radius / 3.0f;
            req.repeatEdges = false;
            req.outRegion = region;
            req.label = "glow";
            if (const Status s = build_gaussian(ctx, brightImage, req, blurred); !s.ok()) return s;
        }

        // 3. Soma à original, na densidade da entrada.
        u32 ow = 0, oh = 0;
        ctx.region_size(region, k, ow, oh);
        const FGTexture result = ctx.texture("glow", ow, oh);
        const Vec4 color = e.color(kColor);
        struct {
            Vec4 uvMapSrc;
            Vec4 uvMapGlow;
            Vec4 tint;
        } uc{};
        uc.uvMapSrc = EffectBuildContext::uv_map(region, input.region);
        uc.uvMapGlow = EffectBuildContext::uv_map(region, blurred.region);
        uc.tint = Vec4{color.x, color.y, color.z, e.f(kIntensity)};
        if (ctx.fullscreen_pass("glow-soma", PassStage::Effects, result, ShaderId::effects_glow_combine_frag,
                                {PassTexture{input.texture, {}, CommonSampler::LinearBorder},
                                 PassTexture{blurred.texture, {}, CommonSampler::LinearClamp}},
                                &uc, sizeof(uc)) == kInvalidIndex) {
            return Errc::PipelineCompileFailed;
        }
        out = LayerImage{result, region, ow, oh};
        return OkStatus;
    }
};

} // namespace

// -----------------------------------------------------------------------------
// Brilho em oitavas (algoritmo 1 do Brilho e do Brilho profundo)
//
// Pirâmide de seis oitavas: a primeira é a imagem clara em meia densidade, cada
// seguinte a metade da anterior (filtro de 13 amostras). Na volta, cada oitava
// recebe as menores ampliadas (tenda 3x3) com um peso que o raio "anda"
// continuamente pela pirâmide — raio pequeno, só as oitavas finas; raio
// grande, todas — e que o decaimento reduz a cada oitava. As oitavas NÃO são
// normalizadas: somadas, passam do branco perto da fonte, e é o mapa de tom
// da composição que as acomoda.
//
// As oitavas são medidas em pixels da LAYER (a oitava i tem texel de 2^(i+1)
// px): o preview em densidade reduzida começa mais acima na pirâmide e dobra
// as oitavas finas que não tem na primeira, para o brilho valer o mesmo que no
// export.
// -----------------------------------------------------------------------------
namespace {
constexpr u32 kOctaves = 6;

f32 octave_walk(f32 radius) noexcept {
    // Raio (px) -> quantas oitavas acendem (0..6). 4,5 px = só a primeira;
    // 288 px = todas.
    return std::clamp(std::log2(std::max(radius, 1e-3f) / 4.5f), 0.0f, static_cast<f32>(kOctaves));
}
} // namespace

f32 octave_glow_reach(f32 radius) noexcept {
    const f32 walk = octave_walk(radius);
    const f32 coarsest = std::min(static_cast<f32>(kOctaves - 1), std::ceil(walk));
    return 8.0f * std::exp2(coarsest) + 4.0f;
}

void octave_glow_pipelines(std::vector<PipelineKey>& out, SurfaceFormat work) {
    out.push_back(PipelineKey::fullscreen(ShaderId::effects_glow_octave_prefilter_frag, work));
    out.push_back(PipelineKey::fullscreen(ShaderId::effects_glow_octave_down_frag, work));
    out.push_back(PipelineKey::fullscreen(ShaderId::effects_glow_octave_up_frag, work));
    out.push_back(PipelineKey::fullscreen(ShaderId::effects_glow_octave_composite_frag, work));
}

Status build_octave_glow(EffectBuildContext& ctx, const OctaveGlow& g, const LayerPlacement* placement,
                         const LayerImage& input, f32 margin, LayerImage& out) {
    const f32 reach = octave_glow_reach(g.radius);
    // AE 2026 (Glow Dimensions): 1 só horizontal, 2 só vertical. O eixo
    // parado não cresce, não reduz na pirâmide e não tem taps (texel 0).
    const u32 dims = std::min<u32>(g.dimensions, 2u);
    const bool spreadX = dims != 2, spreadY = dims != 1;
    const Rect region = spread_region(input.region, spreadX ? reach : 0.0f, spreadY ? reach : 0.0f, placement, margin);
    const f32 k = std::max(input.texel_scale_x(), 1e-4f);

    // Densidade da primeira oitava: meia, nunca mais fina que a oitava 0.
    const f32 d0 = std::min(k, 1.0f) * 0.5f;
    const f32 first = std::max(0.0f, -std::log2(std::min(k, 1.0f)));   // índice da oitava 0 desta pirâmide
    const f32 walk = octave_walk(g.radius);
    const f32 keep = 1.0f + (0.35f - 1.0f) * std::clamp(g.falloff, 0.0f, 1.0f);
    auto weight = [&](f32 octave) { return std::clamp(walk - octave, 0.0f, 1.0f) * keep; };

    // Oitavas finas que a densidade do preview não tem: dobradas na primeira.
    f32 own0 = 1.0f, chain0 = 1.0f;
    for (u32 i = 0; static_cast<f32>(i) + 0.5f < first; ++i) { chain0 *= weight(static_cast<f32>(i)); own0 += chain0; }
    chain0 *= weight(std::floor(first + 0.5f));

    // Quantas oitavas a pirâmide precisa: até a mais larga com peso.
    u32 levels = 1;
    while (levels < kOctaves && first + static_cast<f32>(levels) < static_cast<f32>(kOctaves) &&
           weight(first + static_cast<f32>(levels) - 1.0f) > 0.0f) {
        ++levels;
    }

    struct Level { FGTexture tex; u32 w = 0, h = 0; };
    // Array fixo: o caminho quente do quadro não aloca (Perf8C.SteadyPlayback...).
    std::array<Level, kOctaves> pyr{};
    const Vec4 same = EffectBuildContext::uv_map(region, region);

    // 1. Imagem clara, já na primeira oitava.
    ctx.region_size(region, d0, pyr[0].w, pyr[0].h);
    pyr[0].w = std::max(1u, pyr[0].w); pyr[0].h = std::max(1u, pyr[0].h);
    pyr[0].tex = ctx.texture("brilho-oitava-0", pyr[0].w, pyr[0].h);
    {
        EffectUniforms u;
        u.uvMap = EffectBuildContext::uv_map(region, input.region);
        u.texel = Vec4{1.0f / static_cast<f32>(std::max(1u, input.width)), 1.0f / static_cast<f32>(std::max(1u, input.height)), 0, 0};
        u.p0 = Vec4{std::clamp(g.threshold, 0.0f, 1.0f), std::clamp(g.softness, 0.0f, 1.0f) * 0.5f,
                    g.basedOnAlpha ? 1.0f : 0.0f, 0};
        if (ctx.fullscreen_pass("brilho-oitava-limiar", PassStage::Effects, pyr[0].tex,
                                ShaderId::effects_glow_octave_prefilter_frag,
                                {PassTexture{input.texture, {}, CommonSampler::LinearBorder}},
                                &u, sizeof(u)) == kInvalidIndex) {
            return Errc::PipelineCompileFailed;
        }
    }
    // 2. Descida.
    for (u32 j = 1; j < levels; ++j) {
        ctx.region_size(region, d0 / static_cast<f32>(1u << j), pyr[j].w, pyr[j].h);
        if (!spreadX) pyr[j].w = pyr[0].w;
        if (!spreadY) pyr[j].h = pyr[0].h;
        pyr[j].w = std::max(1u, pyr[j].w); pyr[j].h = std::max(1u, pyr[j].h);
        pyr[j].tex = ctx.texture("brilho-oitava", pyr[j].w, pyr[j].h);
        EffectUniforms u;
        u.uvMap = same;
        u.texel = Vec4{spreadX ? 1.0f / static_cast<f32>(pyr[j - 1].w) : 0.0f,
                       spreadY ? 1.0f / static_cast<f32>(pyr[j - 1].h) : 0.0f, 0, 0};
        if (ctx.fullscreen_pass("brilho-oitava-desce", PassStage::Effects, pyr[j].tex,
                                ShaderId::effects_glow_octave_down_frag,
                                {PassTexture{pyr[j - 1].tex, {}, CommonSampler::LinearBorder}},
                                &u, sizeof(u)) == kInvalidIndex) {
            return Errc::PipelineCompileFailed;
        }
    }
    // 3. Subida: cada oitava soma as menores com o peso dela.
    FGTexture acc = pyr[levels - 1].tex;
    for (u32 j = levels - 1; j-- > 0;) {
        const f32 w = j == 0 ? chain0 : weight(first + static_cast<f32>(j));
        const FGTexture next = ctx.texture("brilho-oitava-soma", pyr[j].w, pyr[j].h);
        EffectUniforms u;
        u.uvMap = same;
        u.texel = Vec4{spreadX ? 1.0f / static_cast<f32>(pyr[j + 1].w) : 0.0f,
                       spreadY ? 1.0f / static_cast<f32>(pyr[j + 1].h) : 0.0f, 0, 0};
        u.p0 = Vec4{w, j == 0 ? own0 : 1.0f, 0, 0};
        if (ctx.fullscreen_pass("brilho-oitava-sobe", PassStage::Effects, next,
                                ShaderId::effects_glow_octave_up_frag,
                                {PassTexture{acc, {}, CommonSampler::LinearBorder},
                                 PassTexture{pyr[j].tex, {}, CommonSampler::LinearClamp}},
                                &u, sizeof(u)) == kInvalidIndex) {
            return Errc::PipelineCompileFailed;
        }
        acc = next;
    }

    // 4. Composição na densidade da entrada.
    u32 ow = 0, oh = 0;
    ctx.region_size(region, input.texel_scale_x(), ow, oh);
    out = LayerImage{ctx.texture("brilho-oitavas", ow, oh), region, ow, oh};
    // AE 2026: o bloco ganhou a cor B no fim (compor o original e cores A e B
    // em p1.zw, p2, p3); com os padrões o shader faz a conta de sempre.
    struct {
        EffectUniforms base;
        Vec4 colorB;
    } cu{};
    EffectUniforms& u = cu.base;
    u.uvMap = EffectBuildContext::uv_map(region, input.region);
    u.texel = same;
    u.p0 = Vec4{std::max(0.0f, g.exposure), std::clamp(g.tintAmount, 0.0f, 1.0f),
                std::clamp(g.chromatic, 0.0f, 1.0f), g.addMode ? 1.0f : 0.0f};
    u.p1 = Vec4{g.glowOnly ? 1.0f : 0.0f, levels == 1 ? own0 : 1.0f, static_cast<f32>(std::min<u32>(g.composite, 2u)),
                g.abColors ? 1.0f : 0.0f};
    u.p2 = Vec4{g.loops, g.phase, g.midpoint, static_cast<f32>(std::min<u32>(g.looping, 3u))};
    u.p3 = g.colorA;
    cu.colorB = g.colorB;
    u.color = g.color;
    if (ctx.fullscreen_pass("brilho-oitavas", PassStage::Effects, out.texture,
                            ShaderId::effects_glow_octave_composite_frag,
                            {PassTexture{input.texture, {}, CommonSampler::LinearBorder},
                             PassTexture{acc, {}, CommonSampler::LinearClamp}},
                            &cu, sizeof(cu)) == kInvalidIndex) {
        return Errc::PipelineCompileFailed;
    }
    return OkStatus;
}

void register_glow_effect(EffectRegistry& r) { (void)r.add(std::make_unique<Glow>()); }

} // namespace aurea::builtin
