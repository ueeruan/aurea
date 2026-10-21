// =============================================================================
//  Aurea / effects / builtin / AuroraEffect.cpp
//
//  Aurora (Gerar, 0.2.7): aurora boreal animada sobre um céu com estrelas e o
//  reflexo numa lâmina d'água. Os controles seguem os de um gerador de aurora
//  do After Effects (comportamento especificado pela tela de controles; ver
//  build/reference/ae2026/NOTAS.md): Distorção, Quantidade, Escala,
//  Comprimento, Exposição, Detalhe, Curvatura, Brilho do céu, as três cores,
//  Deslocamento de tempo e Velocidade, mais o grupo Ambiente.
//
//  Implementação própria (nenhum código, shader ou asset de terceiros): cada
//  pixel é um raio de uma câmera virtual na altura da água; as cortinas são
//  lâminas horizontais empilhadas no céu, e em cada lâmina uma faixa sinuosa
//  de ruído com raios verticais finos. Abaixo do horizonte o raio é espelhado
//  (o reflexo), com a ondulação da água.
// =============================================================================
#include "BuiltinEffects.hpp"

#include <algorithm>
#include <cmath>

namespace aurea::builtin {
namespace {

f32 finite_or(f32 v, f32 fallback) noexcept { return std::isfinite(v) ? v : fallback; }

struct AuroraUniforms {
    EffectUniforms base{};
    Vec4 q0{};   ///< rgb = cor secundária (linear), w = estrelas 0..1
    Vec4 q1{};   ///< rgb = cor do céu (linear), w = campo de visão (graus)
    Vec4 q2{};   ///< x = inclinação °, y = giro °, zw = tamanho da camada (px)
    Vec4 q3{};   ///< x = opacidade 0..1, y = altura do céu, z = deslocamento do horizonte, w = 0
};
static_assert(sizeof(AuroraUniforms) == 176, "layout std140 dos uniforms de gerador");

class Aurora final : public Effect {
public:
    enum : u32 {
        kWarp = 0, kAmount, kScale, kLength, kExposure, kDetail, kCurvature, kSkyGlow,
        kColor, kSecondary, kSkyColor, kTimeOffset, kSpeed,
        // Ambiente
        kHorizon, kReflection, kRipple, kStars, kTilt, kPan, kFov, kSkyHeight,
        kOpacity,
    };

    const EffectInfo& info() const noexcept override {
        static const EffectInfo i{effect_keys::kAurora, "Aurora", "Gerar", EffectClass::Neighborhood};
        return i;
    }
    void declare_parameters(ParameterRegistry& p) const override {
        p.add_float("warp", "Distorção", 0.0f, -100.0f, 100.0f);
        p.typed_range(-1000.0f, 1000.0f);
        p.add_float("amount", "Quantidade", 100.0f, 0.0f, 300.0f, kParamAnimatable | kParamPercent, "%");
        p.typed_range(0.0f, 2000.0f);
        p.add_float("scale", "Escala", 5.0f, 0.5f, 30.0f);
        p.typed_range(0.05f, 300.0f);
        p.add_float("length", "Comprimento", 50.0f, 0.0f, 200.0f);
        p.typed_range(0.0f, 1000.0f);
        p.add_float("exposure", "Exposição", 40.0f, 0.0f, 100.0f);
        p.typed_range(-100.0f, 300.0f);
        p.add_float("detail", "Detalhe", 60.0f, 0.0f, 100.0f);
        p.add_float("curvature", "Curvatura", 4.0f, -30.0f, 30.0f);
        p.typed_range(-300.0f, 300.0f);
        p.add_float("sky_glow", "Brilho do céu", 20.0f, 0.0f, 100.0f);
        p.typed_range(0.0f, 1000.0f);
        p.add_color("color", "Cor", Vec4{0.0f, 0.62f, 0.55f, 1.0f});
        p.add_color("secondary_color", "Cor secundária", Vec4{0.10f, 0.48f, 0.05f, 1.0f});
        p.add_color("sky_color", "Cor do céu", Vec4{0.004f, 0.006f, 0.016f, 1.0f});
        p.add_float("time_offset", "Deslocamento de tempo", 0.0f, -1000.0f, 1000.0f, kParamAnimatable, "s");
        p.typed_range(-100000.0f, 100000.0f);
        p.add_float("speed", "Velocidade", 40.0f, 0.0f, 200.0f);
        p.typed_range(-1000.0f, 1000.0f);
        // Ambiente
        p.add_float("horizon", "Horizonte", 0.0f, -100.0f, 100.0f, kParamAnimatable | kParamPercent, "%");
        p.add_float("reflection", "Reflexo", 80.0f, 0.0f, 100.0f, kParamAnimatable | kParamPercent, "%");
        p.add_float("ripple", "Ondulação da água", 25.0f, 0.0f, 100.0f, kParamAnimatable | kParamPercent, "%");
        p.add_float("stars", "Estrelas", 35.0f, 0.0f, 100.0f, kParamAnimatable | kParamPercent, "%");
        p.add_angle("tilt", "Inclinação da câmera", 10.0f);
        p.add_angle("pan", "Giro da câmera", 0.0f);
        p.add_float("fov", "Campo de visão", 75.0f, 20.0f, 140.0f, kParamAnimatable, "°");
        p.add_float("sky_height", "Altura das cortinas", 50.0f, 0.0f, 100.0f, kParamAnimatable | kParamPercent, "%");
        p.add_float("opacity", "Opacidade", 100.0f, 0.0f, 100.0f, kParamAnimatable | kParamPercent, "%");
        p.add_section("environment", "Ambiente", {kHorizon, kReflection, kRipple, kStars, kTilt, kPan, kFov, kSkyHeight});
    }
    bool demo_values(EffectInstance&, std::vector<ParamValue>& v) const noexcept override {
        v[kAmount] = ParamValue::scalar(140.0f);
        v[kExposure] = ParamValue::scalar(45.0f);
        return true;
    }
    bool is_identity(const EffectEval& e) const noexcept override {
        return !(finite_or(e.f(kOpacity), 100.0f) > 0.01f);
    }
    void pipelines(std::vector<PipelineKey>& out, SurfaceFormat work) const override {
        out.push_back(PipelineKey::fullscreen(ShaderId::effects_aurora_frag, work));
    }
    Status build(EffectBuildContext& ctx, const EffectEval& e, const LayerImage& input, f32,
                 LayerImage& out) const override {
        const f32 w = e.placement && e.placement->layerWidth ? static_cast<f32>(e.placement->layerWidth) : input.region.w;
        const f32 h = e.placement && e.placement->layerHeight ? static_cast<f32>(e.placement->layerHeight) : input.region.h;
        const f64 fps = e.framesPerSecond > 0.0 ? e.framesPerSecond : 30.0;
        // Relógio da aurora: segundos da camada mais o deslocamento, na velocidade
        // (40 = o ritmo calmo de referência). Guardado só com a parte útil.
        const f64 seconds = e.time_frames() / fps + static_cast<f64>(finite_or(e.f(kTimeOffset), 0.0f));
        const f64 clock = seconds * static_cast<f64>(finite_or(e.f(kSpeed), 40.0f)) * 0.025;
        const f32 time = static_cast<f32>(clock - std::floor(clock / 4096.0) * 4096.0);
        AuroraUniforms u;
        u.base = base_uniforms(input);
        u.base.p0 = Vec4{finite_or(e.f(kWarp), 0.0f) * 0.01f,
                         std::max(0.0f, finite_or(e.f(kAmount), 100.0f)) * 0.01f,
                         std::clamp(finite_or(e.f(kScale), 5.0f), 0.05f, 300.0f),
                         std::max(0.0f, finite_or(e.f(kLength), 50.0f)) * 0.01f};
        u.base.p1 = Vec4{finite_or(e.f(kExposure), 40.0f),
                         std::clamp(finite_or(e.f(kDetail), 60.0f), 0.0f, 100.0f) * 0.01f,
                         finite_or(e.f(kCurvature), 4.0f) * 0.01f,
                         std::max(0.0f, finite_or(e.f(kSkyGlow), 20.0f)) * 0.01f};
        u.base.p2 = Vec4{input.region.x, input.region.y, input.region.w, input.region.h};
        u.base.p3 = Vec4{time,
                         std::clamp(finite_or(e.f(kHorizon), 0.0f), -100.0f, 100.0f) * 0.01f,
                         std::clamp(finite_or(e.f(kReflection), 80.0f), 0.0f, 100.0f) * 0.01f,
                         std::clamp(finite_or(e.f(kRipple), 25.0f), 0.0f, 100.0f) * 0.01f};
        u.base.color = e.color(kColor);
        const Vec4 secondary = e.color(kSecondary), sky = e.color(kSkyColor);
        u.q0 = Vec4{secondary.x, secondary.y, secondary.z, std::clamp(finite_or(e.f(kStars), 35.0f), 0.0f, 100.0f) * 0.01f};
        u.q1 = Vec4{sky.x, sky.y, sky.z, std::clamp(finite_or(e.f(kFov), 75.0f), 20.0f, 140.0f)};
        u.q2 = Vec4{std::clamp(finite_or(e.f(kTilt), 10.0f), -89.0f, 89.0f), finite_or(e.f(kPan), 0.0f), w, h};
        u.q3 = Vec4{std::clamp(finite_or(e.f(kOpacity), 100.0f), 0.0f, 100.0f) * 0.01f,
                    std::clamp(finite_or(e.f(kSkyHeight), 50.0f), 0.0f, 100.0f) * 0.01f, 0.0f, 0.0f};
        out = input;
        out.texture = ctx.texture("aurora", input.width, input.height);
        if (ctx.fullscreen_pass("aurora", PassStage::Effects, out.texture, ShaderId::effects_aurora_frag,
                                {PassTexture{input.texture, {}, CommonSampler::LinearClamp}},
                                &u, sizeof(u)) == kInvalidIndex) {
            return Errc::PipelineCompileFailed;
        }
        return OkStatus;
    }
};

} // namespace

void register_aurora_effect(EffectRegistry& r) { (void)r.add(std::make_unique<Aurora>()); }

} // namespace aurea::builtin
