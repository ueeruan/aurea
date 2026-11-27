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
    enum : u32 { kColor = 0, kAlpha, kChannel, kBlend };

    const EffectInfo& info() const noexcept override {
        static const EffectInfo i{effect_keys::kInvert, "Inverter", "Cor", EffectClass::PerPixel};
        return i;
    }
    void declare_parameters(ParameterRegistry& p) const override {
        p.add_bool("invert_color", "Inverter as cores", true);
        p.add_bool("invert_alpha", "Inverter a transparência", false);
        // AE 2026 (no fim): o canal que "Inverter as cores" inverte e a mistura
        // com o original. Padrões = o comportamento de antes (RGB, 0%).
        static const char* const kChannels[] = {"RGB", "Vermelho", "Verde", "Azul", "HLS", "Matiz", "Luminosidade",
                                                "Saturação", "YIQ", "Luminância", "Croma em fase (I)",
                                                "Croma em quadratura (Q)", "Alfa"};
        p.add_enum("channel", "Canal", kChannels, 13, 0);
        p.add_float("blend", "Misturar com o original", 0.0f, 0.0f, 100.0f, kParamAnimatable | kParamPercent, "%");
    }
    bool is_identity(const EffectEval& e) const noexcept override {
        const f32 blend = std::isfinite(e.f(kBlend)) ? e.f(kBlend) : 0.0f;
        return (!e.b(kColor) && !e.b(kAlpha)) || blend >= 99.999f;
    }
    bool color_op(const EffectEval& e, ColorOp& op) const noexcept override {
        op.code = ColorOpCode::Invert;
        op.p[1] = e.b(kColor) ? 1.0f : 0.0f;
        op.p[2] = e.b(kAlpha) ? 1.0f : 0.0f;
        op.p[3] = static_cast<f32>(std::min<u32>(e.e(kChannel), 12u));
        const f32 blend = std::isfinite(e.f(kBlend)) ? e.f(kBlend) : 0.0f;
        op.p[4] = std::clamp(blend / 100.0f, 0.0f, 1.0f);
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
        p.add_float("speed", "Velocidade", 0.0f, -20.0f, 20.0f, kParamAnimatable | kParamPerFrame, "px/quadro");
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

class SinusoidalScanlines final : public Effect {
public:
    explicit SinusoidalScanlines(bool monochrome) noexcept : monochrome_(monochrome) {}

    enum : u32 {
        kFrequency, kSharpness, kAngle, kPhase, kBrightness, kOffset, kGamma,
        kSaturation, kRedPhase, kGreenPhase, kBluePhase, kTint,
    };

    const EffectInfo& info() const noexcept override {
        static const EffectInfo color{"aurea.stylize.sinusoidal_scanlines", "Varredura senoidal",
                                      "Estilizar", EffectClass::Neighborhood};
        static const EffectInfo monochrome{"aurea.stylize.sinusoidal_scanlines_mono", "Varredura senoidal P&B",
                                           "Estilizar", EffectClass::Neighborhood};
        return monochrome_ ? monochrome : color;
    }
    void declare_parameters(ParameterRegistry& p) const override {
        p.add_float("frequency", "Frequência", 50.0f, 1.0f, 1000.0f, kParamAnimatable);
        p.add_float("sharpness", "Nitidez", 1.0f, 0.0f, 8.0f, kParamAnimatable);
        p.add_angle("angle", "Ângulo", 0.0f);
        p.add_float("phase", "Fase", 0.0f, -1.0f, 1.0f, kParamAnimatable);
        p.typed_range(-100.0f, 100.0f);
        p.add_float("brightness", "Brilho", 1.0f, 0.0f, 8.0f, kParamAnimatable);
        p.add_float("offset", "Deslocamento tonal", 0.0f, -2.0f, 2.0f, kParamAnimatable);
        p.add_float("gamma", "Gama", 1.5f, 0.1f, 10.0f, kParamAnimatable);
        if (!monochrome_) {
            p.add_float("saturation", "Saturação", 1.0f, 0.0f, 8.0f, kParamAnimatable);
            p.add_float("red_phase", "Fase do vermelho", 0.0f, -1.0f, 1.0f, kParamAnimatable);
            p.add_float("green_phase", "Fase do verde", 0.0f, -1.0f, 1.0f, kParamAnimatable);
            p.add_float("blue_phase", "Fase do azul", 0.0f, -1.0f, 1.0f, kParamAnimatable);
            p.add_color("tint", "Cor", Vec4{1, 1, 1, 1});
        }
    }
    void pipelines(std::vector<PipelineKey>& out, SurfaceFormat work) const override {
        out.push_back(PipelineKey::fullscreen(ShaderId::effects_scanlines_frag, work));
    }
    Status build(EffectBuildContext& ctx, const EffectEval& e, const LayerImage& input, f32,
                 LayerImage& out) const override {
        EffectUniforms u = base_uniforms(input);
        auto read = [&](u32 index, f32 fallback, f32 lo, f32 hi) {
            return std::clamp(finite_or(e.f(index), fallback), lo, hi);
        };
        const f32 angle = std::remainder(finite_or(e.f(kAngle), 0.0f), 360.0f) * kDeg2Rad;
        const f32 phase = std::remainder(finite_or(e.f(kPhase), 0.0f), 1.0f);
        u.p0 = {read(kFrequency, 50.0f, 1.0f, 1000.0f), read(kSharpness, 1.0f, 0.0f, 8.0f), angle, phase};
        u.p1.w = read(kBrightness, 1.0f, 0.0f, 8.0f);
        u.p2 = {read(kOffset, 0.0f, -2.0f, 2.0f), read(kGamma, 1.5f, 0.1f, 10.0f), 0.0f,
                monochrome_ ? 2.0f : 1.0f};
        u.p3.x = input.region.h / std::max(1e-6f, input.region.w);
        u.color = Vec4{1, 1, 1, 1};
        if (!monochrome_) {
            u.p1.x = std::remainder(finite_or(e.f(kRedPhase), 0.0f), 1.0f);
            u.p1.y = std::remainder(finite_or(e.f(kGreenPhase), 0.0f), 1.0f);
            u.p1.z = std::remainder(finite_or(e.f(kBluePhase), 0.0f), 1.0f);
            u.p2.z = read(kSaturation, 1.0f, 0.0f, 8.0f);
            u.color = e.color(kTint);
        }
        return single_pass(ctx, ShaderId::effects_scanlines_frag, input, u, "varredura-senoidal", out);
    }

private:
    bool monochrome_;
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

class PhotographicGrain final : public Effect {
public:
    enum : u32 {
        kColorStrength, kColorFrequency, kColorOctaves, kMonoStrength, kMonoFrequency, kMonoOctaves,
        kRedFrequency, kGreenFrequency, kBlueFrequency, kSeed, kJitterFrames, kTint,
    };

    const EffectInfo& info() const noexcept override {
        static const EffectInfo i{"aurea.stylize.photographic_grain", "Grão fotográfico",
                                  "Estilizar", EffectClass::Neighborhood};
        return i;
    }
    void declare_parameters(ParameterRegistry& p) const override {
        p.add_float("color_strength", "Intensidade do grão de cor", 0.1f, 0.0f, 2.0f, kParamAnimatable);
        p.add_float("color_frequency", "Frequência do grão de cor", 100.0f, 0.1f, 1000.0f, kParamAnimatable);
        p.add_int("color_octaves", "Oitavas de cor", 1, 1, 10);
        p.add_float("mono_strength", "Intensidade do grão P&B", 0.0f, 0.0f, 2.0f, kParamAnimatable);
        p.add_float("mono_frequency", "Frequência do grão P&B", 100.0f, 0.1f, 1000.0f, kParamAnimatable);
        p.add_int("mono_octaves", "Oitavas P&B", 1, 1, 10);
        p.add_float("red_frequency", "Frequência do vermelho", 1.0f, 0.01f, 8.0f, kParamAnimatable);
        p.add_float("green_frequency", "Frequência do verde", 1.0f, 0.01f, 8.0f, kParamAnimatable);
        p.add_float("blue_frequency", "Frequência do azul", 1.0f, 0.01f, 8.0f, kParamAnimatable);
        p.add_float("seed", "Semente", 0.123f, 0.0f, 1024.0f, kParamAnimatable);
        p.add_int("jitter_frames", "Intervalo entre quadros", 1, 0, 300);
        p.add_color("tint", "Cor do grão", Vec4{1, 1, 1, 1});
    }
    bool is_identity(const EffectEval& e) const noexcept override {
        return e.f(kColorStrength) <= 0.0f && e.f(kMonoStrength) <= 0.0f;
    }
    void pipelines(std::vector<PipelineKey>& out, SurfaceFormat work) const override {
        out.push_back(PipelineKey::fullscreen(ShaderId::effects_grain_frag, work));
    }
    Status build(EffectBuildContext& ctx, const EffectEval& e, const LayerImage& input, f32,
                 LayerImage& out) const override {
        EffectUniforms u = base_uniforms(input);
        auto read = [&](u32 index, f32 fallback, f32 lo, f32 hi) {
            return std::clamp(finite_or(e.f(index), fallback), lo, hi);
        };
        const u32 seed = static_cast<u32>(std::round(read(kSeed, 0.123f, 0.0f, 1024.0f) * 1000000.0f));
        const u32 interval = static_cast<u32>(read(kJitterFrames, 1.0f, 0.0f, 300.0f));
        const u64 frame = interval == 0 ? 0u : static_cast<u64>(std::max<i64>(0, e.localTime.value)) / interval;
        // Split the uint32 in halves before passing through float uniforms.
        const u32 field = seed ^ (static_cast<u32>(frame) * 0x9e3779b9u)
                               ^ (static_cast<u32>(frame >> 32) * 0x85ebca6bu);
        u.p0 = {read(kColorStrength, 0.1f, 0.0f, 2.0f), read(kColorFrequency, 100.0f, 0.1f, 1000.0f),
                read(kColorOctaves, 1.0f, 1.0f, 10.0f), input.region.h / std::max(input.region.w, 1e-6f)};
        u.p1 = {read(kMonoStrength, 0.0f, 0.0f, 2.0f), read(kMonoFrequency, 100.0f, 0.1f, 1000.0f),
                read(kMonoOctaves, 1.0f, 1.0f, 10.0f), 0.0f};
        u.p2 = {read(kRedFrequency, 1.0f, 0.01f, 8.0f), read(kGreenFrequency, 1.0f, 0.01f, 8.0f),
                read(kBlueFrequency, 1.0f, 0.01f, 8.0f), 1.0f};
        u.p3 = {static_cast<f32>(field & 0xffffu), static_cast<f32>(field >> 16), 0.0f, 0.0f};
        u.color = e.color(kTint);
        return single_pass(ctx, ShaderId::effects_grain_frag, input, u, "grao-fotografico", out);
    }
};

class FilmProfileGrain final : public Effect {
public:
    const EffectInfo& info() const noexcept override {
        static const EffectInfo info{"aurea.stylize.film_profile_grain", "Grão de película",
                                     "Estilizar", EffectClass::Neighborhood};
        return info;
    }

    void declare_parameters(ParameterRegistry& p) const override {
        static const char* const profiles[] = {"Digital", "Negativo fino", "16 mm", "Prata P&B"};
        p.add_enum("profile", "Perfil de grão", profiles, 4, 0);
        p.add_float("amount", "Intensidade", 100.0f, 0.0f, 200.0f, kPercent, "%");
        p.add_float("size", "Tamanho do grão", 1.0f, 0.5f, 12.0f, kParamAnimatable | kParamPixels, "px");
        p.add_float("softness", "Suavidade", 100.0f, 0.0f, 200.0f, kPercent, "%");
        p.add_float("shadow_strength", "Grão nas sombras", 37.0f, 0.0f, 200.0f, kPercent, "%");
        p.add_float("midtone_strength", "Grão nos meios-tons", 100.0f, 0.0f, 200.0f, kPercent, "%");
        p.add_float("highlight_strength", "Grão nas luzes", 0.0f, 0.0f, 200.0f, kPercent, "%");
        p.add_float("red_strength", "Intensidade do vermelho", 100.0f, 0.0f, 200.0f, kPercent, "%");
        p.add_float("green_strength", "Intensidade do verde", 100.0f, 0.0f, 200.0f, kPercent, "%");
        p.add_float("blue_strength", "Intensidade do azul", 100.0f, 0.0f, 200.0f, kPercent, "%");
        p.add_float("red_midpoint", "Meio-tom do vermelho", 27.0f, 1.0f, 99.0f, kPercent, "%");
        p.add_float("green_midpoint", "Meio-tom do verde", 30.0f, 1.0f, 99.0f, kPercent, "%");
        p.add_float("blue_midpoint", "Meio-tom do azul", 31.0f, 1.0f, 99.0f, kPercent, "%");
        p.add_float("red_softness", "Suavidade do vermelho", 100.0f, 0.0f, 200.0f, kPercent, "%");
        p.add_float("green_softness", "Suavidade do verde", 100.0f, 0.0f, 200.0f, kPercent, "%");
        p.add_float("blue_softness", "Suavidade do azul", 100.0f, 0.0f, 200.0f, kPercent, "%");
        p.add_bool("monochrome", "Monocromático", false);
        p.add_int("seed", "Semente", 123, 0, 65535);
        p.add_bool("animate", "Grão animado", true);
        p.add_layer_ref("matte", "Camada de máscara");
        p.add_section_range("tones", "Resposta tonal", kDarks, kLights, kInvalidIndex, false);
        p.add_section_range("channels", "Canais de cor", kRed, kBlueSoftness, kInvalidIndex, false);
    }

    i32 input_layer_param() const noexcept override { return kMatte; }

    bool is_identity(const EffectEval& e) const noexcept override {
        return !(e.f(kAmount) > 0.0f)
            || (e.f(kDarks) <= 0.0f && e.f(kMids) <= 0.0f && e.f(kLights) <= 0.0f)
            || (e.f(kRed) <= 0.0f && e.f(kGreen) <= 0.0f && e.f(kBlue) <= 0.0f);
    }

    bool demo_values(EffectInstance&, std::vector<ParamValue>& values) const noexcept override {
        values[kProfile] = ParamValue::scalar(2.0f);
        values[kAmount] = ParamValue::scalar(175.0f);
        return true;
    }

    void pipelines(std::vector<PipelineKey>& out, SurfaceFormat work) const override {
        out.push_back(PipelineKey::fullscreen(ShaderId::effects_film_profile_grain_frag, work));
    }

    Status build(EffectBuildContext& ctx, const EffectEval& e, const LayerImage& input, f32,
                 LayerImage& out) const override {
        auto number = [&](u32 index, f32 fallback, f32 lower, f32 upper) {
            return std::clamp(finite_or(e.f(index), fallback), lower, upper);
        };
        auto percent = [&](u32 index, f32 fallback) {
            return number(index, fallback, 0.0f, 200.0f) * 0.01f;
        };
        const Profile& profile = profiles_[std::min(e.e(kProfile), 3u)];
        const u32 seed = static_cast<u32>(std::lround(number(kSeed, 123.0f, 0.0f, 65535.0f)));
        const u64 frame = e.b(kAnimate) ? static_cast<u64>(e.localTime.value) : 0u;
        const u32 field = seed ^ (static_cast<u32>(frame) * 0x9e3779b9u)
                              ^ (static_cast<u32>(frame >> 32) * 0x85ebca6bu);
        const u64 matteRef = e.value(kMatte).ref;
        const LayerImage* matte = ctx.layer_input(matteRef);
        if (matteRef && !matte) { out = input; return OkStatus; }
        const f32 strength = percent(kAmount, 100.0f) * profile.strength;
        const f32 softness = percent(kSoftness, 100.0f) * profile.softness;
        const bool mono = e.b(kMono) || profile.mono;
        Uniforms u{};
        u.region = {input.region.x, input.region.y, input.region.w, input.region.h};
        u.response = {percent(kDarks, 37.0f), percent(kMids, 100.0f), percent(kLights, 0.0f), 0.0f};
        u.midpoints = {number(kRedMid, 27.0f, 1.0f, 99.0f) * 0.01f, number(kGreenMid, 30.0f, 1.0f, 99.0f) * 0.01f,
                       number(kBlueMid, 31.0f, 1.0f, 99.0f) * 0.01f, 0.0f};
        u.gains = {strength * 0.0226f * percent(kRed, 100.0f), strength * 0.0210f * percent(kGreen, 100.0f),
                   strength * 0.0242f * percent(kBlue, 100.0f), 0.0f};
        u.sigma = {0.23f + 0.65f * softness * percent(kRedSoftness, 100.0f),
                   0.23f + (mono ? 0.65f : 0.63f) * softness * percent(kGreenSoftness, 100.0f),
                   0.23f + (mono ? 0.65f : 0.56f) * softness * percent(kBlueSoftness, 100.0f),
                   std::min(input.texel_scale_x(), input.texel_scale_y())};
        u.sampling = {number(kSize, 1.0f, 0.5f, 12.0f) * profile.size, static_cast<f32>(field & 0xffffu),
                      static_cast<f32>(field >> 16), mono ? 1.0f : 0.0f};
        if (mono) {
            const f32 gain = strength * 0.0226f;
            u.gains = {gain * percent(kRed, 100.0f), gain * percent(kGreen, 100.0f), gain * percent(kBlue, 100.0f), 0.0f};
        }
        if (matte) {
            const Mat4 m = e.placement ? e.placement->compFromLayer : Mat4::identity();
            u.matteRegion = {matte->region.x, matte->region.y, matte->region.w, matte->region.h};
            u.compX = {m.col[0].x, m.col[1].x, m.col[3].x, 0.0f};
            u.compY = {m.col[0].y, m.col[1].y, m.col[3].y, 0.0f};
            u.compW = {m.col[0].w, m.col[1].w, m.col[3].w, 0.0f};
        }
        out = LayerImage{ctx.texture("film-profile-grain", input.width, input.height), input.region, input.width, input.height};
        if (ctx.fullscreen_pass("film-profile-grain", PassStage::Effects, out.texture, ShaderId::effects_film_profile_grain_frag,
                                {PassTexture{input.texture}, PassTexture{matte ? matte->texture : input.texture, {},
                                                                         CommonSampler::LinearBorder}},
                                &u, sizeof(u)) == kInvalidIndex) return Errc::PipelineCompileFailed;
        return OkStatus;
    }

private:
    static constexpr u16 kPercent = kParamAnimatable | kParamPercent;
    enum : u32 {
        kProfile, kAmount, kSize, kSoftness, kDarks, kMids, kLights, kRed, kGreen, kBlue,
        kRedMid, kGreenMid, kBlueMid, kRedSoftness, kGreenSoftness, kBlueSoftness, kMono, kSeed, kAnimate, kMatte,
    };
    struct Profile { f32 strength, size, softness; bool mono; };
    static constexpr Profile profiles_[] = {
        {1.0f, 1.0f, 1.0f, false}, {0.7f, 0.8f, 1.15f, false},
        {1.35f, 1.65f, 0.86f, false}, {1.2f, 1.2f, 0.95f, true},
    };
    struct Uniforms { Vec4 region, response, midpoints, gains, sigma, sampling, matteRegion, compX, compY, compW; };
    static_assert(sizeof(Uniforms) == 160);
};

// -----------------------------------------------------------------------------
// Meio-tom — a geometria do COLOR HALFTONE do After Effects (Fase 9.3)
// -----------------------------------------------------------------------------
class Halftone final : public Effect {
public:
    // Os dez primeiros são os da Fase 7.3: o projeto salvo guarda os valores POR
    // POSIÇÃO, então os parâmetros novos só podem entrar no FIM da lista.
    enum : u32 {
        kCell = 0, kContrast, kAngle, kSoftness, kChannelAngle, kPattern, kSeparate, kPaper, kGain, kColor,
        kCenterX, kCenterY, kMaxRadius, kAngle1, kAngle2, kAngle3, kAngle4,
    };

    /// Os padrões do Color Halftone: o raio máximo e os quatro ângulos das
    /// retículas (1 = ciano, 2 = magenta, 3 = amarelo, 4 = preto).
    static constexpr f32 kAeMaxRadius = 5.0f;
    static constexpr f32 kAeAngle1 = 108.0f;
    static constexpr f32 kAeAngle2 = 162.0f;
    static constexpr f32 kAeAngle3 = 90.0f;
    static constexpr f32 kAeAngle4 = 45.0f;

    const EffectInfo& info() const noexcept override {
        static const EffectInfo i{effect_keys::kHalftone, "Meio-tom", "Estilizar", EffectClass::Neighborhood};
        return i;
    }
    void declare_parameters(ParameterRegistry& p) const override {
        static const char* const kPatterns[] = {"Ponto", "Linha", "Losango"};
        // O passo da grade é 2 × raio máximo no padrão: é a relação do AE, em
        // que o raio máximo é a MEIA-célula e os pontos chegam a se tocar.
        p.add_float("cell", "Passo da grade", 2.0f * kAeMaxRadius, 2.0f, 60.0f, kParamAnimatable | kParamPixels, "px");
        p.add_float("contrast", "Contraste", 30.0f, -100.0f, 200.0f, kParamAnimatable | kParamPercent, "%");
        // Legado: ângulo base, somado ao de cada canal. No padrão é 0 para que
        // os ângulos por canal valham exatamente os do AE.
        p.add_angle("angle", "Ângulo da grade", 0.0f);
        p.add_float("softness", "Suavidade da borda", 30.0f, 1.0f, 100.0f, kParamAnimatable | kParamPercent, "%");
        // Legado: escalona o ângulo por canal (0, +x, +2x…). No padrão é 0.
        p.add_angle("channel_angle", "Rotação por canal", 0.0f);
        p.add_enum("pattern", "Padrão", kPatterns, 3, 0);
        p.add_bool("separate_channels", "Grades separadas por canal", true);
        p.add_float("paper", "Fundo claro", 0.0f, 0.0f, 100.0f, kParamAnimatable | kParamPercent, "%");
        p.add_float("dot_gain", "Ganho do ponto", 50.0f, 0.0f, 100.0f, kParamAnimatable | kParamPercent, "%");
        p.add_color("tint", "Cor do papel", Vec4{1, 1, 1, 1});
        // --- Fase 9.3: o que faltava do Color Halftone ---
        // 0 num eixo = o centro da composição naquele eixo (o padrão do AE).
        p.add_float("center_x", "Centro X", 0.0f, -100000.0f, 100000.0f, kParamAnimatable | kParamPixels, "px");
        p.add_float("center_y", "Centro Y", 0.0f, -100000.0f, 100000.0f, kParamAnimatable | kParamPixels, "px");
        p.add_float("max_radius", "Raio máximo", kAeMaxRadius, 0.0f, 200.0f, kParamAnimatable | kParamPixels, "px");
        p.add_angle("angle_1", "Ângulo do canal 1", kAeAngle1);
        p.add_angle("angle_2", "Ângulo do canal 2", kAeAngle2);
        p.add_angle("angle_3", "Ângulo do canal 3", kAeAngle3);
        p.add_angle("angle_4", "Ângulo do canal 4", kAeAngle4);
    }
    bool is_identity(const EffectEval&) const noexcept override { return false; }
    f32 input_margin(const EffectEval& e) const noexcept override { return std::max(2.0f, e.f(kCell)); }
    bool demo_values(EffectInstance&, std::vector<ParamValue>& v) const noexcept override {
        // Uma grade mais graúda que a do padrão: é o que a cartela de 320×200
        // mostra de longe. O par passo = 2 × raio continua o do AE.
        v[kMaxRadius] = ParamValue::scalar(7.0f);
        v[kCell] = ParamValue::scalar(14.0f);
        v[kContrast] = ParamValue::scalar(45.0f);
        return true;
    }

    /// O centro da retícula em pixels da LAYER. `center_x`/`center_y` são em
    /// pixels da COMPOSIÇÃO (como no AE) e 0 num eixo significa "o centro da
    /// composição naquele eixo" — é o padrão de fábrica.
    static Vec2 center_px(const EffectEval& e) noexcept {
        Vec2 c{e.f(kCenterX), e.f(kCenterY)};
        const LayerPlacement* pl = e.placement;
        if (!pl) return c;
        if (c.x == 0.0f) c.x = static_cast<f32>(pl->compWidth) * 0.5f;
        if (c.y == 0.0f) c.y = static_cast<f32>(pl->compHeight) * 0.5f;
        // composição → layer: inverte a parte 2D afim de `compFromLayer`.
        const Mat4& m = pl->compFromLayer;
        const f32 a = m.col[0].x, b = m.col[0].y, cc = m.col[1].x, d = m.col[1].y;
        const f32 det = a * d - b * cc;
        if (std::fabs(det) < 1e-12f) return c;   // matriz degenerada: layer e comp no mesmo plano
        const f32 X = c.x - m.col[3].x, Y = c.y - m.col[3].y;
        return Vec2{(d * X - cc * Y) / det, (-b * X + a * Y) / det};
    }

    Status build(EffectBuildContext& ctx, const EffectEval& e, const LayerImage& input, f32 margin,
                 LayerImage& out) const override {
        // O ponto lê o centro da célula, que pode cair fora da região: a
        // margem de uma célula inteira garante que a última fileira de pontos
        // não fique com a cor errada.
        const f32 cell = std::max(2.0f, e.f(kCell));
        const Rect region = spread_region(input.region, cell * 4.0f, cell * 4.0f, e.placement, margin);
        u32 w = 0, h = 0;
        ctx.region_size(region, input.texel_scale_x(), w, h);
        // Texels por pixel de layer NESTA região — é o que faz o mesmo número
        // de pixels valer no preview reduzido e no export.
        const f32 sx = region.w > 0.0f ? static_cast<f32>(w) / region.w : 1.0f;
        const f32 sy = region.h > 0.0f ? static_cast<f32>(h) / region.h : 1.0f;
        const Vec2 center = center_px(e);

        EffectUniforms u;
        u.uvMap = EffectBuildContext::uv_map(region, input.region);
        u.texel = Vec4{w > 0 ? 1.0f / static_cast<f32>(w) : 0.0f, h > 0 ? 1.0f / static_cast<f32>(h) : 0.0f, sx, sy};
        u.p0 = Vec4{cell * sx, e.f(kContrast) / 100.0f, e.f(kAngle), e.f(kSoftness) / 100.0f};
        u.p1 = Vec4{e.f(kChannelAngle), static_cast<f32>(e.e(kPattern)), e.b(kSeparate) ? 1.0f : 0.0f,
                    e.f(kMaxRadius) * sx};
        u.p2 = Vec4{e.f(kPaper) / 100.0f, e.f(kGain) / 50.0f, (center.x - region.x) * sx, (center.y - region.y) * sy};
        u.p3 = Vec4{e.f(kAngle1), e.f(kAngle2), e.f(kAngle3), e.f(kAngle4)};
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
#include "MinimaxEffect.inl"

class RadialScreen final : public Effect {
public:
    explicit RadialScreen(bool color) noexcept : color_(color) {}

    enum : u32 {
        kFrequency, kAngle, kWidth, kSharpness, kLighten, kShift, kInkMode,
        kToneA, kToneB, kBlueShift, kSaturation,
    };

    const EffectInfo& info() const noexcept override {
        static const EffectInfo mono{"aurea.stylize.radial_screen", "Retícula radial",
                                     "Estilizar", EffectClass::Neighborhood};
        static const EffectInfo color{"aurea.stylize.radial_screen_color", "Retícula radial colorida",
                                      "Estilizar", EffectClass::Neighborhood};
        return color_ ? color : mono;
    }
    void declare_parameters(ParameterRegistry& p) const override {
        static const char* const kMonoModes[] = {"Preto", "Branco"};
        static const char* const kColorModes[] = {"CMY", "RGB"};
        p.add_float("frequency", "Frequência", 50.0f, 0.0f, 1000.0f, kParamAnimatable);
        p.add_angle("angle", "Ângulo", 30.0f);
        p.add_float("relative_width", "Largura relativa", 1.0f, 0.05f, 20.0f, kParamAnimatable);
        p.add_float("sharpness", "Nitidez", 4.0f, 0.0f, 40.0f, kParamAnimatable);
        p.add_float("lighten", "Clarear", 0.0f, -2.0f, 2.0f, kParamAnimatable);
        p.add_point2("shift", "Deslocamento", Vec2{0, 0}, -20.0f, 20.0f, kParamAnimatable);
        p.add_enum("ink_mode", "Cor dos pontos", color_ ? kColorModes : kMonoModes, 2, 0);
        if (color_) {
            p.add_point2("red_shift", "Deslocamento do vermelho", Vec2{0, 0.5f},
                         -20.0f, 20.0f, kParamAnimatable);
            p.add_point2("green_shift", "Deslocamento do verde", Vec2{0, 0},
                         -20.0f, 20.0f, kParamAnimatable);
            p.add_point2("blue_shift", "Deslocamento do azul", Vec2{0, -0.5f},
                         -20.0f, 20.0f, kParamAnimatable);
            p.add_float("saturation", "Saturação", 1.0f, -2.0f, 10.0f, kParamAnimatable);
        } else {
            p.add_color("dark", "Cor escura", Vec4{0, 0, 0, 1});
            p.add_color("light", "Cor clara", Vec4{1, 1, 1, 1});
        }
    }
    void pipelines(std::vector<PipelineKey>& out, SurfaceFormat work) const override {
        out.push_back(PipelineKey::fullscreen(ShaderId::effects_halftone_frag, work));
    }
    Status build(EffectBuildContext& ctx, const EffectEval& e, const LayerImage& input, f32,
                 LayerImage& out) const override {
        EffectUniforms u = base_uniforms(input);
        const auto read = [&](u32 index, f32 fallback, f32 lo, f32 hi) {
            return std::clamp(finite_or(e.f(index), fallback), lo, hi);
        };
        const auto shift = [&](u32 index, Vec2 fallback) {
            const Vec2 v = e.p2(index);
            return Vec2{finite_or(v.x, fallback.x), finite_or(v.y, fallback.y)};
        };
        const f32 angle = std::remainder(finite_or(e.f(kAngle), 30.0f), 360.0f) * kDeg2Rad;
        u.p0 = {read(kFrequency, 50.0f, 0.0f, 1000.0f), read(kWidth, 1.0f, 0.05f, 20.0f),
                angle, read(kSharpness, 4.0f, 0.0f, 40.0f)};
        u.p1 = {read(kLighten, 0.0f, -2.0f, 2.0f), color_ ? -2.0f : -1.0f,
                color_ ? read(kSaturation, 1.0f, -2.0f, 10.0f) : 0.0f,
                e.e(kInkMode) == 1 ? 1.0f : 0.0f};
        const Vec2 shiftAll = shift(kShift, Vec2{0, 0});
        u.p2 = {shiftAll.x, shiftAll.y, input.region.h / std::max(input.region.w, 1e-6f), 0.0f};
        if (color_) {
            const Vec2 r = shift(kToneA, Vec2{0, 0.5f});
            const Vec2 g = shift(kToneB, Vec2{0, 0});
            const Vec2 b = shift(kBlueShift, Vec2{0, -0.5f});
            u.p3 = {r.x, r.y, g.x, g.y};
            u.color = {b.x, b.y, 0, 0};
        } else {
            u.p3 = e.color(kToneA);
            u.color = e.color(kToneB);
        }
        return single_pass(ctx, ShaderId::effects_halftone_frag, input, u, "reticula-radial", out);
    }

private:
    bool color_;
};

class MedianFilter final : public Effect {
public:
    enum : u32 { kRadiusX, kRadiusY, kAmount };

    const EffectInfo& info() const noexcept override {
        static const EffectInfo info{"aurea.blur.median", "Filtro mediano", "Desfoque", EffectClass::Neighborhood};
        return info;
    }
    void declare_parameters(ParameterRegistry& p) const override {
        p.add_float("radius_x", "Raio X", 1.0f, 0.0f, 20.0f, kParamAnimatable | kParamPixels, "px");
        p.add_float("radius_y", "Raio Y", 1.0f, 0.0f, 20.0f, kParamAnimatable | kParamPixels, "px");
        p.add_float("amount", "Intensidade", 100.0f, 0.0f, 100.0f, kParamAnimatable | kParamPercent, "%");
    }
    bool is_identity(const EffectEval& e) const noexcept override {
        return input_margin(e) <= 0.0f || amount(e) <= 0.0f;
    }
    f32 input_margin(const EffectEval& e) const noexcept override {
        return std::max(radius(e, kRadiusX), radius(e, kRadiusY));
    }
    void pipelines(std::vector<PipelineKey>& out, SurfaceFormat work) const override {
        out.push_back(PipelineKey::fullscreen(ShaderId::effects_median_filter_frag, work));
    }
    Status build(EffectBuildContext& ctx, const EffectEval& e, const LayerImage& input, f32,
                 LayerImage& out) const override {
        EffectUniforms uniforms = base_uniforms(input);
        uniforms.p0 = Vec4{radius(e, kRadiusX) * uniforms.texel.z,
                           radius(e, kRadiusY) * uniforms.texel.w, amount(e), 0.0f};
        return single_pass(ctx, ShaderId::effects_median_filter_frag, input, uniforms, "filtro-mediano", out);
    }

private:
    static f32 radius(const EffectEval& e, u32 slot) noexcept {
        return std::clamp(finite_or(e.f(slot), 1.0f), 0.0f, 20.0f);
    }
    static f32 amount(const EffectEval& e) noexcept {
        return std::clamp(finite_or(e.f(kAmount), 100.0f) / 100.0f, 0.0f, 1.0f);
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

// Bounded spatial error diffusion. No history buffer: scrubbing is deterministic.
class OminoDiffusion final : public Effect {
public:
    const EffectInfo& info() const noexcept override {
        static const EffectInfo i{"aurea.stylize.omino_diffusion", "Omino Diffusion", "Estilizar", EffectClass::Neighborhood};
        return i;
    }
    void declare_parameters(ParameterRegistry& p) const override {
        static const char* const palettes[] = {"Preto e branco", "RGB 2", "RGB 3", "RGB 4", "4 cores"};
        p.add_float("amount", "Intensidade", 100, 0, 100, kParamAnimatable | kParamPercent, "%");
        p.add_float("error_weight", "Difusão", 1.2f, 0, 10, kParamAnimatable);
        p.add_float("angle", "Direção", 0, 0, 360, kParamAnimatable, "°");
        p.add_float("strength", "Alcance", 25, 0, 100, kParamAnimatable | kParamPercent, "%");
        p.add_int("steps", "Amostras", 24, 1, 64);
        p.add_float("stripe_width", "Largura das faixas", 1, 1, 32, kParamAnimatable | kParamPixels, "px");
        p.add_enum("palette", "Paleta", palettes, 5, 1);
        p.add_float("falloff", "Atenuação", 0, 0, 100, kParamAnimatable | kParamPercent, "%");
        p.add_color("color_a", "Cor 1", Vec4{0,0,0,1});
        p.add_color("color_b", "Cor 2", Vec4{0,1,1,1});
        p.add_color("color_c", "Cor 3", Vec4{1,0,1,1});
        p.add_color("color_d", "Cor 4", Vec4{1,1,1,1});
    }
    bool is_identity(const EffectEval& e) const noexcept override { return e.f(0) <= 0; }
    void pipelines(std::vector<PipelineKey>& out, SurfaceFormat work) const override {
        out.push_back(PipelineKey::fullscreen(ShaderId::effects_omino_diffusion_frag, work));
    }
    Status build(EffectBuildContext& ctx, const EffectEval& e, const LayerImage& input, f32,
                 LayerImage& out) const override {
        struct Uniforms { EffectUniforms base; Vec4 colorD; } u{};
        static_assert(sizeof(Uniforms) == 128);
        auto value = [&](u32 index, f32 lo, f32 hi) { return std::clamp(finite_or(e.f(index), lo), lo, hi); };
        u.base = base_uniforms(input);
        u.base.p0 = Vec4{value(0,0,100)/100, value(1,0,10), value(2,0,360)*0.01745329252f, value(3,0,100)/100};
        u.base.p1 = Vec4{value(4,1,64),value(5,1,32),value(6,0,4),value(7,0,100)/100};
        u.base.p2 = e.color(8); u.base.p3 = e.color(9); u.base.color = e.color(10); u.colorD = e.color(11);
        out = input;
        out.texture = ctx.texture("omino-diffusion", input.width, input.height);
        if (ctx.fullscreen_pass("omino-diffusion", PassStage::Effects, out.texture,
            ShaderId::effects_omino_diffusion_frag,
            {PassTexture{input.texture, {}, CommonSampler::LinearClamp}}, &u, sizeof(u)) == kInvalidIndex)
            return Errc::PipelineCompileFailed;
        return OkStatus;
    }
};

// Audio sends are serialized as regular effects, but never change video pixels.
class AudioSendEffect final : public Effect {
    u32 kind_;
public:
    explicit AudioSendEffect(u32 kind):kind_(kind) {}
    const EffectInfo& info() const noexcept override {
        static const EffectInfo infos[] = {
            {"aurea.audio.reverb","Reverb (envio antigo)","Áudio",EffectClass::PerPixel},
            {"aurea.audio.flanger","Flanger","Áudio",EffectClass::PerPixel},
            {"aurea.audio.echo","Echo / Delay","Áudio",EffectClass::PerPixel}};
        return infos[kind_];
    }
    void declare_parameters(ParameterRegistry& p) const override {
        p.add_float("wet","Nível do efeito",30,0,100,0,"%");
        p.add_float("delay","Tempo",kind_==1?3.f:120.f,kind_==1?.1f:10.f,kind_==1?10.f:500.f,0,"ms");
        if (kind_ == 1) {
            p.add_float("rate","Velocidade",.3f,.01f,5,0,"Hz");
            p.add_float("decay","Realimentação",50,0,90,0,"%");
        } else {
            p.add_float("decay","Decaimento",50,0,90,0,"%");
        }
    }
    bool is_identity(const EffectEval&) const noexcept override { return true; }
};

class PsykoColor final : public Effect {
public:
    explicit PsykoColor(bool stripes) noexcept : stripes_(stripes) {}

    enum : u32 { kAmount, kFrequency, kColorCycles, kPhase, kPhaseSpeed, kSourceDetail,
                 kSaturation, kBrightness, kSeedOrAngle, kPatternDetail };

    const EffectInfo& info() const noexcept override {
        static const EffectInfo blobs{"aurea.stylize.psyko_blobs", "PsykoBlobs",
                                      "Estilizar", EffectClass::Neighborhood};
        static const EffectInfo stripes{"aurea.stylize.psyko_stripes", "PsykoStripes",
                                        "Estilizar", EffectClass::Neighborhood};
        return stripes_ ? stripes : blobs;
    }

    void declare_parameters(ParameterRegistry& p) const override {
        p.add_float("amount", "Intensidade", 100.0f, 0.0f, 100.0f, kParamAnimatable | kParamPercent, "%");
        p.add_float("frequency", "Frequência", stripes_ ? 5.0f : 4.0f, 0.25f, 40.0f, kParamAnimatable);
        p.add_float("color_cycles", "Ciclos de cor", 3.0f, 0.25f, 15.0f, kParamAnimatable);
        p.add_float("phase", "Fase", 0.0f, -1.0f, 1.0f, kParamAnimatable);
        p.add_float("phase_speed", "Velocidade da fase", 0.35f, -4.0f, 4.0f, kParamNone, "Hz");
        p.add_float("source_detail", "Detalhe da imagem", 1.0f, 0.0f, 2.0f, kParamAnimatable);
        p.add_float("saturation", "Saturação", 1.0f, 0.0f, 2.0f, kParamAnimatable);
        p.add_float("brightness", "Brilho", 1.0f, 0.0f, 2.0f, kParamAnimatable);
        if (stripes_) {
            p.add_angle("angle", "Rotação do padrão", 35.0f);
        } else {
            p.add_int("seed", "Semente", 1, 0, 65535);
            p.add_int("pattern_detail", "Detalhe do padrão", 1, 1, 6);
        }
    }

    bool is_identity(const EffectEval& e) const noexcept override {
        return amount(e) <= 0.0f;
    }

    void pipelines(std::vector<PipelineKey>& out, SurfaceFormat work) const override {
        out.push_back(PipelineKey::fullscreen(ShaderId::effects_psyko_color_frag, work));
    }

    Status build(EffectBuildContext& ctx, const EffectEval& e, const LayerImage& input, f32,
                 LayerImage& out) const override {
        const auto read = [&](u32 index, f32 fallback, f32 low, f32 high) {
            return std::clamp(finite_or(e.f(index), fallback), low, high);
        };
        const f64 fps = e.framesPerSecond > 0.0 ? e.framesPerSecond : 30.0;
        const f64 seconds = e.time_frames() / fps;
        const f64 timePhase = std::isfinite(seconds) ? seconds * read(kPhaseSpeed, 0.35f, -4.0f, 4.0f) : 0.0;
        const f32 phase = static_cast<f32>(std::remainder(timePhase + read(kPhase, 0.0f, -1.0f, 1.0f), 1.0));

        const f32 width = e.placement && e.placement->layerWidth ? static_cast<f32>(e.placement->layerWidth) : input.region.w;
        const f32 height = e.placement && e.placement->layerHeight ? static_cast<f32>(e.placement->layerHeight) : input.region.h;
        const f32 unit = std::max(1.0f, std::min(width, height));
        EffectUniforms u = base_uniforms(input);
        u.p0 = {amount(e), read(kFrequency, stripes_ ? 5.0f : 4.0f, 0.25f, 40.0f),
                read(kColorCycles, 3.0f, 0.25f, 15.0f), read(kSourceDetail, 1.0f, 0.0f, 2.0f)};
        u.p1 = {phase, read(kSaturation, 1.0f, 0.0f, 2.0f),
                read(kBrightness, 1.0f, 0.0f, 2.0f), stripes_ ? 1.0f : 0.0f};
        u.p2 = {(input.region.x - 0.5f * width) / unit, (input.region.y - 0.5f * height) / unit,
                input.region.w / unit, input.region.h / unit};
        if (stripes_) {
            const f32 angle = std::remainder(finite_or(e.f(kSeedOrAngle), 35.0f), 360.0f) * kDeg2Rad;
            u.p3 = {std::cos(angle), std::sin(angle), 0.0f, 0.0f};
        } else {
            // Keep seed offsets in the exact-integer range of the shader's float coordinates.
            const u32 seed = static_cast<u32>(std::lround(read(kSeedOrAngle, 1.0f, 0.0f, 65535.0f)));
            u.p3 = {static_cast<f32>((seed * 251u) % 997u), static_cast<f32>((seed * 127u) % 991u),
                    std::round(read(kPatternDetail, 1.0f, 1.0f, 6.0f)), 0.0f};
        }
        return single_pass(ctx, ShaderId::effects_psyko_color_frag, input, u, "psyko-color", out);
    }

private:
    static f32 amount(const EffectEval& e) noexcept {
        return std::clamp(finite_or(e.f(kAmount), 100.0f) / 100.0f, 0.0f, 1.0f);
    }

    bool stripes_;
};

} // namespace

void register_stylize_effects(EffectRegistry& r) {
    for(u32 i=0;i<3;++i) (void)r.add(std::make_unique<AudioSendEffect>(i));
    (void)r.add(std::make_unique<OminoDiffusion>());
    (void)r.add(std::make_unique<Invert>());
    (void)r.add(std::make_unique<Scanlines>());
    (void)r.add(std::make_unique<SinusoidalScanlines>(false));
    (void)r.add(std::make_unique<SinusoidalScanlines>(true));
    (void)r.add(std::make_unique<PsykoColor>(false));
    (void)r.add(std::make_unique<PsykoColor>(true));
    (void)r.add(std::make_unique<Grain>());
    (void)r.add(std::make_unique<PhotographicGrain>());
    (void)r.add(std::make_unique<FilmProfileGrain>());
    (void)r.add(std::make_unique<Halftone>());
    (void)r.add(std::make_unique<RadialScreen>(false));
    (void)r.add(std::make_unique<RadialScreen>(true));
    (void)r.add(std::make_unique<Minimax>());
    (void)r.add(std::make_unique<MedianFilter>());
    (void)r.add(std::make_unique<UnsharpMask>());
}

} // namespace aurea::builtin
