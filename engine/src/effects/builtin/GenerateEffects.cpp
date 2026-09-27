// =============================================================================
//  Geradores — Ruído fractal · Degradê · Degradê de 4 cores · Espectro de áudio.
//
//  Um gerador DESENHA sobre a camada em vez de transformá-la: o que sai é a
//  cor gerada, misturada com a cor da camada pelo modo escolhido e recortada
//  pela transparência dela (o ruído preenche as letras de um texto, não a
//  caixa do texto). "Preencher a caixa toda" ignora a transparência.
//
//  Todo ponto é medido no plano da camada, em px da resolução cheia — a prévia
//  reduzida e o export desenham o mesmo degradê no mesmo lugar. O ruído usa
//  um hash inteiro (sem sin()): a GPU do celular avalia sin() em precisão
//  reduzida e o ruído mostra uma grade.
//
//  Os shaders de gerador leem UM bloco a mais que os outros efeitos
//  (GeneratorUniforms): quatro cores e quatro pontos não cabem em p0..p3.
// =============================================================================
#include "BuiltinEffects.hpp"

#include <algorithm>
#include <cmath>

namespace aurea::builtin {
namespace {

f32 finite_or(f32 v, f32 fallback) noexcept { return std::isfinite(v) ? v : fallback; }

Vec2 layer_size(const EffectEval& e, const LayerImage& input) noexcept {
    if (e.placement && e.placement->layerWidth && e.placement->layerHeight) {
        return Vec2{static_cast<f32>(e.placement->layerWidth), static_cast<f32>(e.placement->layerHeight)};
    }
    return Vec2{input.region.w, input.region.h};
}

Vec4 region_vec(const Rect& r) noexcept { return Vec4{r.x, r.y, r.w, r.h}; }

/// Ponto relativo (0..1 da caixa natural) → px da camada.
Vec2 layer_point(const EffectEval& e, const LayerImage& input, Vec2 rel, Vec2 fallback) noexcept {
    const Vec2 size = layer_size(e, input);
    return Vec2{finite_or(rel.x, fallback.x) * size.x, finite_or(rel.y, fallback.y) * size.y};
}

/// O bloco de uniforms dos geradores: o comum mais quatro vec4.
struct GeneratorUniforms {
    EffectUniforms base{};
    Vec4 q0{};
    Vec4 q1{};
    Vec4 q2{};
    Vec4 q3{};
};
static_assert(sizeof(GeneratorUniforms) == 176, "layout std140 dos uniforms de gerador");

/// Modos de mistura do gerador com a camada (batem com `aurea_blend_generated`).
const char* const kBlendModes[] = {"Normal", "Multiplicar", "Tela", "Sobrepor", "Adicionar"};

Status generator_pass(EffectBuildContext& ctx, ShaderId frag, const LayerImage& input,
                      const GeneratorUniforms& u, const char* name, LayerImage& out) {
    out = input;
    out.texture = ctx.texture(name, input.width, input.height);
    if (ctx.fullscreen_pass(name, PassStage::Effects, out.texture, frag,
                            {PassTexture{input.texture, {}, CommonSampler::LinearClamp}},
                            &u, sizeof(u)) == kInvalidIndex) {
        return Errc::PipelineCompileFailed;
    }
    return OkStatus;
}

// -----------------------------------------------------------------------------
// Ruído fractal
// -----------------------------------------------------------------------------
class FractalNoise final : public Effect {
public:
    enum : u32 { kNoiseType = 0, kFractalType, kContrast, kBrightness, kInvert, kOverflow, kScale,
                 kComplexity, kEvolution, kOffset, kRotation, kSeed, kSubInfluence, kOpacity, kBlend, kFillBox };

    const EffectInfo& info() const noexcept override {
        static const EffectInfo i{effect_keys::kFractalNoise, "Ruído fractal", "Gerar", EffectClass::Neighborhood};
        return i;
    }
    void declare_parameters(ParameterRegistry& p) const override {
        static const char* const kNoises[] = {"Blocos", "Linear", "Suave", "Perlin", "Simplex"};
        static const char* const kFractals[] = {"fBm", "Turbulência", "Cristas"};
        static const char* const kOverflows[] = {"Recortar", "Suave", "Envolver"};
        p.add_enum("noise_type", "Tipo de ruído", kNoises, 5, 2);
        p.add_enum("fractal_type", "Tipo de fractal", kFractals, 3, 0);
        // Contraste e brilho giram em volta do cinza médio: 400% já leva o
        // campo a preto e branco puros; digitado vai além para quem quer.
        p.add_float("contrast", "Contraste", 100.0f, 0.0f, 400.0f, kParamAnimatable | kParamPercent, "%");
        p.typed_range(0.0f, 1000.0f);
        p.add_float("brightness", "Brilho", 0.0f, -100.0f, 100.0f, kParamAnimatable | kParamPercent, "%");
        p.typed_range(-400.0f, 400.0f);
        p.add_bool("invert", "Inverter", false);
        p.add_enum("overflow", "Estouro", kOverflows, 3, 0);
        // Tamanho da célula da primeira oitava (px da camada): 8 amostras por
        // oitava qualquer que seja o tamanho, a faixa digitada vai longe.
        p.add_float("scale", "Escala", 200.0f, 4.0f, 2000.0f, kParamAnimatable | kParamPixels, "px");
        p.typed_range(1.0f, 20000.0f);
        // Oitavas: o custo por pixel — fica no slider.
        p.add_float("complexity", "Complexidade", 4.0f, 1.0f, 8.0f, kParamAnimatable, "oitavas");
        // Uma volta = um campo inteiramente novo; keyframear é o "evoluir".
        p.add_angle("evolution", "Evolução", 0.0f);
        p.add_point2("offset", "Deslocamento", Vec2{0.0f, 0.0f}, -2000.0f, 2000.0f, kParamAnimatable | kParamPixels);
        p.typed_range(-100000.0f, 100000.0f);
        p.add_angle("rotation", "Rotação", 0.0f);
        p.add_int("seed", "Semente", 1, 0, 9999);
        p.add_float("sub_influence", "Influência das oitavas", 50.0f, 0.0f, 100.0f, kParamAnimatable | kParamPercent, "%");
        p.add_float("opacity", "Opacidade", 100.0f, 0.0f, 100.0f, kParamAnimatable | kParamPercent, "%");
        p.add_enum("blend", "Mistura com a camada", kBlendModes, 5, 0);
        p.add_bool("fill_box", "Preencher a caixa toda", false);
    }
    bool demo_values(EffectInstance&, std::vector<ParamValue>& v) const noexcept override {
        v[kContrast] = ParamValue::scalar(180.0f);
        v[kScale] = ParamValue::scalar(90.0f);
        return true;
    }
    bool is_identity(const EffectEval& e) const noexcept override {
        return !(finite_or(e.f(kOpacity), 0.0f) > 0.01f);
    }
    void pipelines(std::vector<PipelineKey>& out, SurfaceFormat work) const override {
        out.push_back(PipelineKey::fullscreen(ShaderId::effects_fractal_noise_frag, work));
    }
    Status build(EffectBuildContext& ctx, const EffectEval& e, const LayerImage& input, f32,
                 LayerImage& out) const override {
        const Vec2 size = layer_size(e, input);
        const Vec2 off = e.p2(kOffset);
        const f32 rot = finite_or(e.f(kRotation), 0.0f) * kDeg2Rad;
        // Gira em volta do CENTRO da camada: q = R·(p − C) + C − deslocamento
        // = R·p + T, com T calculado aqui.
        const f32 c = std::cos(rot), s = std::sin(rot);
        const Vec2 center{size.x * 0.5f, size.y * 0.5f};
        const Vec2 t{center.x - (c * center.x - s * center.y) - finite_or(off.x, 0.0f),
                     center.y - (s * center.x + c * center.y) - finite_or(off.y, 0.0f)};

        GeneratorUniforms u;
        u.base = base_uniforms(input);
        u.base.p0 = Vec4{static_cast<f32>(e.e(kNoiseType)), static_cast<f32>(e.e(kFractalType)),
                         std::clamp(finite_or(e.f(kContrast), 100.0f) / 100.0f, 0.0f, 10.0f),
                         std::clamp(finite_or(e.f(kBrightness), 0.0f) / 100.0f, -4.0f, 4.0f)};
        u.base.p1 = Vec4{std::clamp(finite_or(e.f(kScale), 200.0f), 1.0f, 20000.0f),
                         std::clamp(finite_or(e.f(kComplexity), 4.0f), 1.0f, 8.0f),
                         finite_or(e.f(kEvolution), 0.0f) / 360.0f,
                         static_cast<f32>(std::clamp(e.value(kSeed).as_int(), 0, 9999))};
        u.base.p2 = region_vec(input.region);
        u.base.p3 = Vec4{t.x, t.y, rot, static_cast<f32>(e.e(kOverflow))};
        u.q0 = Vec4{std::clamp(finite_or(e.f(kOpacity), 100.0f) / 100.0f, 0.0f, 1.0f),
                    static_cast<f32>(e.e(kBlend)), e.b(kInvert) ? 1.0f : 0.0f, e.b(kFillBox) ? 1.0f : 0.0f};
        u.q1 = Vec4{std::clamp(finite_or(e.f(kSubInfluence), 50.0f) / 100.0f, 0.0f, 1.0f), 0.0f, 0.0f, 0.0f};
        return generator_pass(ctx, ShaderId::effects_fractal_noise_frag, input, u, "ruido-fractal", out);
    }
};

// -----------------------------------------------------------------------------
// Degradê (linear ou radial entre dois pontos)
// -----------------------------------------------------------------------------
class GradientRamp final : public Effect {
public:
    enum : u32 { kShape = 0, kStart, kEnd, kStartColor, kEndColor, kScatter, kBlendOriginal, kBlend, kFillBox };

    const EffectInfo& info() const noexcept override {
        static const EffectInfo i{effect_keys::kGradientRamp, "Degradê", "Gerar", EffectClass::Neighborhood};
        return i;
    }
    void declare_parameters(ParameterRegistry& p) const override {
        static const char* const kShapes[] = {"Linear", "Radial"};
        p.add_enum("shape", "Forma", kShapes, 2, 0);
        p.add_point2("start", "Ponto inicial", Vec2{0.5f, 0.0f}, -1.0f, 2.0f, kParamAnimatable | kParamRelative);
        p.typed_range(-10.0f, 10.0f);
        p.add_point2("end", "Ponto final", Vec2{0.5f, 1.0f}, -1.0f, 2.0f, kParamAnimatable | kParamRelative);
        p.typed_range(-10.0f, 10.0f);
        p.add_color("start_color", "Cor inicial", Vec4{0.0f, 0.0f, 0.0f, 1.0f});
        p.add_color("end_color", "Cor final", Vec4{1.0f, 1.0f, 1.0f, 1.0f});
        // Dispersão: ruído por pixel na posição ao longo da rampa — quebra a
        // banda de cor de um degradê longo.
        p.add_float("scatter", "Dispersão", 0.0f, 0.0f, 200.0f, kParamAnimatable | kParamPixels, "px");
        p.typed_range(0.0f, 2000.0f);
        p.add_float("blend_original", "Mistura com o original", 0.0f, 0.0f, 100.0f, kParamAnimatable | kParamPercent, "%");
        p.add_enum("blend", "Mistura com a camada", kBlendModes, 5, 0);
        p.add_bool("fill_box", "Preencher a caixa toda", false);
    }
    bool demo_values(EffectInstance&, std::vector<ParamValue>& v) const noexcept override {
        v[kStartColor] = ParamValue::color(0.02f, 0.10f, 0.60f, 1.0f);
        v[kEndColor] = ParamValue::color(1.0f, 0.45f, 0.05f, 1.0f);
        return true;
    }
    bool is_identity(const EffectEval& e) const noexcept override {
        return finite_or(e.f(kBlendOriginal), 0.0f) >= 99.99f;
    }
    void pipelines(std::vector<PipelineKey>& out, SurfaceFormat work) const override {
        out.push_back(PipelineKey::fullscreen(ShaderId::effects_gradient_ramp_frag, work));
    }
    Status build(EffectBuildContext& ctx, const EffectEval& e, const LayerImage& input, f32,
                 LayerImage& out) const override {
        const Vec2 a = layer_point(e, input, e.p2(kStart), Vec2{0.5f, 0.0f});
        const Vec2 b = layer_point(e, input, e.p2(kEnd), Vec2{0.5f, 1.0f});
        GeneratorUniforms u;
        u.base = base_uniforms(input);
        u.base.p0 = Vec4{a.x, a.y, b.x, b.y};
        u.base.p1 = Vec4{static_cast<f32>(e.e(kShape)),
                         std::clamp(finite_or(e.f(kScatter), 0.0f), 0.0f, 2000.0f),
                         std::clamp(finite_or(e.f(kBlendOriginal), 0.0f) / 100.0f, 0.0f, 1.0f),
                         static_cast<f32>(e.e(kBlend))};
        u.base.p2 = region_vec(input.region);
        u.base.color = e.color(kStartColor);
        u.q0 = e.color(kEndColor);
        u.q1 = Vec4{e.b(kFillBox) ? 1.0f : 0.0f, 0.0f, 0.0f, 0.0f};
        return generator_pass(ctx, ShaderId::effects_gradient_ramp_frag, input, u, "degrade", out);
    }
};

// -----------------------------------------------------------------------------
// Degradê de 4 cores
// -----------------------------------------------------------------------------
class FourColorGradient final : public Effect {
public:
    enum : u32 { kPoint1 = 0, kPoint2, kPoint3, kPoint4, kColor1, kColor2, kColor3, kColor4,
                 kSoftness, kJitter, kOpacity, kBlend, kFillBox };

    const EffectInfo& info() const noexcept override {
        static const EffectInfo i{effect_keys::kFourColorGradient, "Degradê de 4 cores", "Gerar", EffectClass::Neighborhood};
        return i;
    }
    void declare_parameters(ParameterRegistry& p) const override {
        static const Vec2 kCorners[4] = {{0.1f, 0.1f}, {0.9f, 0.1f}, {0.1f, 0.9f}, {0.9f, 0.9f}};
        static const char* const kPointIds[4] = {"point_1", "point_2", "point_3", "point_4"};
        static const char* const kPointLabels[4] = {"Ponto 1", "Ponto 2", "Ponto 3", "Ponto 4"};
        for (u32 i = 0; i < 4; ++i) {
            p.add_point2(kPointIds[i], kPointLabels[i], kCorners[i], -1.0f, 2.0f, kParamAnimatable | kParamRelative);
            p.typed_range(-10.0f, 10.0f);
        }
        p.add_color("color_1", "Cor 1", Vec4{1.0f, 0.05f, 0.05f, 1.0f});
        p.add_color("color_2", "Cor 2", Vec4{1.0f, 0.70f, 0.00f, 1.0f});
        p.add_color("color_3", "Cor 3", Vec4{0.0f, 0.60f, 0.15f, 1.0f});
        p.add_color("color_4", "Cor 4", Vec4{0.05f, 0.20f, 1.00f, 1.0f});
        // Suavidade = quão longe cada cor alcança: 0 é quatro regiões quase
        // duras em volta dos pontos; 100 é uma mistura larga.
        p.add_float("softness", "Suavidade", 50.0f, 0.0f, 100.0f, kParamAnimatable | kParamPercent, "%");
        p.add_float("jitter", "Ruído", 0.0f, 0.0f, 100.0f, kParamAnimatable | kParamPercent, "%");
        p.add_float("opacity", "Opacidade", 100.0f, 0.0f, 100.0f, kParamAnimatable | kParamPercent, "%");
        p.add_enum("blend", "Mistura com a camada", kBlendModes, 5, 0);
        p.add_bool("fill_box", "Preencher a caixa toda", false);
    }
    bool is_identity(const EffectEval& e) const noexcept override {
        return !(finite_or(e.f(kOpacity), 0.0f) > 0.01f);
    }
    void pipelines(std::vector<PipelineKey>& out, SurfaceFormat work) const override {
        out.push_back(PipelineKey::fullscreen(ShaderId::effects_four_color_gradient_frag, work));
    }
    Status build(EffectBuildContext& ctx, const EffectEval& e, const LayerImage& input, f32,
                 LayerImage& out) const override {
        static const Vec2 kCorners[4] = {{0.1f, 0.1f}, {0.9f, 0.1f}, {0.1f, 0.9f}, {0.9f, 0.9f}};
        Vec2 pt[4];
        for (u32 i = 0; i < 4; ++i) pt[i] = layer_point(e, input, e.p2(kPoint1 + i), kCorners[i]);
        const f32 softness = std::clamp(finite_or(e.f(kSoftness), 50.0f) / 100.0f, 0.0f, 1.0f);
        GeneratorUniforms u;
        u.base = base_uniforms(input);
        u.base.p0 = Vec4{pt[0].x, pt[0].y, pt[1].x, pt[1].y};
        u.base.p1 = Vec4{pt[2].x, pt[2].y, pt[3].x, pt[3].y};
        u.base.p2 = region_vec(input.region);
        // Expoente do inverso da distância: 4 (duro) → 0,5 (largo).
        u.base.p3 = Vec4{0.5f + (1.0f - softness) * 3.5f,
                         std::clamp(finite_or(e.f(kJitter), 0.0f) / 100.0f, 0.0f, 1.0f),
                         std::clamp(finite_or(e.f(kOpacity), 100.0f) / 100.0f, 0.0f, 1.0f),
                         static_cast<f32>(e.e(kBlend))};
        u.base.color = e.color(kColor1);
        u.q0 = e.color(kColor2);
        u.q1 = e.color(kColor3);
        u.q2 = e.color(kColor4);
        u.q3 = Vec4{e.b(kFillBox) ? 1.0f : 0.0f, 0.0f, 0.0f, 0.0f};
        return generator_pass(ctx, ShaderId::effects_four_color_gradient_frag, input, u, "degrade-4-cores", out);
    }
};

// -----------------------------------------------------------------------------
// Espectro de áudio
//
// O som vem do renderer (EffectResources::audio_spectrum), resolvido no
// planejamento e guardado no eval: uma textura `faixas`×1 com a magnitude de
// cada faixa neste quadro. O shader só desenha — barras, linha ou pontos ao
// longo de um caminho (reta entre dois pontos, ou círculo).
// -----------------------------------------------------------------------------
class AudioSpectrum final : public Effect {
public:
    enum : u32 { kSource = 0, kBands, kStart, kEnd, kHeight, kThickness, kSoftness, kInsideColor,
                 kOutsideColor, kHueInterp, kDisplay, kSide, kPolar, kComposite, kGain };

    const EffectInfo& info() const noexcept override {
        static const EffectInfo i{effect_keys::kAudioSpectrum, "Espectro de áudio", "Gerar", EffectClass::Neighborhood};
        return i;
    }
    void declare_parameters(ParameterRegistry& p) const override {
        static const char* const kSources[] = {"Automático", "Esta camada", "Primeira com som"};
        static const char* const kDisplays[] = {"Barras", "Linhas", "Pontos"};
        static const char* const kSides[] = {"Lado A", "Lado B", "Os dois lados"};
        p.add_enum("source", "Fonte do som", kSources, 3, 0);
        p.add_int("bands", "Faixas", 32, 1, static_cast<i32>(kAudioSpectrumMaxBands));
        p.add_point2("start", "Ponto inicial", Vec2{0.1f, 0.5f}, -1.0f, 2.0f, kParamAnimatable | kParamRelative);
        p.typed_range(-10.0f, 10.0f);
        p.add_point2("end", "Ponto final", Vec2{0.9f, 0.5f}, -1.0f, 2.0f, kParamAnimatable | kParamRelative);
        p.typed_range(-10.0f, 10.0f);
        p.add_float("height", "Altura máxima", 200.0f, 0.0f, 2000.0f, kParamAnimatable | kParamPixels, "px");
        p.typed_range(0.0f, 20000.0f);
        p.add_float("thickness", "Espessura", 6.0f, 0.0f, 200.0f, kParamAnimatable | kParamPixels, "px");
        p.typed_range(0.0f, 2000.0f);
        p.add_float("softness", "Suavidade", 20.0f, 0.0f, 100.0f, kParamAnimatable | kParamPercent, "%");
        p.add_color("inside_color", "Cor de dentro", Vec4{0.10f, 0.90f, 1.00f, 1.0f});
        p.add_color("outside_color", "Cor de fora", Vec4{1.00f, 0.10f, 0.60f, 1.0f});
        p.add_angle("hue_interp", "Giro de matiz", 0.0f);
        p.add_enum("display", "Exibição", kDisplays, 3, 0);
        p.add_enum("side", "Lado", kSides, 3, 0);
        p.add_bool("polar", "Em círculo", false);
        p.add_bool("composite", "Compor sobre o original", false);
        p.add_float("gain", "Sensibilidade", 100.0f, 0.0f, 400.0f, kParamAnimatable | kParamPercent, "%");
        p.typed_range(0.0f, 1000.0f);
    }
    bool demo_values(EffectInstance&, std::vector<ParamValue>& v) const noexcept override {
        // A prévia do catálogo não tem som: em linhas, o repouso é a linha do
        // caminho sobre a foto — dá para ver ONDE o espectro vai desenhar.
        v[kDisplay] = ParamValue::scalar(1.0f);
        v[kThickness] = ParamValue::scalar(8.0f);
        v[kComposite] = ParamValue::boolean(true);
        return true;
    }
    void pipelines(std::vector<PipelineKey>& out, SurfaceFormat work) const override {
        out.push_back(PipelineKey::fullscreen(ShaderId::effects_audio_spectrum_frag, work));
    }
    void resolve_resources(EffectEval& e) const noexcept override {
        e.aux = TextureHandle{};
        e.auxInfo = Vec4{0.0f, 0.0f, 0.0f, 0.0f};
        if (!e.resources) return;
        AudioSpectrumRequest req;
        req.host = e.layer;
        req.source = static_cast<AudioSpectrumSource>(std::min<u32>(e.e(kSource), 2u));
        req.bands = static_cast<u32>(std::clamp(e.value(kBands).as_int(), 1, static_cast<i32>(kAudioSpectrumMaxBands)));
        req.gain = std::clamp(finite_or(e.f(kGain), 100.0f) / 100.0f, 0.0f, 10.0f);
        if (req.gain <= 0.0f) return;   // sensibilidade 0: o desenho repousa
        e.aux = e.resources->audio_spectrum(req);
        e.auxInfo.x = e.aux.valid() ? 1.0f : 0.0f;
    }
    Status build(EffectBuildContext& ctx, const EffectEval& e, const LayerImage& input, f32,
                 LayerImage& out) const override {
        const Vec2 a = layer_point(e, input, e.p2(kStart), Vec2{0.1f, 0.5f});
        const Vec2 b = layer_point(e, input, e.p2(kEnd), Vec2{0.9f, 0.5f});
        GeneratorUniforms u;
        u.base = base_uniforms(input);
        u.base.p0 = Vec4{a.x, a.y, b.x, b.y};
        u.base.p1 = Vec4{std::clamp(finite_or(e.f(kHeight), 200.0f), 0.0f, 20000.0f),
                         std::clamp(finite_or(e.f(kThickness), 6.0f), 0.0f, 2000.0f),
                         std::clamp(finite_or(e.f(kSoftness), 20.0f) / 100.0f, 0.0f, 1.0f),
                         static_cast<f32>(std::clamp(e.value(kBands).as_int(), 1, static_cast<i32>(kAudioSpectrumMaxBands)))};
        u.base.p2 = region_vec(input.region);
        u.base.p3 = Vec4{static_cast<f32>(e.e(kDisplay)), static_cast<f32>(e.e(kSide)),
                         e.b(kPolar) ? 1.0f : 0.0f, e.b(kComposite) ? 1.0f : 0.0f};
        u.base.color = e.color(kInsideColor);
        u.q0 = e.color(kOutsideColor);
        u.q1 = Vec4{finite_or(e.f(kHueInterp), 0.0f) / 360.0f, e.aux.valid() ? 1.0f : 0.0f, 0.0f, 0.0f};

        out = input;
        out.texture = ctx.texture("espectro-audio", input.width, input.height);
        // Sem espectro (sem som, sem GPU), o slot recebe a própria entrada e o
        // shader ignora: o passe existe do mesmo jeito, o desenho repousa.
        const PassTexture spectrum = e.aux.valid() ? PassTexture{{}, e.aux, CommonSampler::NearestClamp}
                                                   : PassTexture{input.texture, {}, CommonSampler::NearestClamp};
        if (ctx.fullscreen_pass("espectro-audio", PassStage::Effects, out.texture, ShaderId::effects_audio_spectrum_frag,
                                {PassTexture{input.texture, {}, CommonSampler::LinearClamp}, spectrum},
                                &u, sizeof(u)) == kInvalidIndex) {
            return Errc::PipelineCompileFailed;
        }
        return OkStatus;
    }
};

} // namespace

void register_generate_effects(EffectRegistry& r) {
    (void)r.add(std::make_unique<FractalNoise>());
    (void)r.add(std::make_unique<GradientRamp>());
    (void)r.add(std::make_unique<FourColorGradient>());
    (void)r.add(std::make_unique<AudioSpectrum>());
}

} // namespace aurea::builtin
