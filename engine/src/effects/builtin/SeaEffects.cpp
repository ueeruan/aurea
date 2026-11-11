// =============================================================================
//  Pacote "Mar" (0.5.2): Oceano · Embaixo d'água · Cáusticas · Onda na praia
//
//  Oceano (Gerar): o mar até o horizonte — ondas de Gerstner + relevo fino de
//  ruído, reflexo do céu por Fresnel (cor em degradê, a própria camada ou
//  outra camada), brilho do sol, espuma nas cristas, névoa ao longe e um
//  modo estilizado (faixas chapadas). Serve de camada de fundo.
//  Embaixo d'água (Distorcer): ondulação + cáusticas + absorção/névoa azul
//  pela profundidade + raios de luz de cima + partículas em suspensão.
//  Cáusticas (Luz): a rede de luz focada pela água, projetada na camada.
//  Onda na praia (Transição): uma onda com espuma atravessa a camada e a
//  revela (ou leva embora).
//
//  O resto do mar já existia e continua onde estava: Reflexo na água
//  (WaterWrapBallpointEffects.cpp), Ondulação radial (OpticalEffects.cpp,
//  que ganhou velocidade e brilho no fim da lista), Garoa/Drizzle
//  (CCStylizeEffects.cpp), Onda (DistortEffects.cpp). O navegador junta tudo
//  no grupo "Mar" (EffectPickerLogic.kt / EffectsBrowser.swift).
//
//  Comportamento especificado por referências públicas (descrições e vídeos
//  de geradores de oceano, do "Caustics" e de transições de onda); a
//  matemática e os shaders (sea_*.frag, sea_common.glsl) são nossos. Cada
//  efeito é UM passe de tela cheia com custo fixo por pixel; a prévia
//  adaptativa corta ondas/oitavas/redes pela `effect_quality()`, o export usa
//  tudo. Tudo é função pura do tempo do quadro e dos parâmetros: o export é
//  a prévia. Os índices de parâmetro ficam fixos nos projetos (acrescentar só
//  no fim).
// =============================================================================
#include "CCCommon.hpp"

#include <algorithm>
#include <cmath>
#include <memory>

namespace aurea::builtin {
namespace {
using namespace cc;

constexpr const char* kOceanKey = "aurea.generate.ocean";
constexpr const char* kUnderwaterKey = "aurea.distort.underwater";
constexpr const char* kCausticsKey = "aurea.light.caustics";
constexpr const char* kShoreWaveKey = "aurea.transition.shore_wave";

/// Espelho do bloco de sea_common.glsl: o dos efeitos CC + q0..q5.
struct SeaUniforms {
    Uniforms base;
    Vec4 q[6];
};
static_assert(sizeof(SeaUniforms) == 272, "layout std140 de sea_common.glsl");

f32 quality(EffectBuildContext& ctx) noexcept {
    return std::clamp(finite_or(ctx.resources().effect_quality(), 1.0f), 0.25f, 1.0f);
}

Vec4 linear_rgb(Vec4 c) noexcept {
    const Vec4 l = premultiplied_linear(Vec4{c.x, c.y, c.z, 1.0f});
    return Vec4{l.x, l.y, l.z, 1.0f};
}

/// Um passe de `frag` da entrada para `region` (densidade da entrada). O
/// slot 1 recebe `extra` (ou a própria entrada, para o layout ficar fixo).
Status sea_pass(EffectBuildContext& ctx, const EffectEval& e, ShaderId frag, const LayerImage& in, const Rect& region,
                SeaUniforms& u, const char* name, LayerImage& out, f32 q,
                CommonSampler sampler = CommonSampler::LinearBorder, const LayerImage* extra = nullptr) {
    u32 w = 0, h = 0;
    ctx.region_size(region, in.texel_scale_x(), w, h);
    if (!w || !h) { out = in; return OkStatus; }
    u.base.dst = Vec4{region.x, region.y, region.w, region.h};
    u.base.src = Vec4{in.region.x, in.region.y, in.region.w, in.region.h};
    const Rect box = layer_box(e, in);
    u.base.box = Vec4{box.x, box.y, box.w, box.h};
    const f32 texel = 0.5f * (region.w / static_cast<f32>(w) + region.h / static_cast<f32>(h));
    u.base.info = Vec4{std::max(texel, 1e-4f), seconds(e), in.texel_scale_x(), q};
    out = LayerImage{ctx.texture(name, w, h), region, w, h};
    const u32 pass = extra
        ? ctx.fullscreen_pass(name, PassStage::Effects, out.texture, frag,
                              {PassTexture{in.texture, {}, sampler},
                               PassTexture{extra->texture, {}, CommonSampler::LinearClamp}}, &u, sizeof(u))
        : ctx.fullscreen_pass(name, PassStage::Effects, out.texture, frag,
                              {PassTexture{in.texture, {}, sampler}}, &u, sizeof(u));
    return pass == kInvalidIndex ? Status{Errc::PipelineCompileFailed} : OkStatus;
}

f32 seed_of(const EffectEval& e, u32 i) noexcept {
    return static_cast<f32>(std::clamp(e.value(i).as_int(), 0, 9999));
}

// =============================================================================
// Oceano
// =============================================================================
class Ocean final : public Effect {
public:
    enum : u32 { kHeight = 0, kLength, kWind, kSpeed, kChop, kShallow, kDeep, kSkyMode, kSkyColor, kSkyLayer,
                 kFoam, kGlitter, kSunDirection, kSunHeight, kHorizon, kCameraAngle, kHaze, kStylize, kUseAlpha,
                 kSeed, kCount };
    static_assert(kCount == 20, "índices gravados nos projetos: só se acrescenta no fim");

    const EffectInfo& info() const noexcept override {
        static const EffectInfo i{kOceanKey, "Oceano", "Gerar", EffectClass::Neighborhood};
        return i;
    }
    void declare_parameters(ParameterRegistry& p) const override {
        static const char* const kSkyModes[] = {"Cor", "Imagem da camada", "Outra camada"};
        p.add_float("wave_height", "Altura das ondas", 1.0f, 0.0f, 5.0f, kAnim, "m");
        p.typed_range(0.0f, 50.0f);
        p.add_float("wavelength", "Comprimento das ondas", 14.0f, 1.0f, 100.0f, kAnim, "m");
        p.typed_range(0.2f, 1000.0f);
        p.add_angle("wind_direction", "Direção do vento", 0.0f);
        p.add_float("speed", "Velocidade", 1.0f, 0.0f, 4.0f, kAnim);
        p.typed_range(-20.0f, 20.0f);
        p.add_float("choppiness", "Agitação", 55.0f, 0.0f, 100.0f, kAnimPct, "%");
        p.add_color("shallow_color", "Cor da água rasa", Vec4{0.12f, 0.66f, 0.66f, 1.0f});
        p.add_color("deep_color", "Cor da água funda", Vec4{0.02f, 0.16f, 0.30f, 1.0f});
        p.add_enum("sky_mode", "Céu", kSkyModes, 3, 0);
        p.add_color("sky_color", "Cor do céu", Vec4{0.50f, 0.72f, 0.95f, 1.0f});
        p.add_layer_ref("sky_layer", "Camada do céu");
        p.add_float("foam", "Espuma", 35.0f, 0.0f, 100.0f, kAnimPct, "%");
        p.add_float("sun_glitter", "Brilho do sol", 60.0f, 0.0f, 100.0f, kAnimPct, "%");
        p.typed_range(0.0f, 400.0f);
        p.add_angle("sun_direction", "Direção do sol", 15.0f);
        p.add_float("sun_height", "Altura do sol", 14.0f, 1.0f, 89.0f, kAnim, "°");
        p.add_float("horizon", "Horizonte", 40.0f, 0.0f, 100.0f, kAnimPct, "%");
        p.typed_range(-100.0f, 200.0f);
        p.add_float("camera_angle", "Ângulo da câmera", 22.0f, 2.0f, 80.0f, kAnim, "°");
        p.add_float("haze", "Névoa", 40.0f, 0.0f, 100.0f, kAnimPct, "%");
        p.add_float("stylize", "Estilizado", 0.0f, 0.0f, 100.0f, kAnimPct, "%");
        p.add_bool("use_layer_alpha", "Recortar pela camada", false);
        p.add_int("seed", "Semente", 0, 0, 9999);
        p.set_tip(kSkyMode, "Imagem da camada: o céu e o reflexo vêm desta camada. "
                            "Outra camada: escolha a camada do céu (ela aparece acima do horizonte).");
    }
    bool is_identity(const EffectEval& e) const noexcept override { return e.count < kCount; }
    bool needs_full_input() const noexcept override { return true; }
    i32 input_layer_param() const noexcept override { return kSkyLayer; }
    bool demo_values(EffectInstance&, std::vector<ParamValue>& v) const noexcept override {
        v[kHeight] = ParamValue::scalar(1.4f);
        return true;
    }
    void pipelines(std::vector<PipelineKey>& out, SurfaceFormat work) const override {
        out.push_back(PipelineKey::fullscreen(ShaderId::effects_sea_ocean_frag, work));
    }
    Status build(EffectBuildContext& ctx, const EffectEval& e, const LayerImage& in, f32 margin, LayerImage& out) const override {
        const Rect box = layer_box(e, in);
        const f32 q = quality(ctx);
        const f32 horizon = box.y + box.h * clampf(e.f(kHorizon), -100.0f, 200.0f, 40.0f) / 100.0f;
        // O foco sai do ângulo: o pé da camada olha `ângulo` graus para baixo.
        const f32 below = std::max(box.y + box.h - horizon, box.h * 0.02f);
        const f32 angle = clampf(e.f(kCameraAngle), 2.0f, 80.0f, 22.0f) * kDeg;
        const f32 focal = std::max(below / std::tan(angle), 1.0f);
        constexpr f32 kCameraHeight = 3.0f;
        const f32 wind = finite_or(e.f(kWind), 0.0f) * kDeg;
        const f32 az = finite_or(e.f(kSunDirection), 15.0f) * kDeg;
        const f32 el = clampf(e.f(kSunHeight), 1.0f, 89.0f, 14.0f) * kDeg;
        const f32 tau = std::fmod(seconds(e) * clampf(e.f(kSpeed), -20.0f, 20.0f, 1.0f), 20000.0f);

        SeaUniforms u{};
        u.base.p0 = Vec4{horizon, focal, box.x + 0.5f * box.w, kCameraHeight};
        u.base.p1 = Vec4{0.5f * clampf(e.f(kHeight), 0.0f, 50.0f, 1.0f), clampf(e.f(kLength), 0.2f, 1000.0f, 14.0f), tau,
                         clampf(e.f(kChop), 0.0f, 100.0f, 55.0f) / 100.0f};
        // Prévia leve: menos ondas e uma oitava de relevo; o export, tudo.
        const f32 waves = std::round(3.0f + 4.0f * (q - 0.25f) / 0.75f);
        u.base.p2 = Vec4{std::sin(wind), -std::cos(wind), std::clamp(waves, 3.0f, 7.0f), q >= 0.75f ? 2.0f : 1.0f};
        u.base.p3 = Vec4{std::sin(az) * std::cos(el), std::sin(el), std::cos(az) * std::cos(el),
                         clampf(e.f(kGlitter), 0.0f, 400.0f, 60.0f) / 100.0f};
        const u32 mode = std::min(e.e(kSkyMode), 2u);
        u.base.p4 = Vec4{static_cast<f32>(mode), e.b(kUseAlpha) ? 1.0f : 0.0f,
                         clampf(e.f(kHaze), 0.0f, 100.0f, 40.0f) / 100.0f,
                         clampf(e.f(kStylize), 0.0f, 100.0f, 0.0f) / 100.0f};
        u.base.color = linear_rgb(e.color(kSkyColor));
        const Vec4 shallow = linear_rgb(e.color(kShallow)), deep = linear_rgb(e.color(kDeep));
        u.q[0] = Vec4{shallow.x, shallow.y, shallow.z, clampf(e.f(kFoam), 0.0f, 100.0f, 35.0f) / 100.0f};
        u.q[1] = Vec4{deep.x, deep.y, deep.z, seed_of(e, kSeed)};

        const LayerImage* sky = nullptr;
        if (mode == 2) {
            const u64 ref = e.value(kSkyLayer).ref;
            sky = ref ? ctx.layer_input(ref) : nullptr;
            if (sky && !sky->valid()) sky = nullptr;
        }
        if (sky) {
            // px da camada → px da composição (a linha afim da camada 2D comum).
            const Mat4 m = e.placement ? e.placement->compFromLayer : Mat4::identity();
            u.q[2] = Vec4{m.col[0].x, m.col[1].x, m.col[3].x, 1.0f};
            u.q[3] = Vec4{m.col[0].y, m.col[1].y, m.col[3].y, 0.0f};
            u.base.p5 = Vec4{sky->region.x, sky->region.y, sky->region.w, sky->region.h};
        } else {
            u.base.p5 = Vec4{0.0f, 0.0f, 1.0f, 1.0f};
        }
        // O gerador enche a caixa inteira da camada (uma forma, um sólido, um texto).
        const Rect region = grown_region(e, in.region, Rect::unite(in.region, box), margin);
        // A própria camada como céu: o reflexo lê fora da imagem → borda repetida.
        const CommonSampler sampler = mode == 1 ? CommonSampler::LinearClamp : CommonSampler::LinearBorder;
        return sea_pass(ctx, e, ShaderId::effects_sea_ocean_frag, in, region, u, "oceano", out, q, sampler,
                        sky ? sky : &in);
    }
};

// =============================================================================
// Embaixo d'água
// =============================================================================
class Underwater final : public Effect {
public:
    enum : u32 { kDistortion = 0, kWaveSize, kSpeed, kCaustics, kCausticsScale, kWaterColor, kDepth, kRays,
                 kParticles, kParticleSize, kSeed, kCount };
    static_assert(kCount == 11, "índices gravados nos projetos: só se acrescenta no fim");

    const EffectInfo& info() const noexcept override {
        static const EffectInfo i{kUnderwaterKey, "Embaixo d'água", "Distorcer", EffectClass::Neighborhood};
        return i;
    }
    void declare_parameters(ParameterRegistry& p) const override {
        p.add_float("distortion", "Ondulação", 8.0f, 0.0f, 60.0f, kAnimPx, "px");
        p.typed_range(0.0f, 400.0f);
        p.add_float("wave_size", "Tamanho das ondas", 160.0f, 10.0f, 1000.0f, kAnimPx, "px");
        p.typed_range(2.0f, 10000.0f);
        p.add_float("speed", "Velocidade", 1.0f, 0.0f, 5.0f, kAnim);
        p.typed_range(-50.0f, 50.0f);
        p.add_float("caustics", "Cáusticas", 45.0f, 0.0f, 100.0f, kAnimPct, "%");
        p.typed_range(0.0f, 400.0f);
        p.add_float("caustics_scale", "Escala das cáusticas", 120.0f, 10.0f, 800.0f, kAnimPx, "px");
        p.typed_range(2.0f, 8000.0f);
        p.add_color("water_color", "Cor da água", Vec4{0.06f, 0.42f, 0.56f, 1.0f});
        p.add_float("depth", "Profundidade", 55.0f, 0.0f, 100.0f, kAnimPct, "%");
        p.add_float("light_rays", "Raios de luz", 40.0f, 0.0f, 100.0f, kAnimPct, "%");
        p.typed_range(0.0f, 300.0f);
        p.add_float("particles", "Partículas", 30.0f, 0.0f, 100.0f, kAnimPct, "%");
        p.add_float("particle_size", "Tamanho das partículas", 3.0f, 0.5f, 20.0f, kAnimPx, "px");
        p.typed_range(0.3f, 200.0f);
        p.add_int("seed", "Semente", 0, 0, 9999);
    }
    bool is_identity(const EffectEval& e) const noexcept override {
        if (e.count < kCount) return true;
        auto off = [&](u32 i) { return !(finite_or(e.f(i), 0.0f) > 0.0f); };
        return off(kDistortion) && off(kCaustics) && off(kDepth) && off(kRays) && off(kParticles);
    }
    f32 input_margin(const EffectEval& e) const noexcept override {
        return clampf(e.f(kDistortion), 0.0f, 400.0f, 8.0f) * 1.3f + 1.0f;
    }
    void pipelines(std::vector<PipelineKey>& out, SurfaceFormat work) const override {
        out.push_back(PipelineKey::fullscreen(ShaderId::effects_sea_underwater_frag, work));
    }
    Status build(EffectBuildContext& ctx, const EffectEval& e, const LayerImage& in, f32, LayerImage& out) const override {
        const f32 q = quality(ctx);
        const f32 tau = std::fmod(seconds(e) * clampf(e.f(kSpeed), -50.0f, 50.0f, 1.0f), 20000.0f);
        SeaUniforms u{};
        u.base.p0 = Vec4{clampf(e.f(kDistortion), 0.0f, 400.0f, 8.0f), clampf(e.f(kWaveSize), 2.0f, 10000.0f, 160.0f), tau,
                         clampf(e.f(kCaustics), 0.0f, 400.0f, 45.0f) / 100.0f};
        u.base.p1 = Vec4{clampf(e.f(kCausticsScale), 2.0f, 8000.0f, 120.0f), clampf(e.f(kDepth), 0.0f, 100.0f, 55.0f) / 100.0f,
                         clampf(e.f(kRays), 0.0f, 300.0f, 40.0f) / 100.0f, clampf(e.f(kParticles), 0.0f, 100.0f, 30.0f) / 100.0f};
        u.base.p2 = Vec4{clampf(e.f(kParticleSize), 0.3f, 200.0f, 3.0f), seed_of(e, kSeed), q >= 0.6f ? 2.0f : 1.0f,
                         q >= 0.6f ? 2.0f : 1.0f};
        u.base.color = linear_rgb(e.color(kWaterColor));
        return sea_pass(ctx, e, ShaderId::effects_sea_underwater_frag, in, in.region, u, "embaixo-dagua", out, q);
    }
};

// =============================================================================
// Cáusticas
// =============================================================================
class Caustics final : public Effect {
public:
    enum : u32 { kColor = 0, kScale, kSpeed, kIntensity, kSharpness, kBlend, kRefraction, kAngle, kSeed, kCount };
    static_assert(kCount == 9, "índices gravados nos projetos: só se acrescenta no fim");

    const EffectInfo& info() const noexcept override {
        static const EffectInfo i{kCausticsKey, "Cáusticas", "Luz", EffectClass::Neighborhood};
        return i;
    }
    void declare_parameters(ParameterRegistry& p) const override {
        static const char* const kModes[] = {"Adicionar", "Tela", "Sobrepor", "Multiplicar", "Só as cáusticas"};
        p.add_color("color", "Cor da luz", Vec4{0.80f, 0.96f, 1.0f, 1.0f});
        p.add_float("scale", "Escala", 120.0f, 10.0f, 800.0f, kAnimPx, "px");
        p.typed_range(2.0f, 8000.0f);
        p.add_float("speed", "Velocidade", 1.0f, 0.0f, 5.0f, kAnim);
        p.typed_range(-50.0f, 50.0f);
        p.add_float("intensity", "Intensidade", 80.0f, 0.0f, 200.0f, kAnimPct, "%");
        p.typed_range(0.0f, 1000.0f);
        p.add_float("sharpness", "Nitidez", 55.0f, 0.0f, 100.0f, kAnimPct, "%");
        p.add_enum("blend", "Modo de mistura", kModes, 5, 0);
        p.add_float("refraction", "Refração", 4.0f, 0.0f, 40.0f, kAnimPx, "px");
        p.typed_range(0.0f, 400.0f);
        p.add_angle("angle", "Ângulo", 0.0f);
        p.add_int("seed", "Semente", 0, 0, 9999);
    }
    bool is_identity(const EffectEval& e) const noexcept override {
        if (e.count < kCount) return true;
        return e.e(kBlend) != 4u && !(finite_or(e.f(kIntensity), 80.0f) > 0.0f) && !(finite_or(e.f(kRefraction), 4.0f) > 0.0f);
    }
    f32 input_margin(const EffectEval& e) const noexcept override {
        return clampf(e.f(kRefraction), 0.0f, 400.0f, 4.0f) + 1.0f;
    }
    void pipelines(std::vector<PipelineKey>& out, SurfaceFormat work) const override {
        out.push_back(PipelineKey::fullscreen(ShaderId::effects_sea_caustics_frag, work));
    }
    Status build(EffectBuildContext& ctx, const EffectEval& e, const LayerImage& in, f32, LayerImage& out) const override {
        const f32 q = quality(ctx);
        const f32 tau = std::fmod(seconds(e) * clampf(e.f(kSpeed), -50.0f, 50.0f, 1.0f), 20000.0f);
        SeaUniforms u{};
        u.base.p0 = Vec4{clampf(e.f(kScale), 2.0f, 8000.0f, 120.0f), tau, clampf(e.f(kIntensity), 0.0f, 1000.0f, 80.0f) / 100.0f,
                         clampf(e.f(kSharpness), 0.0f, 100.0f, 55.0f) / 100.0f};
        u.base.p1 = Vec4{static_cast<f32>(std::min(e.e(kBlend), 4u)), clampf(e.f(kRefraction), 0.0f, 400.0f, 4.0f),
                         finite_or(e.f(kAngle), 0.0f) * kDeg, seed_of(e, kSeed)};
        u.base.p2 = Vec4{q >= 0.6f ? 2.0f : 1.0f, 0.0f, 0.0f, 0.0f};
        u.base.color = linear_rgb(e.color(kColor));
        return sea_pass(ctx, e, ShaderId::effects_sea_caustics_frag, in, in.region, u, "causticas", out, q);
    }
};

// =============================================================================
// Onda na praia (transição)
// =============================================================================
class ShoreWave final : public Effect {
public:
    enum : u32 { kProgress = 0, kMode, kAuto, kAngle, kEdgeWave, kEdgeSize, kFoamWidth, kFoamColor, kWaterWidth,
                 kWaterColor, kRefraction, kClip, kSeed, kCount };
    static_assert(kCount == 13, "índices gravados nos projetos: só se acrescenta no fim");

    const EffectInfo& info() const noexcept override {
        static const EffectInfo i{kShoreWaveKey, "Onda na praia", "Transição", EffectClass::Domain};
        return i;
    }
    void declare_parameters(ParameterRegistry& p) const override {
        static const char* const kModes[] = {"Revelar camada", "Remover camada"};
        p.add_float("progress", "Progresso", 100.0f, 0.0f, 100.0f, kAnimPct, "%");
        p.add_enum("transition_dir", "Direção", kModes, 2, 0);
        p.add_bool("auto_transition", "Transição automática", false);
        p.add_angle("direction", "Ângulo", 0.0f);
        // Tamanhos em % da travessia (a camada inteira na direção da onda):
        // um texto de 300 px e um vídeo 4K ganham a mesma onda.
        p.add_float("edge_wave", "Ondulação da borda", 5.0f, 0.0f, 30.0f, kAnimPct, "%");
        p.typed_range(0.0f, 200.0f);
        p.add_float("edge_size", "Tamanho das ondas da borda", 30.0f, 3.0f, 100.0f, kAnimPct, "%");
        p.typed_range(1.0f, 1000.0f);
        p.add_float("foam_width", "Largura da espuma", 7.0f, 0.0f, 40.0f, kAnimPct, "%");
        p.typed_range(0.0f, 200.0f);
        p.add_color("foam_color", "Cor da espuma", Vec4{0.97f, 0.99f, 1.0f, 1.0f});
        p.add_float("water_width", "Largura da água", 22.0f, 0.0f, 100.0f, kAnimPct, "%");
        p.typed_range(0.0f, 400.0f);
        p.add_color("water_color", "Cor da água", Vec4{0.14f, 0.64f, 0.70f, 1.0f});
        p.add_float("refraction", "Distorção da água", 8.0f, 0.0f, 60.0f, kAnimPx, "px");
        p.typed_range(0.0f, 600.0f);
        p.add_bool("clip_to_layer", "Recortar pela camada", false);
        p.add_int("seed", "Semente", 0, 0, 9999);
    }
    static f32 progress(const EffectEval& e) noexcept {
        return std::clamp(e.b(kAuto) ? e.clipProgress : finite_or(e.f(kProgress), 100.0f) / 100.0f, 0.0f, 1.0f);
    }
    bool is_identity(const EffectEval& e) const noexcept override {
        if (e.count < kCount) return true;
        const f32 t = progress(e);
        return e.e(kMode) == 0 ? t >= 1.0f : t <= 0.0f;
    }
    bool demo_values(EffectInstance&, std::vector<ParamValue>& v) const noexcept override {
        v[kProgress] = ParamValue::scalar(55.0f);
        return true;
    }
    f32 input_margin(const EffectEval& e) const noexcept override {
        return clampf(e.f(kRefraction), 0.0f, 600.0f, 8.0f) * 1.5f + 1.0f;
    }
    void pipelines(std::vector<PipelineKey>& out, SurfaceFormat work) const override {
        out.push_back(PipelineKey::fullscreen(ShaderId::effects_sea_shore_wave_frag, work));
    }
    Status build(EffectBuildContext& ctx, const EffectEval& e, const LayerImage& in, f32, LayerImage& out) const override {
        const Rect box = layer_box(e, in);
        const f32 a = finite_or(e.f(kAngle), 0.0f) * kDeg;
        const Vec2 dir{std::cos(a), std::sin(a)};
        // Meia travessia da caixa na direção da onda.
        const f32 half = 0.5f * (std::fabs(dir.x) * box.w + std::fabs(dir.y) * box.h);
        const f32 span = std::max(2.0f * half, 1.0f);
        const f32 edge = span * clampf(e.f(kEdgeWave), 0.0f, 200.0f, 5.0f) / 100.0f;
        const f32 edgeSize = span * clampf(e.f(kEdgeSize), 1.0f, 1000.0f, 30.0f) / 100.0f;
        const f32 foam = span * clampf(e.f(kFoamWidth), 0.0f, 200.0f, 7.0f) / 100.0f;
        const f32 water = span * clampf(e.f(kWaterWidth), 0.0f, 400.0f, 22.0f) / 100.0f;
        // Em 0 a espuma ainda está fora; em 1 a água (e as bolhas) já saíram.
        const f32 lead = edge + 0.3f * foam + 4.0f;
        const f32 tail = edge + std::max(foam + water, 2.4f * foam) + 0.3f * foam + 4.0f;
        const f32 t = progress(e);
        const f32 front = -half - lead + t * (2.0f * half + lead + tail);
        SeaUniforms u{};
        u.base.p0 = Vec4{t, static_cast<f32>(std::min(e.e(kMode), 1u)), dir.x, dir.y};
        u.base.p1 = Vec4{edge, std::max(edgeSize, 1.0f), foam, water};
        u.base.p2 = Vec4{clampf(e.f(kRefraction), 0.0f, 600.0f, 8.0f), e.b(kClip) ? 1.0f : 0.0f, seed_of(e, kSeed), seconds(e)};
        u.base.p3 = Vec4{front, half, 0.0f, 0.0f};
        u.base.color = linear_rgb(e.color(kFoamColor));
        u.q[0] = linear_rgb(e.color(kWaterColor));
        return sea_pass(ctx, e, ShaderId::effects_sea_shore_wave_frag, in, in.region, u, "onda-na-praia", out, quality(ctx));
    }
};

} // namespace

void register_sea_effects(EffectRegistry& r) {
    (void)r.add(std::make_unique<Ocean>());
    (void)r.add(std::make_unique<Underwater>());
    (void)r.add(std::make_unique<Caustics>());
    (void)r.add(std::make_unique<ShoreWave>());
}

} // namespace aurea::builtin
