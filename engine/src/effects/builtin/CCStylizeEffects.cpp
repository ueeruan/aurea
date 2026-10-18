// =============================================================================
//  Pacote CC — estilo, luz e cor.
//
//  Vector Blur · Light Burst · Star Field · Threshold RGB · Color Offset ·
//  Scatter · Blobby · Glass · Kernel · Spotlight · Drizzle
//
//  Referência de COMPORTAMENTO: CC Vector Blur, CC Light Burst 2.5, CC Star
//  Burst, CC Threshold RGB, CC Color Offset, CC Scatterize, CC Blobbylize,
//  CC Glass, CC Kernel, CC Spotlight e CC Drizzle. Implementação própria
//  (cc_*.frag); os nomes exibidos são genéricos e o nome "CC" fica na busca.
//
//  Custo fixo por pixel em todos: o laço mais longo (raios da Light Burst,
//  desfoque vetorial) é limitado e encolhe com a qualidade adaptativa da
//  prévia. Bolhas e Vidro fazem o gaussiano do motor (que reduz a imagem
//  para sigmas grandes) e um passe de luz.
// =============================================================================
#include "CCCommon.hpp"

#include <algorithm>
#include <cmath>

namespace aurea::builtin {
namespace {
using namespace cc;

/// Amostras de um laço do shader: `full` no export, menos na prévia adaptativa.
f32 adaptive_samples(EffectBuildContext& ctx, f32 full, f32 minimum) noexcept {
    const f32 q = std::clamp(ctx.resources().effect_quality(), 0.25f, 1.0f);
    return std::max(minimum, std::round(full * q));
}

Status same_region(EffectBuildContext& ctx, const EffectEval& e, ShaderId frag, const LayerImage& in, Uniforms& u,
                   const char* name, LayerImage& out, CommonSampler sampler = CommonSampler::LinearBorder) {
    return run_pass(ctx, e, frag, in, in.region, u, name, out, sampler);
}

/// A região de entrada escalada `k` vezes a partir de `c` (unida à entrada).
Rect scaled_about(const Rect& in, Vec2 c, f32 k) noexcept {
    const f32 x0 = c.x + (in.x - c.x) * k, x1 = c.x + (in.x + in.w - c.x) * k;
    const f32 y0 = c.y + (in.y - c.y) * k, y1 = c.y + (in.y + in.h - c.y) * k;
    const f32 l = std::min({in.x, x0, x1}), r = std::max({in.x + in.w, x0, x1});
    const f32 t = std::min({in.y, y0, y1}), b = std::max({in.y + in.h, y0, y1});
    return Rect{l, t, r - l, b - t};
}

// =============================================================================
// Vector Blur (CC Vector Blur)
// =============================================================================
class VectorBlur final : public Effect {
public:
    enum : u32 { kType = 0, kAmount, kAngle, kRidge, kProperty, kSoftness };
    const EffectInfo& info() const noexcept override {
        static const EffectInfo i{effect_keys::kVectorBlur, "Vector Blur", "Desfoque", EffectClass::Neighborhood};
        return i;
    }
    void declare_parameters(ParameterRegistry& p) const override {
        static const char* const types[] = {"Natural", "Comprimento constante", "Perpendicular"};
        static const char* const props[] = {"Luminância", "Vermelho", "Verde", "Azul", "Alfa"};
        p.add_enum("type", "Tipo", types, 3, 0);
        // O laço é fixo (até 24 amostras): o teto digitado é de legibilidade.
        p.add_float("amount", "Intensidade", 20.0f, 0.0f, 200.0f, kAnimPx, "px");
        p.typed_range(0.0f, 1000.0f);
        p.add_angle("angle_offset", "Ângulo", 0.0f);
        p.add_float("ridge_smoothness", "Suavidade das cristas", 20.0f, 0.0f, 100.0f, kAnimPct, "%");
        p.add_enum("property", "Propriedade", props, 5, 0);
        p.add_float("map_softness", "Suavidade do mapa", 4.0f, 0.0f, 100.0f, kAnimPx, "px");
        p.typed_range(0.0f, 500.0f);
    }
    bool is_identity(const EffectEval& e) const noexcept override { return finite_or(e.f(kAmount), 0.0f) < 0.01f; }
    f32 input_margin(const EffectEval& e) const noexcept override {
        return clampf(e.f(kAmount), 0.0f, 1000.0f, 20.0f) + 2.0f * clampf(e.f(kSoftness), 0.0f, 500.0f, 4.0f);
    }
    void pipelines(std::vector<PipelineKey>& out, SurfaceFormat work) const override {
        out.push_back(PipelineKey::fullscreen(ShaderId::effects_cc_vector_blur_frag, work));
    }
    Status build(EffectBuildContext& ctx, const EffectEval& e, const LayerImage& in, f32, LayerImage& out) const override {
        const f32 amount = clampf(e.f(kAmount), 0.0f, 1000.0f, 20.0f);
        // Uma amostra a cada ~1,5 texel do traço, entre 4 e 24.
        const f32 texels = amount * in.texel_scale_x();
        Uniforms u{};
        u.p0 = Vec4{amount, finite_or(e.f(kAngle), 0.0f) * kDeg, clampf(e.f(kRidge), 0.0f, 100.0f, 20.0f) / 100.0f,
                    clampf(e.f(kSoftness), 0.0f, 500.0f, 4.0f)};
        u.p1 = Vec4{static_cast<f32>(std::min(e.e(kType), 2u)), static_cast<f32>(std::min(e.e(kProperty), 4u)),
                    std::clamp(adaptive_samples(ctx, std::ceil(texels / 1.5f), 4.0f), 4.0f, 24.0f), 0.0f};
        return same_region(ctx, e, ShaderId::effects_cc_vector_blur_frag, in, u, "desfoque-vetorial", out);
    }
};

// =============================================================================
// Light Burst (CC Light Burst 2.5)
// =============================================================================
class LightBurst final : public Effect {
public:
    enum : u32 { kCenter = 0, kIntensity, kLength, kBurst, kSetColor, kColor };
    const EffectInfo& info() const noexcept override {
        static const EffectInfo i{effect_keys::kLightBurst, "Light Burst", "Luz", EffectClass::Neighborhood};
        return i;
    }
    void declare_parameters(ParameterRegistry& p) const override {
        static const char* const modes[] = {"Reta", "Esmaecer", "Centro"};
        p.add_point2("center", "Centro", Vec2{0.5f, 0.5f}, -1.0f, 2.0f, kAnimRel);
        p.typed_range(-10.0f, 10.0f);
        p.add_float("intensity", "Intensidade", 70.0f, 0.0f, 400.0f, kAnimPct, "%");
        p.typed_range(0.0f, 2000.0f);
        p.add_float("ray_length", "Comprimento dos raios", 50.0f, 0.0f, 95.0f, kAnimPct, "%");
        p.add_enum("burst", "Explosão", modes, 3, 0);
        p.add_bool("set_color", "Usar cor", false);
        p.add_color("color", "Cor", Vec4{1.0f, 0.85f, 0.55f, 1.0f});
    }
    bool is_identity(const EffectEval& e) const noexcept override {
        return finite_or(e.f(kIntensity), 0.0f) < 0.01f;
    }
    void pipelines(std::vector<PipelineKey>& out, SurfaceFormat work) const override {
        out.push_back(PipelineKey::fullscreen(ShaderId::effects_cc_light_burst_frag, work));
    }
    Status build(EffectBuildContext& ctx, const EffectEval& e, const LayerImage& in, f32 margin, LayerImage& out) const override {
        const Vec2 c = layer_point(e, in, kCenter);
        const f32 L = clampf(e.f(kLength), 0.0f, 95.0f, 50.0f) / 100.0f;
        Uniforms u{};
        u.p0 = Vec4{c.x, c.y, L, clampf(e.f(kIntensity), 0.0f, 2000.0f, 70.0f) / 100.0f};
        u.p1 = Vec4{static_cast<f32>(std::min(e.e(kBurst), 2u)), adaptive_samples(ctx, 24.0f, 8.0f),
                    e.b(kSetColor) ? 1.0f : 0.0f, 0.0f};
        const Vec4 col = premultiplied_linear(e.color(kColor));
        u.color = Vec4{col.x, col.y, col.z, 1.0f};
        // A luz de q chega a C + (q − C)/(1 − L): a entrada escalada a partir do centro.
        const f32 k = std::min(1.0f / std::max(1.0f - L, 0.05f), 16.0f);
        return run_pass(ctx, e, ShaderId::effects_cc_light_burst_frag, in,
                        grown_region(e, in.region, scaled_about(in.region, c, k), margin), u, "explosao-de-luz", out);
    }
};

// =============================================================================
// Star Field (CC Star Burst)
// =============================================================================
class StarField final : public Effect {
public:
    enum : u32 { kScatter = 0, kSpeed, kPhase, kSpacing, kSize, kBlend, kCenter, kSeed };
    static constexpr f32 kNear = 0.55f, kFar = 4.0f;
    const EffectInfo& info() const noexcept override {
        static const EffectInfo i{effect_keys::kStarField, "Star Field", "Estilizar", EffectClass::Domain};
        return i;
    }
    void declare_parameters(ParameterRegistry& p) const override {
        p.add_float("scatter", "Dispersão", 30.0f, 0.0f, 100.0f, kAnimPct, "%");
        // Ciclos por segundo: em 1, cada grade vai do fundo até quem vê em 1 s.
        p.add_float("speed", "Velocidade", 0.25f, -4.0f, 4.0f, kAnim);
        p.typed_range(-40.0f, 40.0f);
        p.add_float("phase", "Fase", 0.0f, -10.0f, 10.0f, kAnim);
        p.typed_range(-10000.0f, 10000.0f);
        p.add_float("grid_spacing", "Espaçamento da grade", 14.0f, 2.0f, 200.0f, kAnimPx, "px");
        p.typed_range(1.0f, 2000.0f);
        p.add_float("size", "Tamanho", 70.0f, 5.0f, 100.0f, kAnimPct, "%");
        p.add_float("blend", "Mistura com o original", 0.0f, 0.0f, 100.0f, kAnimPct, "%");
        p.add_point2("center", "Centro", Vec2{0.5f, 0.5f}, -1.0f, 2.0f, kAnimRel);
        p.typed_range(-10.0f, 10.0f);
        p.add_int("seed", "Semente", 0, 0, 9999);
    }
    bool is_identity(const EffectEval& e) const noexcept override { return finite_or(e.f(kBlend), 0.0f) >= 99.999f; }
    bool needs_full_input() const noexcept override { return true; }
    void pipelines(std::vector<PipelineKey>& out, SurfaceFormat work) const override {
        out.push_back(PipelineKey::fullscreen(ShaderId::effects_cc_star_field_frag, work));
    }
    Status build(EffectBuildContext& ctx, const EffectEval& e, const LayerImage& in, f32 margin, LayerImage& out) const override {
        const Vec2 c = layer_point(e, in, kCenter);
        // Espaçamento de pelo menos 2 texels (abaixo disso os pontos somem).
        const f32 spacing = std::max(clampf(e.f(kSpacing), 1.0f, 2000.0f, 14.0f), 2.0f / std::max(in.texel_scale_x(), 1e-3f));
        // Fase em que a grade 0 fica em repouso (z = 1) com tempo e fase zero.
        const f32 rest = (kFar - 1.0f) / (kFar - kNear);
        const f64 cycles = static_cast<f64>(seconds(e)) * static_cast<f64>(clampf(e.f(kSpeed), -40.0f, 40.0f, 0.25f))
                         + static_cast<f64>(clampf(e.f(kPhase), -10000.0f, 10000.0f, 0.0f)) + rest;
        Uniforms u{};
        u.p0 = Vec4{c.x, c.y, spacing, clampf(e.f(kSize), 5.0f, 100.0f, 70.0f) / 100.0f};
        u.p1 = Vec4{static_cast<f32>(cycles - std::floor(cycles)), clampf(e.f(kScatter), 0.0f, 100.0f, 30.0f) / 100.0f,
                    clampf(e.f(kBlend), 0.0f, 100.0f, 0.0f) / 100.0f,
                    static_cast<f32>(std::clamp(e.value(kSeed).as_int(), 0, 9999))};
        u.p2 = Vec4{kNear, kFar, 0.0f, 0.0f};
        return run_pass(ctx, e, ShaderId::effects_cc_star_field_frag, in,
                        grown_region(e, in.region, scaled_about(in.region, c, 1.0f / kNear), margin), u, "estrelas", out);
    }
};

// =============================================================================
// Threshold RGB (CC Threshold RGB) e Color Offset (CC Color Offset)
// =============================================================================
class ThresholdRgb final : public Effect {
public:
    enum : u32 { kRed = 0, kGreen, kBlue, kInvRed, kInvGreen, kInvBlue, kBlend };
    const EffectInfo& info() const noexcept override {
        static const EffectInfo i{effect_keys::kThresholdRgb, "Threshold RGB", "Cor", EffectClass::Neighborhood};
        return i;
    }
    void declare_parameters(ParameterRegistry& p) const override {
        p.add_float("red_threshold", "Limite do vermelho", 50.0f, 0.0f, 100.0f, kAnimPct, "%");
        p.add_float("green_threshold", "Limite do verde", 50.0f, 0.0f, 100.0f, kAnimPct, "%");
        p.add_float("blue_threshold", "Limite do azul", 50.0f, 0.0f, 100.0f, kAnimPct, "%");
        p.add_bool("invert_red", "Inverter vermelho", false);
        p.add_bool("invert_green", "Inverter verde", false);
        p.add_bool("invert_blue", "Inverter azul", false);
        p.add_float("blend", "Mistura com o original", 0.0f, 0.0f, 100.0f, kAnimPct, "%");
    }
    bool is_identity(const EffectEval& e) const noexcept override { return finite_or(e.f(kBlend), 0.0f) >= 99.999f; }
    void pipelines(std::vector<PipelineKey>& out, SurfaceFormat work) const override {
        out.push_back(PipelineKey::fullscreen(ShaderId::effects_cc_threshold_rgb_frag, work));
    }
    Status build(EffectBuildContext& ctx, const EffectEval& e, const LayerImage& in, f32, LayerImage& out) const override {
        Uniforms u{};
        u.p0 = Vec4{clampf(e.f(kRed), 0.0f, 100.0f, 50.0f) / 100.0f, clampf(e.f(kGreen), 0.0f, 100.0f, 50.0f) / 100.0f,
                    clampf(e.f(kBlue), 0.0f, 100.0f, 50.0f) / 100.0f, clampf(e.f(kBlend), 0.0f, 100.0f, 0.0f) / 100.0f};
        u.p1 = Vec4{e.b(kInvRed) ? 1.0f : 0.0f, e.b(kInvGreen) ? 1.0f : 0.0f, e.b(kInvBlue) ? 1.0f : 0.0f, 0.0f};
        return same_region(ctx, e, ShaderId::effects_cc_threshold_rgb_frag, in, u, "limiar-rgb", out);
    }
};

class ColorOffset final : public Effect {
public:
    enum : u32 { kRed = 0, kGreen, kBlue, kOverflow, kBlend };
    const EffectInfo& info() const noexcept override {
        static const EffectInfo i{effect_keys::kColorOffset, "Color Offset", "Cor", EffectClass::Neighborhood};
        return i;
    }
    void declare_parameters(ParameterRegistry& p) const override {
        static const char* const modes[] = {"Repetir", "Solarizar", "Polarizar"};
        p.add_angle("red_phase", "Fase do vermelho", 120.0f);
        p.add_angle("green_phase", "Fase do verde", 0.0f);
        p.add_angle("blue_phase", "Fase do azul", 240.0f);
        p.add_enum("overflow", "Estouro", modes, 3, 0);
        p.add_float("blend", "Mistura com o original", 0.0f, 0.0f, 100.0f, kAnimPct, "%");
    }
    bool is_identity(const EffectEval& e) const noexcept override {
        if (finite_or(e.f(kBlend), 0.0f) >= 99.999f) return true;
        if (e.e(kOverflow) == 2u) return false;
        const f32 period = e.e(kOverflow) == 1u ? 720.0f : 360.0f;
        for (const u32 k : {kRed, kGreen, kBlue}) {
            if (std::fabs(finite_or(e.f(k), 0.0f)) > 1e-3f) {
                if (e.e(kOverflow) == 1u || std::fabs(std::remainder(e.f(k), period)) > 1e-3f) return false;
            }
        }
        return true;
    }
    void pipelines(std::vector<PipelineKey>& out, SurfaceFormat work) const override {
        out.push_back(PipelineKey::fullscreen(ShaderId::effects_cc_color_offset_frag, work));
    }
    Status build(EffectBuildContext& ctx, const EffectEval& e, const LayerImage& in, f32, LayerImage& out) const override {
        // Voltas, reduzidas ao período (a conta do shader fica em meia precisão boa).
        auto turns = [&](u32 k) {
            const f32 t = finite_or(e.f(k), 0.0f) / 360.0f;
            return t - 2.0f * std::floor(t * 0.5f);
        };
        Uniforms u{};
        u.p0 = Vec4{turns(kRed), turns(kGreen), turns(kBlue), static_cast<f32>(std::min(e.e(kOverflow), 2u))};
        u.p1 = Vec4{clampf(e.f(kBlend), 0.0f, 100.0f, 0.0f) / 100.0f, 0.0f, 0.0f, 0.0f};
        return same_region(ctx, e, ShaderId::effects_cc_color_offset_frag, in, u, "deslocar-cor", out);
    }
};

// =============================================================================
// Scatter (CC Scatterize)
// =============================================================================
class Scatter final : public Effect {
public:
    enum : u32 { kAmount = 0, kRightTwist, kLeftTwist, kMode, kSeed };
    const EffectInfo& info() const noexcept override {
        static const EffectInfo i{effect_keys::kScatter, "Scatter", "Estilizar", EffectClass::Neighborhood};
        return i;
    }
    void declare_parameters(ParameterRegistry& p) const override {
        static const char* const modes[] = {"Substituir", "Sobre o original", "Atrás do original", "Somar"};
        p.add_float("scatter", "Dispersão", 25.0f, 0.0f, 300.0f, kAnimPx, "px");
        p.typed_range(0.0f, 2000.0f);
        p.add_angle("right_twist", "Torção à direita", 0.0f);
        p.add_angle("left_twist", "Torção à esquerda", 0.0f);
        p.add_enum("transfer_mode", "Modo", modes, 4, 0);
        p.add_int("seed", "Semente", 0, 0, 9999);
    }
    bool is_identity(const EffectEval& e) const noexcept override {
        return finite_or(e.f(kAmount), 0.0f) < 0.01f && std::fabs(finite_or(e.f(kRightTwist), 0.0f)) < 0.01f
            && std::fabs(finite_or(e.f(kLeftTwist), 0.0f)) < 0.01f && e.e(kMode) == 0u;
    }
    f32 input_margin(const EffectEval& e) const noexcept override { return clampf(e.f(kAmount), 0.0f, 2000.0f, 25.0f); }
    bool needs_full_input() const noexcept override { return true; }
    void pipelines(std::vector<PipelineKey>& out, SurfaceFormat work) const override {
        out.push_back(PipelineKey::fullscreen(ShaderId::effects_cc_scatter_frag, work));
    }
    Status build(EffectBuildContext& ctx, const EffectEval& e, const LayerImage& in, f32 margin, LayerImage& out) const override {
        const f32 amount = clampf(e.f(kAmount), 0.0f, 2000.0f, 25.0f);
        Uniforms u{};
        u.p0 = Vec4{amount, finite_or(e.f(kLeftTwist), 0.0f) * kDeg, finite_or(e.f(kRightTwist), 0.0f) * kDeg,
                    static_cast<f32>(std::min(e.e(kMode), 3u))};
        u.p1 = Vec4{static_cast<f32>(std::clamp(e.value(kSeed).as_int(), 0, 9999)), 0.0f, 0.0f, 0.0f};
        const Rect want{in.region.x - amount, in.region.y - amount, in.region.w + 2.0f * amount, in.region.h + 2.0f * amount};
        return run_pass(ctx, e, ShaderId::effects_cc_scatter_frag, in, grown_region(e, in.region, want, margin), u,
                        "espalhar", out);
    }
};

// =============================================================================
// Blobby (CC Blobbylize) e Glass (CC Glass): gaussiano do motor + passe de luz
// =============================================================================
Status blurred_map(EffectBuildContext& ctx, const LayerImage& in, f32 sigma, const char* label, LayerImage& out) {
    BlurRequest req;
    req.sigmaX = sigma;
    req.sigmaY = sigma;
    req.repeatEdges = false;
    req.outRegion = in.region;
    req.label = label;
    return build_gaussian(ctx, in, req, out);
}

class Blobby final : public Effect {
public:
    enum : u32 { kProperty = 0, kSoftness, kCut, kLight };
    const EffectInfo& info() const noexcept override {
        static const EffectInfo i{effect_keys::kBlobby, "Blobby", "Estilizar", EffectClass::Neighborhood};
        return i;
    }
    void declare_parameters(ParameterRegistry& p) const override {
        static const char* const props[] = {"Luminância", "Alfa"};
        p.add_enum("property", "Propriedade", props, 2, 0);
        p.add_float("softness", "Suavidade", 10.0f, 0.5f, 100.0f, kAnimPx, "px");
        p.typed_range(0.5f, 500.0f);
        p.add_float("cut_away", "Corte", 30.0f, 0.0f, 100.0f, kAnimPct, "%");
        declare_light(p, LightDefaults{100.0f, -45.0f, 50.0f, 45.0f, 60.0f, 60.0f, 20.0f, 60.0f});
    }
    f32 input_margin(const EffectEval& e) const noexcept override { return 3.0f * clampf(e.f(kSoftness), 0.5f, 500.0f, 10.0f) + 2.0f; }
    void pipelines(std::vector<PipelineKey>& out, SurfaceFormat work) const override {
        out.push_back(PipelineKey::fullscreen(ShaderId::effects_cc_blobby_frag, work));
    }
    Status build(EffectBuildContext& ctx, const EffectEval& e, const LayerImage& in, f32, LayerImage& out) const override {
        const f32 sigma = clampf(e.f(kSoftness), 0.5f, 500.0f, 10.0f);
        LayerImage map;
        if (const Status s = blurred_map(ctx, in, sigma, "bolhas-mapa", map); !s.ok()) return s;
        Uniforms u{};
        u.p0 = Vec4{static_cast<f32>(std::min(e.e(kProperty), 1u)), clampf(e.f(kCut), 0.0f, 100.0f, 30.0f) / 100.0f,
                    1.5f * sigma, 0.0f};
        u.p3 = Vec4{map.region.x, map.region.y, map.region.w, map.region.h};
        light_uniforms(e, kLight, u);
        return run_pass(ctx, e, ShaderId::effects_cc_blobby_frag, in, in.region, u, "bolhas", out,
                        CommonSampler::LinearBorder, &map);
    }
};

class Glass final : public Effect {
public:
    enum : u32 { kProperty = 0, kSoftness, kHeight, kDisplacement, kLight };
    const EffectInfo& info() const noexcept override {
        static const EffectInfo i{effect_keys::kGlass, "Glass", "Estilizar", EffectClass::Neighborhood};
        return i;
    }
    void declare_parameters(ParameterRegistry& p) const override {
        static const char* const props[] = {"Luminância", "Alfa", "Vermelho", "Verde", "Azul"};
        p.add_enum("property", "Propriedade", props, 5, 0);
        p.add_float("softness", "Suavidade", 12.0f, 0.5f, 100.0f, kAnimPx, "px");
        p.typed_range(0.5f, 500.0f);
        p.add_float("height", "Altura", 100.0f, -200.0f, 200.0f, kAnimPct, "%");
        p.typed_range(-1000.0f, 1000.0f);
        p.add_float("displacement", "Deslocamento", 15.0f, -200.0f, 200.0f, kAnimPx, "px");
        p.typed_range(-2000.0f, 2000.0f);
        declare_light(p, LightDefaults{100.0f, -45.0f, 50.0f, 85.0f, 25.0f, 70.0f, 10.0f, 0.0f});
    }
    f32 input_margin(const EffectEval& e) const noexcept override {
        return 3.0f * clampf(e.f(kSoftness), 0.5f, 500.0f, 12.0f) + std::fabs(clampf(e.f(kDisplacement), -2000.0f, 2000.0f, 15.0f)) + 2.0f;
    }
    void pipelines(std::vector<PipelineKey>& out, SurfaceFormat work) const override {
        out.push_back(PipelineKey::fullscreen(ShaderId::effects_cc_glass_frag, work));
    }
    Status build(EffectBuildContext& ctx, const EffectEval& e, const LayerImage& in, f32, LayerImage& out) const override {
        const f32 sigma = clampf(e.f(kSoftness), 0.5f, 500.0f, 12.0f);
        LayerImage map;
        if (const Status s = blurred_map(ctx, in, sigma, "vidro-mapa", map); !s.ok()) return s;
        Uniforms u{};
        // Relevo: a inclinação do mapa por px × (altura · 4σ) — independente da suavidade.
        u.p0 = Vec4{static_cast<f32>(std::min(e.e(kProperty), 4u)),
                    clampf(e.f(kHeight), -1000.0f, 1000.0f, 100.0f) / 100.0f * 4.0f * sigma,
                    clampf(e.f(kDisplacement), -2000.0f, 2000.0f, 15.0f), 0.0f};
        u.p3 = Vec4{map.region.x, map.region.y, map.region.w, map.region.h};
        light_uniforms(e, kLight, u);
        return run_pass(ctx, e, ShaderId::effects_cc_glass_frag, in, in.region, u, "vidro", out,
                        CommonSampler::LinearClamp, &map);
    }
};

// =============================================================================
// Kernel (CC Kernel)
// =============================================================================
class Kernel final : public Effect {
public:
    enum : u32 { kW0 = 0, kDivisor = 9, kDistance, kBlend };
    const EffectInfo& info() const noexcept override {
        static const EffectInfo i{effect_keys::kKernel, "Kernel", "Estilizar", EffectClass::Neighborhood};
        return i;
    }
    void declare_parameters(ParameterRegistry& p) const override {
        // Padrão: nitidez (centro 5, cruz −1).
        static const char* const ids[9] = {"w1", "w2", "w3", "w4", "w5", "w6", "w7", "w8", "w9"};
        static const char* const labels[9] = {"Peso 1", "Peso 2", "Peso 3", "Peso 4", "Peso 5",
                                              "Peso 6", "Peso 7", "Peso 8", "Peso 9"};
        static const f32 defaults[9] = {0.0f, -1.0f, 0.0f, -1.0f, 5.0f, -1.0f, 0.0f, -1.0f, 0.0f};
        for (u32 i = 0; i < 9; ++i) {
            p.add_float(ids[i], labels[i], defaults[i], -20.0f, 20.0f, kAnim);
            p.typed_range(-1000.0f, 1000.0f);
        }
        // 0 = automático (a soma dos pesos).
        p.add_float("divisor", "Divisor", 0.0f, -100.0f, 100.0f, kAnim);
        p.typed_range(-10000.0f, 10000.0f);
        p.add_float("distance", "Distância", 1.0f, 0.5f, 20.0f, kAnimPx, "px");
        p.typed_range(0.1f, 200.0f);
        p.add_float("blend", "Mistura com o original", 0.0f, 0.0f, 100.0f, kAnimPct, "%");
    }
    static f32 weight(const EffectEval& e, u32 i) noexcept { return clampf(e.f(kW0 + i), -1000.0f, 1000.0f, 0.0f); }
    bool is_identity(const EffectEval& e) const noexcept override {
        if (finite_or(e.f(kBlend), 0.0f) >= 99.999f) return true;
        for (u32 i = 0; i < 9; ++i) if (i != 4 && std::fabs(weight(e, i)) > 1e-6f) return false;
        const f32 div = finite_or(e.f(kDivisor), 0.0f);
        return std::fabs(weight(e, 4)) > 1e-6f && (std::fabs(div) < 1e-6f || std::fabs(div - weight(e, 4)) < 1e-6f);
    }
    f32 input_margin(const EffectEval& e) const noexcept override { return clampf(e.f(kDistance), 0.1f, 200.0f, 1.0f) + 1.0f; }
    void pipelines(std::vector<PipelineKey>& out, SurfaceFormat work) const override {
        out.push_back(PipelineKey::fullscreen(ShaderId::effects_cc_kernel_frag, work));
    }
    Status build(EffectBuildContext& ctx, const EffectEval& e, const LayerImage& in, f32, LayerImage& out) const override {
        f32 sum = 0.0f;
        for (u32 i = 0; i < 9; ++i) sum += weight(e, i);
        f32 div = clampf(e.f(kDivisor), -10000.0f, 10000.0f, 0.0f);
        if (std::fabs(div) < 1e-6f) div = std::fabs(sum) > 1e-6f ? sum : 1.0f;
        // Um texel de distância no mínimo: menos que isso não filtra nada.
        const f32 dist = std::max(clampf(e.f(kDistance), 0.1f, 200.0f, 1.0f), 1.0f / std::max(in.texel_scale_x(), 1e-3f));
        Uniforms u{};
        u.p0 = Vec4{weight(e, 0), weight(e, 1), weight(e, 2), 1.0f / div};
        u.p1 = Vec4{weight(e, 3), weight(e, 4), weight(e, 5), dist};
        u.p2 = Vec4{weight(e, 6), weight(e, 7), weight(e, 8), clampf(e.f(kBlend), 0.0f, 100.0f, 0.0f) / 100.0f};
        return same_region(ctx, e, ShaderId::effects_cc_kernel_frag, in, u, "nucleo", out, CommonSampler::LinearClamp);
    }
};

// =============================================================================
// Spotlight (CC Spotlight)
// =============================================================================
class Spotlight final : public Effect {
public:
    enum : u32 { kFrom = 0, kTo, kHeight, kCone, kSoftness, kColor, kIntensity, kAmbient, kRender };
    const EffectInfo& info() const noexcept override {
        static const EffectInfo i{effect_keys::kSpotlight, "Spotlight", "Luz", EffectClass::Neighborhood};
        return i;
    }
    void declare_parameters(ParameterRegistry& p) const override {
        static const char* const modes[] = {"Luz sobre a camada", "Só a luz"};
        p.add_point2("from", "Origem", Vec2{0.2f, 0.1f}, -1.0f, 2.0f, kAnimRel);
        p.typed_range(-10.0f, 10.0f);
        p.add_point2("to", "Alvo", Vec2{0.5f, 0.55f}, -1.0f, 2.0f, kAnimRel);
        p.typed_range(-10.0f, 10.0f);
        p.add_float("height", "Altura", 80.0f, 1.0f, 400.0f, kAnimPct, "%");
        p.typed_range(0.5f, 2000.0f);
        p.add_angle("cone_angle", "Abertura do cone", 40.0f, 1.0f, 170.0f);
        p.add_float("edge_softness", "Suavidade da borda", 40.0f, 0.0f, 100.0f, kAnimPct, "%");
        p.add_color("color", "Cor", Vec4{1.0f, 0.95f, 0.85f, 1.0f});
        p.add_float("intensity", "Intensidade", 100.0f, 0.0f, 400.0f, kAnimPct, "%");
        p.typed_range(0.0f, 2000.0f);
        p.add_float("ambient", "Ambiente", 15.0f, 0.0f, 100.0f, kAnimPct, "%");
        p.add_enum("render", "Renderizar", modes, 2, 0);
    }
    void pipelines(std::vector<PipelineKey>& out, SurfaceFormat work) const override {
        out.push_back(PipelineKey::fullscreen(ShaderId::effects_cc_spotlight_frag, work));
    }
    Status build(EffectBuildContext& ctx, const EffectEval& e, const LayerImage& in, f32, LayerImage& out) const override {
        const Rect box = layer_box(e, in);
        const Vec2 a = layer_point(e, in, kFrom), b = layer_point(e, in, kTo);
        Uniforms u{};
        u.p0 = Vec4{a.x, a.y, b.x, b.y};
        u.p1 = Vec4{clampf(e.f(kHeight), 0.5f, 2000.0f, 80.0f) / 100.0f * std::max(box.h, 1.0f),
                    0.5f * clampf(e.f(kCone), 1.0f, 170.0f, 40.0f) * kDeg, clampf(e.f(kSoftness), 0.0f, 100.0f, 40.0f) / 100.0f,
                    clampf(e.f(kIntensity), 0.0f, 2000.0f, 100.0f) / 100.0f};
        u.p2 = Vec4{clampf(e.f(kAmbient), 0.0f, 100.0f, 15.0f) / 100.0f, static_cast<f32>(std::min(e.e(kRender), 1u)), 0.0f, 0.0f};
        const Vec4 c = premultiplied_linear(e.color(kColor));
        u.color = Vec4{c.x, c.y, c.z, 1.0f};
        return same_region(ctx, e, ShaderId::effects_cc_spotlight_frag, in, u, "holofote", out);
    }
};

// =============================================================================
// Drizzle (CC Drizzle)
// =============================================================================
class Drizzle final : public Effect {
public:
    enum : u32 { kRate = 0, kLife, kRings, kRadius, kDisplacement, kHighlight, kSeed };
    const EffectInfo& info() const noexcept override {
        static const EffectInfo i{effect_keys::kDrizzle, "Drizzle", "Distorcer", EffectClass::Neighborhood};
        return i;
    }
    void declare_parameters(ParameterRegistry& p) const override {
        p.add_float("drip_rate", "Gotas por segundo", 1.0f, 0.05f, 10.0f, kAnim);
        p.typed_range(0.01f, 60.0f);
        p.add_float("longevity", "Duração", 1.5f, 0.1f, 10.0f, kAnim, "s");
        p.add_float("rippling", "Anéis", 3.0f, 1.0f, 10.0f, kAnim);
        p.add_float("radius", "Raio da onda", 60.0f, 5.0f, 400.0f, kAnimPx, "px");
        p.typed_range(2.0f, 4000.0f);
        p.add_float("displacement", "Deslocamento", 6.0f, 0.0f, 50.0f, kAnimPx, "px");
        p.typed_range(0.0f, 500.0f);
        p.add_float("ripple_height", "Altura da onda", 50.0f, 0.0f, 200.0f, kAnimPct, "%");
        p.add_int("seed", "Semente", 0, 0, 9999);
    }
    bool is_identity(const EffectEval& e) const noexcept override {
        return finite_or(e.f(kDisplacement), 0.0f) < 0.001f && finite_or(e.f(kHighlight), 0.0f) < 0.01f;
    }
    f32 input_margin(const EffectEval& e) const noexcept override { return clampf(e.f(kDisplacement), 0.0f, 500.0f, 6.0f) + 1.0f; }
    bool demo_values(EffectInstance&, std::vector<ParamValue>& v) const noexcept override {
        // A prévia é um quadro só: gotas mais densas e mais fortes para aparecer.
        v[kRadius] = ParamValue::scalar(40.0f);
        v[kDisplacement] = ParamValue::scalar(10.0f);
        v[kRate] = ParamValue::scalar(2.0f);
        return true;
    }
    void pipelines(std::vector<PipelineKey>& out, SurfaceFormat work) const override {
        out.push_back(PipelineKey::fullscreen(ShaderId::effects_cc_drizzle_frag, work));
    }
    Status build(EffectBuildContext& ctx, const EffectEval& e, const LayerImage& in, f32, LayerImage& out) const override {
        // Raio de pelo menos 4 texels (a célula da gota não fica menor que o anel).
        const f32 radius = std::max(clampf(e.f(kRadius), 2.0f, 4000.0f, 60.0f), 4.0f / std::max(in.texel_scale_x(), 1e-3f));
        Uniforms u{};
        u.p0 = Vec4{radius, clampf(e.f(kRate), 0.01f, 60.0f, 1.0f), clampf(e.f(kLife), 0.1f, 10.0f, 1.5f),
                    clampf(e.f(kRings), 1.0f, 10.0f, 3.0f)};
        u.p1 = Vec4{clampf(e.f(kDisplacement), 0.0f, 500.0f, 6.0f), clampf(e.f(kHighlight), 0.0f, 200.0f, 50.0f) / 100.0f,
                    static_cast<f32>(std::clamp(e.value(kSeed).as_int(), 0, 9999)), seconds(e)};
        return same_region(ctx, e, ShaderId::effects_cc_drizzle_frag, in, u, "garoa", out);
    }
};

} // namespace

void register_cc_stylize_effects(EffectRegistry& r) {
    (void)r.add(std::make_unique<VectorBlur>());
    (void)r.add(std::make_unique<LightBurst>());
    (void)r.add(std::make_unique<StarField>());
    (void)r.add(std::make_unique<ThresholdRgb>());
    (void)r.add(std::make_unique<ColorOffset>());
    (void)r.add(std::make_unique<Scatter>());
    (void)r.add(std::make_unique<Blobby>());
    (void)r.add(std::make_unique<Glass>());
    (void)r.add(std::make_unique<Kernel>());
    (void)r.add(std::make_unique<Spotlight>());
    (void)r.add(std::make_unique<Drizzle>());
}

} // namespace aurea::builtin
