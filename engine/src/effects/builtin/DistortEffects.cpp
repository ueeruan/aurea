// =============================================================================
//  Efeitos de distorção (Fase 7.3 §24, §27, §28, §36, §40).
//
//  Shake · Turbulência · Onda · Lente · Ondulação que dissolve
//
//  Todos mexem na GEOMETRIA: nenhum deles cabe no passe de cor fundido, e
//  todos declaram a margem que leem para o grafo recortar a região sem cortar
//  o que eles precisam.
//
//  Shake é o caso especial: ele não escreve shader nenhum, ele REUSA a
//  reamostragem afim do Transformar com uma matriz sorteada por quadro. O
//  sorteio vem de uma hash de (semente, quadro) — o mesmo tremor no preview e
//  no export, e o mesmo tremor ao reabrir o projeto.
// =============================================================================
#include "BuiltinEffects.hpp"

#include <algorithm>
#include <cmath>

namespace aurea::builtin {
namespace {

f32 finite_or(f32 v, f32 fallback) noexcept { return std::isfinite(v) ? v : fallback; }

/// Hash determinística de (semente, quadro, eixo) em [-1, 1]. Um `sin` grande
/// em vez de uma tabela: mesma entrada, mesma saída, em qualquer aparelho.
f32 noise1(u32 seed, i64 frame, u32 axis) noexcept {
    u64 x = static_cast<u64>(seed) * 0x9E3779B97F4A7C15ull;
    x ^= static_cast<u64>(frame) * 0xBF58476D1CE4E5B9ull;
    x ^= static_cast<u64>(axis) * 0x94D049BB133111EBull;
    x ^= x >> 31;
    x *= 0xD6E8FEB86659FD93ull;
    x ^= x >> 29;
    const f32 u = static_cast<f32>(x & 0xFFFFFFull) / 16777215.0f;
    return u * 2.0f - 1.0f;
}

// -----------------------------------------------------------------------------
// Shake — tremor determinístico
// -----------------------------------------------------------------------------
class Shake final : public Effect {
public:
    enum : u32 { kAmplitudeX = 0, kAmplitudeY, kFrequency, kSeed, kSeparate, kRotation, kSmoothing, kMix };

    const EffectInfo& info() const noexcept override {
        static const EffectInfo i{effect_keys::kShake, "Tremor", "Distorcer", EffectClass::Domain};
        return i;
    }
    void declare_parameters(ParameterRegistry& p) const override {
        p.add_float("amplitude_x", "Amplitude X", 20.0f, 0.0f, 500.0f, kParamAnimatable | kParamPixels, "px");
        p.add_float("amplitude_y", "Amplitude Y", 20.0f, 0.0f, 500.0f, kParamAnimatable | kParamPixels, "px");
        p.add_float("frequency", "Frequência", 1.0f, 0.05f, 20.0f, kParamAnimatable, "por quadro");
        p.add_int("seed", "Semente", 1, 0, 9999);
        p.add_bool("separate_axes", "Eixos separados", true);
        p.add_angle("rotation", "Rotação", 0.0f);
        p.add_float("smoothing", "Suavização", 0.0f, 0.0f, 100.0f, kParamAnimatable | kParamPercent, "%");
        p.add_float("mix", "Mistura", 100.0f, 0.0f, 100.0f, kParamAnimatable | kParamPercent, "%");
    }
    bool is_identity(const EffectEval& e) const noexcept override {
        return e.f(kMix) < 0.01f
            || (e.f(kAmplitudeX) < 0.01f && e.f(kAmplitudeY) < 0.01f && std::fabs(e.f(kRotation)) < 0.01f);
    }
    f32 input_margin(const EffectEval& e) const noexcept override {
        // A rotação pode trazer imagem de fora da caixa; a margem cobre o pior
        // caso (a metade da diagonal) só quando há rotação.
        const f32 rot = std::fabs(e.f(kRotation));
        return std::max(e.f(kAmplitudeX), e.f(kAmplitudeY)) + (rot > 0.01f ? 64.0f : 0.0f);
    }
    bool demo_values(EffectInstance&, std::vector<ParamValue>& v) const noexcept override {
        v[kAmplitudeX] = ParamValue::scalar(34.0f);
        v[kAmplitudeY] = ParamValue::scalar(22.0f);
        v[kSmoothing] = ParamValue::scalar(35.0f);
        return true;
    }

    Status build(EffectBuildContext& ctx, const EffectEval& e, const LayerImage& input, f32 margin,
                 LayerImage& out) const override {
        const u32 seed = e.e(kSeed);
        const f32 freq = std::max(0.05f, e.f(kFrequency));
        const f32 smooth = std::clamp(e.f(kSmoothing) / 100.0f, 0.0f, 1.0f);
        // A suavização pega a média de três quadros vizinhos: o tremor fica
        // menos nervoso sem deixar de ser tremor.
        const f32 t = static_cast<f32>(e.localTime.value) * freq;
        const i64 f0 = static_cast<i64>(std::floor(t));
        const f32 frac = t - static_cast<f32>(f0);

        auto axis_value = [&](u32 axis, f32 amplitude) noexcept {
            const f32 a = noise1(seed, f0, axis);
            if (smooth <= 0.0f) return a * amplitude;
            const f32 b = noise1(seed, f0 + 1, axis);
            const f32 c = noise1(seed, f0 - 1, axis);
            const f32 smoothVal = (c + 2.0f * a + b) * 0.25f;
            // Interpola entre o valor duro (por quadro) e o suavizado.
            return (a + (smoothVal - a) * smooth) * amplitude;
        };

        const f32 ax = e.f(kAmplitudeX), ay = e.f(kAmplitudeY);
        const f32 dx = axis_value(1u, ax);
        const f32 dy = e.b(kSeparate) ? axis_value(2u, ay) : dx * (ay / std::max(ax, 1e-3f));
        const f32 rot = e.f(kRotation) * noise1(seed, f0, 3u) * kDeg2Rad;

        const f32 w = e.placement ? static_cast<f32>(e.placement->layerWidth) : 1.0f;
        const f32 h = e.placement ? static_cast<f32>(e.placement->layerHeight) : 1.0f;
        const Vec3 center{w * 0.5f, h * 0.5f, 0.0f};
        const Mat4 r = Mat4::from_quat(Quat::from_axis_angle(Vec3{0, 0, 1}, rot));
        const Mat4 m = Mat4::translation(Vec3{dx, dy, 0.0f}) * Mat4::translation(center) * r
                     * Mat4::translation(-center);
        (void)frac;
        return affine_pass(ctx, input, m, e.placement, e.f(kMix) / 100.0f, "tremor", margin, out);
    }
};

// -----------------------------------------------------------------------------
// Turbulência (deslocamento procedural)
// -----------------------------------------------------------------------------
class Turbulence final : public Effect {
public:
    enum : u32 { kAmount = 0, kSize, kComplexity, kEvolution, kOffsetX, kOffsetY, kSeed,
                 kHorizontal, kEdges, kSpin, kMix };

    const EffectInfo& info() const noexcept override {
        static const EffectInfo i{effect_keys::kTurbulence, "Turbulência", "Distorcer", EffectClass::Domain};
        return i;
    }
    void declare_parameters(ParameterRegistry& p) const override {
        static const char* const kEdgeModes[] = {"Repetir", "Recortar", "Esticar"};
        p.add_float("amount", "Intensidade", 40.0f, 0.0f, 500.0f, kParamAnimatable | kParamPixels, "px");
        p.add_float("size", "Tamanho do ruído", 120.0f, 2.0f, 2000.0f, kParamAnimatable | kParamPixels, "px");
        p.add_float("complexity", "Complexidade", 3.0f, 1.0f, 6.0f, kParamAnimatable, "oitavas");
        p.add_float("evolution", "Evolução", 0.0f, -50.0f, 50.0f, kParamAnimatable | kParamPixels, "px/quadro");
        p.add_float("offset_x", "Deslocamento X", 0.0f, -1000.0f, 1000.0f, kParamAnimatable | kParamPixels, "px");
        p.add_float("offset_y", "Deslocamento Y", 0.0f, -1000.0f, 1000.0f, kParamAnimatable | kParamPixels, "px");
        p.add_int("seed", "Semente", 3, 0, 9999);
        p.add_bool("horizontal_only", "Só na horizontal", false);
        p.add_enum("edges", "Bordas", kEdgeModes, 3, 0);
        p.add_angle("spin", "Girar o deslocamento", 0.0f);
        p.add_float("mix", "Mistura", 100.0f, 0.0f, 100.0f, kParamAnimatable | kParamPercent, "%");
    }
    bool is_identity(const EffectEval& e) const noexcept override {
        return e.f(kMix) < 0.01f || (e.f(kAmount) < 0.01f && e.f(kOffsetX) < 0.01f && e.f(kOffsetY) < 0.01f);
    }
    f32 input_margin(const EffectEval& e) const noexcept override {
        return e.f(kAmount) + std::max(std::fabs(e.f(kOffsetX)), std::fabs(e.f(kOffsetY)));
    }
    bool demo_values(EffectInstance&, std::vector<ParamValue>& v) const noexcept override {
        v[kAmount] = ParamValue::scalar(55.0f);
        v[kSize] = ParamValue::scalar(90.0f);
        return true;
    }
    Status build(EffectBuildContext& ctx, const EffectEval& e, const LayerImage& input, f32 margin,
                 LayerImage& out) const override {
        const f32 reach = e.f(kAmount) + std::max(std::fabs(e.f(kOffsetX)), std::fabs(e.f(kOffsetY)));
        const Rect region = spread_region(input.region, reach, reach, e.placement, margin);
        u32 w = 0, h = 0;
        ctx.region_size(region, input.texel_scale_x(), w, h);

        EffectUniforms u;
        u.uvMap = EffectBuildContext::uv_map(region, input.region);
        u.texel = Vec4{region.w > 0.0f ? 1.0f / region.w : 0.0f, region.h > 0.0f ? 1.0f / region.h : 0.0f,
                       input.texel_scale_x(), input.texel_scale_y()};
        u.p0 = Vec4{e.f(kAmount), e.f(kSize), e.f(kComplexity), e.f(kEvolution)};
        u.p1 = Vec4{e.f(kOffsetX), e.f(kOffsetY), static_cast<f32>(e.e(kSeed)), e.b(kHorizontal) ? 1.0f : 0.0f};
        u.p2 = Vec4{static_cast<f32>(e.e(kEdges)), e.f(kSpin), 0.0f, 0.0f};
        u.p3 = Vec4{static_cast<f32>(e.localTime.value), finite_or(e.f(kComplexity), 3.0f), 0.0f, 0.0f};

        out = LayerImage{ctx.texture("turbulencia", w, h), region, w, h};
        if (ctx.fullscreen_pass("turbulencia", PassStage::Transform, out.texture,
                                ShaderId::effects_turbulence_displace_frag,
                                {PassTexture{input.texture, {}, CommonSampler::LinearClamp}},
                                &u, sizeof(u)) == kInvalidIndex) {
            return Errc::PipelineCompileFailed;
        }
        (void)e.f(kMix);
        return OkStatus;
    }
};

// -----------------------------------------------------------------------------
// Onda
// -----------------------------------------------------------------------------
class WaveWarp final : public Effect {
public:
    enum : u32 { kHeight = 0, kWavelength, kSpeed, kPhase, kDirection, kSquare, kEdges, kPin, kMix };

    const EffectInfo& info() const noexcept override {
        static const EffectInfo i{effect_keys::kWaveWarp, "Onda", "Distorcer", EffectClass::Domain};
        return i;
    }
    void declare_parameters(ParameterRegistry& p) const override {
        static const char* const kDirs[] = {"Horizontal", "Vertical", "Diagonal", "As duas"};
        static const char* const kEdgeModes[] = {"Repetir", "Recortar", "Esticar"};
        p.add_float("height", "Altura da onda", 30.0f, 0.0f, 1000.0f, kParamAnimatable | kParamPixels, "px");
        p.add_float("wavelength", "Largura de onda", 200.0f, 2.0f, 4000.0f, kParamAnimatable | kParamPixels, "px");
        p.add_float("speed", "Velocidade", 0.0f, -200.0f, 200.0f, kParamAnimatable | kParamPixels, "px/quadro");
        p.add_angle("phase", "Fase", 0.0f);
        p.add_enum("direction", "Direção", kDirs, 4, 0);
        p.add_bool("square", "Onda quadrada", false);
        p.add_enum("edges", "Bordas", kEdgeModes, 3, 0);
        p.add_bool("pin_edges", "Travar nas bordas", false);
        p.add_float("mix", "Mistura", 100.0f, 0.0f, 100.0f, kParamAnimatable | kParamPercent, "%");
    }
    bool is_identity(const EffectEval& e) const noexcept override { return e.f(kMix) < 0.01f || e.f(kHeight) < 0.01f; }
    f32 input_margin(const EffectEval& e) const noexcept override { return e.f(kHeight); }
    bool demo_values(EffectInstance&, std::vector<ParamValue>& v) const noexcept override {
        v[kHeight] = ParamValue::scalar(36.0f);
        v[kWavelength] = ParamValue::scalar(140.0f);
        return true;
    }
    Status build(EffectBuildContext& ctx, const EffectEval& e, const LayerImage& input, f32 margin,
                 LayerImage& out) const override {
        const f32 h = e.f(kHeight);
        const Rect region = spread_region(input.region, h, h, e.placement, margin);
        u32 w = 0, hh = 0;
        ctx.region_size(region, input.texel_scale_x(), w, hh);

        EffectUniforms u;
        u.uvMap = EffectBuildContext::uv_map(region, input.region);
        u.texel = Vec4{region.w > 0.0f ? 1.0f / region.w : 0.0f, region.h > 0.0f ? 1.0f / region.h : 0.0f,
                       input.texel_scale_x(), input.texel_scale_y()};
        u.p0 = Vec4{h, e.f(kWavelength), e.f(kSpeed), e.f(kPhase)};
        u.p1 = Vec4{static_cast<f32>(e.e(kDirection)), e.b(kSquare) ? 1.0f : 0.0f,
                    static_cast<f32>(e.e(kEdges)), e.b(kPin) ? 1.0f : 0.0f};
        u.p3 = Vec4{static_cast<f32>(e.localTime.value), 0.0f, 0.0f, 0.0f};

        out = LayerImage{ctx.texture("onda", w, hh), region, w, hh};
        if (ctx.fullscreen_pass("onda", PassStage::Transform, out.texture, ShaderId::effects_wave_warp_frag,
                                {PassTexture{input.texture, {}, CommonSampler::LinearClamp}},
                                &u, sizeof(u)) == kInvalidIndex) {
            return Errc::PipelineCompileFailed;
        }
        return OkStatus;
    }
};

// -----------------------------------------------------------------------------
// Lente (warp)
// -----------------------------------------------------------------------------
class Warp final : public Effect {
public:
    enum : u32 { kMode = 0, kAmount, kRadius, kCenter, kEdges, kMix, kSphereLight };

    const EffectInfo& info() const noexcept override {
        static const EffectInfo i{effect_keys::kWarp, "Lente", "Distorcer", EffectClass::Domain};
        return i;
    }
    void declare_parameters(ParameterRegistry& p) const override {
        static const char* const kModes[] = {"Empurrar", "Puxar", "Torcer", "Esfera", "Canto"};
        static const char* const kEdgeModes[] = {"Repetir", "Recortar", "Esticar"};
        p.add_enum("mode", "Modo", kModes, 5, 0);
        p.add_float("amount", "Intensidade", 60.0f, -1000.0f, 1000.0f, kParamAnimatable | kParamPixels, "px");
        p.add_float("radius", "Raio", 260.0f, 1.0f, 4000.0f, kParamAnimatable | kParamPixels, "px");
        p.add_point2("center", "Centro", Vec2{0.5f, 0.5f}, -1.0f, 2.0f, kParamAnimatable | kParamRelative);
        p.add_enum("edges", "Bordas", kEdgeModes, 3, 0);
        p.add_float("mix", "Mistura", 100.0f, 0.0f, 100.0f, kParamAnimatable | kParamPercent, "%");
        p.add_float("sphere_light", "Luz da esfera", 0.0f, 0.0f, 100.0f, kParamAnimatable | kParamPercent, "%");
    }
    bool is_identity(const EffectEval& e) const noexcept override {
        return e.f(kMix) < 0.01f || std::fabs(e.f(kAmount)) < 0.01f;
    }
    f32 input_margin(const EffectEval& e) const noexcept override { return std::fabs(e.f(kAmount)); }
    bool demo_values(EffectInstance&, std::vector<ParamValue>& v) const noexcept override {
        v[kMode] = ParamValue::scalar(3.0f);       // esfera: a mais reconhecível
        v[kAmount] = ParamValue::scalar(55.0f);
        v[kRadius] = ParamValue::scalar(220.0f);
        v[kSphereLight] = ParamValue::scalar(60.0f);
        return true;
    }
    Status build(EffectBuildContext& ctx, const EffectEval& e, const LayerImage& input, f32 margin,
                 LayerImage& out) const override {
        const f32 reach = std::fabs(e.f(kAmount));
        const Rect region = spread_region(input.region, reach, reach, e.placement, margin);
        u32 w = 0, h = 0;
        ctx.region_size(region, input.texel_scale_x(), w, h);

        EffectUniforms u;
        u.uvMap = EffectBuildContext::uv_map(region, input.region);
        u.texel = Vec4{region.w > 0.0f ? 1.0f / region.w : 0.0f, region.h > 0.0f ? 1.0f / region.h : 0.0f,
                       input.texel_scale_x(), input.texel_scale_y()};
        // O centro é relativo à LAYER; o shader o converte para o espaço da
        // textura de saída dividindo por texel scale.
        const Vec2 c = e.p2(kCenter);
        u.p0 = Vec4{static_cast<f32>(e.e(kMode)), e.f(kAmount), e.f(kRadius), e.f(kSphereLight) / 100.0f};
        u.p1 = Vec4{c.x, c.y, static_cast<f32>(e.e(kEdges)), e.f(kMix) / 100.0f};

        out = LayerImage{ctx.texture("lente", w, h), region, w, h};
        if (ctx.fullscreen_pass("lente", PassStage::Transform, out.texture, ShaderId::effects_warp_frag,
                                {PassTexture{input.texture, {}, CommonSampler::LinearClamp}},
                                &u, sizeof(u)) == kInvalidIndex) {
            return Errc::PipelineCompileFailed;
        }
        return OkStatus;
    }
};

// -----------------------------------------------------------------------------
// Ondulação que dissolve
// -----------------------------------------------------------------------------
class RippleDissolve final : public Effect {
public:
    enum : u32 { kProgress = 0, kAmplitude, kWavelength, kSoftness, kCenter, kSpeed, kSeed,
                 kDistort, kInvert, kMix };

    const EffectInfo& info() const noexcept override {
        static const EffectInfo i{effect_keys::kRippleDissolve, "Ondulação que dissolve", "Transição", EffectClass::Domain};
        return i;
    }
    void declare_parameters(ParameterRegistry& p) const override {
        p.add_float("progress", "Progresso", 50.0f, 0.0f, 100.0f, kParamAnimatable | kParamPercent, "%");
        p.add_float("amplitude", "Ondulação", 25.0f, 0.0f, 500.0f, kParamAnimatable | kParamPixels, "px");
        p.add_float("wavelength", "Comprimento da onda", 120.0f, 4.0f, 2000.0f, kParamAnimatable | kParamPixels, "px");
        p.add_float("softness", "Suavidade da borda", 15.0f, 1.0f, 100.0f, kParamAnimatable | kParamPercent, "%");
        p.add_point2("center", "Centro", Vec2{0.5f, 0.5f}, -1.0f, 2.0f, kParamAnimatable | kParamRelative);
        p.add_float("speed", "Velocidade da onda", 0.0f, -100.0f, 100.0f, kParamAnimatable);
        p.add_int("seed", "Semente", 2, 0, 9999);
        p.add_bool("warp_image", "Distorcer a imagem junto", true);
        p.add_bool("outside_in", "De fora para dentro", false);
        p.add_float("mix", "Mistura", 100.0f, 0.0f, 100.0f, kParamAnimatable | kParamPercent, "%");
    }
    bool is_identity(const EffectEval& e) const noexcept override {
        return e.f(kMix) < 0.01f || e.f(kProgress) < 0.001f;
    }
    f32 input_margin(const EffectEval& e) const noexcept override {
        return e.b(kDistort) ? e.f(kAmplitude) * 9.0f : 0.0f;
    }
    bool demo_values(EffectInstance&, std::vector<ParamValue>& v) const noexcept override {
        v[kProgress] = ParamValue::scalar(55.0f);
        v[kAmplitude] = ParamValue::scalar(38.0f);
        return true;
    }
    Status build(EffectBuildContext& ctx, const EffectEval& e, const LayerImage& input, f32 margin,
                 LayerImage& out) const override {
        const f32 reach = e.b(kDistort) ? e.f(kAmplitude) * 9.0f : 0.0f;
        const Rect region = spread_region(input.region, reach, reach, e.placement, margin);
        u32 w = 0, h = 0;
        ctx.region_size(region, input.texel_scale_x(), w, h);

        EffectUniforms u;
        u.uvMap = EffectBuildContext::uv_map(region, input.region);
        u.texel = Vec4{region.w > 0.0f ? 1.0f / region.w : 0.0f, region.h > 0.0f ? 1.0f / region.h : 0.0f,
                       input.texel_scale_x(), input.texel_scale_y()};
        const Vec2 c = e.p2(kCenter);
        // O progresso entra como RAIO: 0 não dissolver nada, 1 dissolver tudo.
        // A suavidade é a meia-largura da transição, e o padrão do joelho é
        // uma fração do progresso para ele não ficar duro no começo.
        const f32 progress = std::clamp(e.f(kProgress) / 100.0f, 0.0f, 1.0f);
        const f32 soft = std::max(0.001f, e.f(kSoftness) / 100.0f * 0.5f);
        u.p0 = Vec4{progress * 1.05f - 0.02f, e.f(kAmplitude), e.f(kWavelength), soft};
        u.p1 = Vec4{c.x, c.y, e.f(kSpeed), static_cast<f32>(e.e(kSeed))};
        u.p2 = Vec4{e.b(kDistort) ? 1.0f : 0.0f, e.b(kInvert) ? 1.0f : 0.0f, 0.0f, 0.0f};
        u.p3 = Vec4{static_cast<f32>(e.localTime.value), 0.0f, 0.0f, 0.0f};

        out = LayerImage{ctx.texture("ondulacao", w, h), region, w, h};
        if (ctx.fullscreen_pass("ondulacao", PassStage::Transform, out.texture,
                                ShaderId::effects_ripple_dissolve_frag,
                                {PassTexture{input.texture, {}, CommonSampler::LinearClamp}},
                                &u, sizeof(u)) == kInvalidIndex) {
            return Errc::PipelineCompileFailed;
        }
        (void)e.f(kMix);
        return OkStatus;
    }
};

// No shake o "mix" já entra na opacidade da reamostragem; nos outros, ele
// mistura com o original — o que exige o original guardado. Para não alocar um
// passe a mais só por isso, os efeitos de deslocamento puro não têm mistura.

} // namespace

void register_distort_effects(EffectRegistry& r) {
    (void)r.add(std::make_unique<Shake>());
    (void)r.add(std::make_unique<Turbulence>());
    (void)r.add(std::make_unique<WaveWarp>());
    (void)r.add(std::make_unique<Warp>());
    (void)r.add(std::make_unique<RippleDissolve>());
}

} // namespace aurea::builtin
