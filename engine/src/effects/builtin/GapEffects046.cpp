// =============================================================================
//  Aurea / effects / builtin / GapEffects046.cpp
//
//  0.4.6: efeitos que faltavam frente às referências de motion no celular
//  (levantamento em build/reference/alight-motion-5.0.280/gap-aurea-2026-11-02.md).
//  Especificados pela FUNÇÃO (o que o efeito faz na imagem), escritos do zero:
//  Redemoinho, Coordenadas polares, Túnel, Lupa (Distorcer); Sombra longa,
//  Bordas ásperas, Curvas de nível (Estilizar); Brilho interno (Luz); Raio e
//  Células (Gerar). Um passe cada, nos uniforms padrão dos efeitos.
// =============================================================================
#include "BuiltinEffects.hpp"

#include <algorithm>
#include <cmath>

namespace aurea::builtin {
namespace {

constexpr const char* kSwirlKey = "aurea.distort.swirl";
constexpr const char* kPolarKey = "aurea.distort.polar_coordinates";
constexpr const char* kTunnelKey = "aurea.distort.tunnel";
constexpr const char* kMagnifyKey = "aurea.distort.magnify";
constexpr const char* kLongShadowKey = "aurea.stylize.long_shadow";
constexpr const char* kRoughenKey = "aurea.stylize.roughen_edges";
constexpr const char* kContourKey = "aurea.stylize.contour_lines";
constexpr const char* kInnerGlowKey = "aurea.light.inner_glow";
constexpr const char* kLightningKey = "aurea.generate.lightning";
constexpr const char* kVoronoiKey = "aurea.generate.voronoi_cells";

f32 finite_or(f32 v, f32 fallback) noexcept { return std::isfinite(v) ? v : fallback; }

f32 pct(const EffectEval& e, u32 i, f32 lo = 0.0f, f32 hi = 1.0f) {
    return std::clamp(finite_or(e.f(i), 0.0f) * 0.01f, lo, hi);
}

/// Tamanho natural da camada (px); sem posicionamento, a região da entrada.
Vec2 layer_size(const EffectEval& e, const LayerImage& input) noexcept {
    if (e.placement && e.placement->layerWidth && e.placement->layerHeight)
        return Vec2{static_cast<f32>(e.placement->layerWidth), static_cast<f32>(e.placement->layerHeight)};
    return Vec2{std::max(input.region.w, 1e-4f), std::max(input.region.h, 1e-4f)};
}

/// Os pontos relativos (centro) são da CAMADA; o shader mede no uv da REGIÃO da
/// entrada, que um efeito anterior (sombra, brilho) pode ter aumentado. Leva o
/// ponto ao uv da região e devolve a escala de "fração da altura da camada"
/// para "fração da altura da região". Região = caixa da camada: identidade.
Vec2 region_uv(const EffectEval& e, const LayerImage& input, Vec2 rel, f32& heightScale) noexcept {
    const Rect& r = input.region;
    const Vec2 ls = layer_size(e, input);
    heightScale = 1.0f;
    // Depois do Motion Tile a PAREDE é o plano (o efeito chega aos ladrilhos):
    // o centro relativo continua medido nela, como antes.
    if (input.tiled) return rel;
    if (r.x == 0.0f && r.y == 0.0f && r.w == ls.x && r.h == ls.y) return rel;
    heightScale = ls.y / std::max(r.h, 1e-4f);
    return Vec2{(rel.x * ls.x - r.x) / std::max(r.w, 1e-4f), (rel.y * ls.y - r.y) / std::max(r.h, 1e-4f)};
}

/// Um passe na região da entrada (saída do mesmo tamanho).
Status same_region_pass(EffectBuildContext& ctx, const LayerImage& input, const EffectUniforms& u, ShaderId shader,
                        const char* name, CommonSampler sampler, LayerImage& out) {
    out = input;
    out.texture = ctx.texture(name, input.width, input.height);
    if (ctx.fullscreen_pass(name, PassStage::Effects, out.texture, shader,
                            {PassTexture{input.texture, {}, sampler}}, &u, sizeof(u)) == kInvalidIndex)
        return Errc::PipelineCompileFailed;
    return OkStatus;
}

// --- Redemoinho ---------------------------------------------------------------
class Swirl final : public Effect {
public:
    enum : u32 { kCenter = 0, kRadius, kAngle, kMix };
    const EffectInfo& info() const noexcept override {
        static const EffectInfo i{kSwirlKey, "Redemoinho", "Distorcer", EffectClass::Domain};
        return i;
    }
    void declare_parameters(ParameterRegistry& p) const override {
        p.add_point2("center", "Centro", Vec2{0.5f, 0.5f}, -1.0f, 2.0f, kParamAnimatable | kParamRelative);
        p.typed_range(-10.0f, 10.0f);
        p.add_float("radius", "Raio", 45.0f, 1.0f, 200.0f, kParamAnimatable | kParamPercent, "%");
        p.typed_range(0.1f, 2000.0f);
        p.add_angle("angle", "Ângulo", 180.0f);
        p.add_float("mix", "Mistura", 100.0f, 0.0f, 100.0f, kParamAnimatable | kParamPercent, "%");
    }
    bool is_identity(const EffectEval& e) const noexcept override {
        return std::fabs(e.f(kAngle)) < 0.01f || e.f(kMix) < 0.01f;
    }
    bool demo_values(EffectInstance&, std::vector<ParamValue>& v) const noexcept override {
        v[kAngle] = ParamValue::scalar(240.0f);
        return true;
    }
    void pipelines(std::vector<PipelineKey>& out, SurfaceFormat work) const override {
        out.push_back(PipelineKey::fullscreen(ShaderId::effects_swirl_frag, work));
    }
    Status build(EffectBuildContext& ctx, const EffectEval& e, const LayerImage& input, f32, LayerImage& out) const override {
        EffectUniforms u = base_uniforms(input);
        f32 hs = 1.0f;
        const Vec2 c = region_uv(e, input, e.p2(kCenter), hs);
        u.p0 = Vec4{c.x, c.y, std::clamp(finite_or(e.f(kRadius), 45.0f) * 0.01f, 1e-4f, 20.0f) * hs,
                    finite_or(e.f(kAngle), 0.0f) * kDeg2Rad};
        u.p1 = Vec4{pct(e, kMix), 0.0f, 0.0f, 0.0f};
        return same_region_pass(ctx, input, u, ShaderId::effects_swirl_frag, "redemoinho", CommonSampler::LinearBorder, out);
    }
};

// --- Coordenadas polares ------------------------------------------------------
class PolarCoordinates final : public Effect {
public:
    enum : u32 { kMode = 0, kAmount, kRotation };
    const EffectInfo& info() const noexcept override {
        static const EffectInfo i{kPolarKey, "Coordenadas polares", "Distorcer", EffectClass::Domain};
        return i;
    }
    void declare_parameters(ParameterRegistry& p) const override {
        static const char* const modes[] = {"Retangular para polar", "Polar para retangular"};
        p.add_enum("mode", "Conversão", modes, 2, 0);
        p.add_float("amount", "Quantidade", 100.0f, 0.0f, 100.0f, kParamAnimatable | kParamPercent, "%");
        p.add_angle("rotation", "Rotação", 0.0f);
    }
    bool is_identity(const EffectEval& e) const noexcept override { return e.f(kAmount) < 0.01f; }
    void pipelines(std::vector<PipelineKey>& out, SurfaceFormat work) const override {
        out.push_back(PipelineKey::fullscreen(ShaderId::effects_polar_coordinates_frag, work));
    }
    Status build(EffectBuildContext& ctx, const EffectEval& e, const LayerImage& input, f32, LayerImage& out) const override {
        EffectUniforms u = base_uniforms(input);
        u.p0 = Vec4{static_cast<f32>(e.e(kMode)), pct(e, kAmount), finite_or(e.f(kRotation), 0.0f) / 360.0f, 0.0f};
        return same_region_pass(ctx, input, u, ShaderId::effects_polar_coordinates_frag, "polar", CommonSampler::LinearRepeat, out);
    }
};

// --- Túnel ----------------------------------------------------------------------
class Tunnel final : public Effect {
public:
    enum : u32 { kCenter = 0, kDepth, kTravel, kTwist, kFog, kMix };
    const EffectInfo& info() const noexcept override {
        static const EffectInfo i{kTunnelKey, "Túnel", "Distorcer", EffectClass::Domain};
        return i;
    }
    void declare_parameters(ParameterRegistry& p) const override {
        p.add_point2("center", "Centro", Vec2{0.5f, 0.5f}, -1.0f, 2.0f, kParamAnimatable | kParamRelative);
        p.typed_range(-10.0f, 10.0f);
        p.add_float("depth", "Repetições", 1.0f, 0.1f, 8.0f);
        p.add_float("travel", "Viagem", 0.0f, -100.0f, 100.0f);
        p.typed_range(-100000.0f, 100000.0f);
        p.add_float("twist", "Torção", 0.0f, -4.0f, 4.0f);
        p.add_float("fog", "Névoa", 60.0f, 0.0f, 100.0f, kParamAnimatable | kParamPercent, "%");
        p.add_float("mix", "Mistura", 100.0f, 0.0f, 100.0f, kParamAnimatable | kParamPercent, "%");
    }
    bool is_identity(const EffectEval& e) const noexcept override { return e.f(kMix) < 0.01f; }
    void pipelines(std::vector<PipelineKey>& out, SurfaceFormat work) const override {
        out.push_back(PipelineKey::fullscreen(ShaderId::effects_tunnel_frag, work));
    }
    Status build(EffectBuildContext& ctx, const EffectEval& e, const LayerImage& input, f32, LayerImage& out) const override {
        EffectUniforms u = base_uniforms(input);
        f32 hs = 1.0f;
        const Vec2 c = region_uv(e, input, e.p2(kCenter), hs);
        u.p0 = Vec4{c.x, c.y, std::clamp(finite_or(e.f(kDepth), 1.0f), 0.05f, 50.0f), finite_or(e.f(kTravel), 0.0f)};
        u.p1 = Vec4{std::clamp(finite_or(e.f(kTwist), 0.0f), -50.0f, 50.0f), pct(e, kFog), pct(e, kMix), 0.0f};
        return same_region_pass(ctx, input, u, ShaderId::effects_tunnel_frag, "tunel", CommonSampler::LinearRepeat, out);
    }
};

// --- Lupa ------------------------------------------------------------------------
class Magnify final : public Effect {
public:
    enum : u32 { kCenter = 0, kRadius, kZoom, kRim, kRimColor, kSoftness };
    const EffectInfo& info() const noexcept override {
        static const EffectInfo i{kMagnifyKey, "Lupa", "Distorcer", EffectClass::Domain};
        return i;
    }
    void declare_parameters(ParameterRegistry& p) const override {
        p.add_point2("center", "Centro", Vec2{0.5f, 0.5f}, -1.0f, 2.0f, kParamAnimatable | kParamRelative);
        p.typed_range(-10.0f, 10.0f);
        p.add_float("radius", "Raio", 25.0f, 1.0f, 200.0f, kParamAnimatable | kParamPercent, "%");
        p.add_float("zoom", "Ampliação", 200.0f, 10.0f, 1000.0f, kParamAnimatable | kParamPercent, "%");
        p.add_float("rim", "Aro", 0.0f, 0.0f, 20.0f, kParamAnimatable | kParamPercent, "%");
        p.add_color("rim_color", "Cor do aro", Vec4{1.0f, 1.0f, 1.0f, 1.0f});
        p.add_float("softness", "Suavidade", 3.0f, 0.0f, 100.0f, kParamAnimatable | kParamPercent, "%");
    }
    bool is_identity(const EffectEval& e) const noexcept override {
        return std::fabs(e.f(kZoom) - 100.0f) < 0.01f && e.f(kRim) <= 0.0f;
    }
    void pipelines(std::vector<PipelineKey>& out, SurfaceFormat work) const override {
        out.push_back(PipelineKey::fullscreen(ShaderId::effects_magnify_frag, work));
    }
    Status build(EffectBuildContext& ctx, const EffectEval& e, const LayerImage& input, f32, LayerImage& out) const override {
        EffectUniforms u = base_uniforms(input);
        f32 hs = 1.0f;
        const Vec2 c = region_uv(e, input, e.p2(kCenter), hs);
        u.p0 = Vec4{c.x, c.y, std::clamp(finite_or(e.f(kRadius), 25.0f) * 0.01f, 1e-4f, 20.0f) * hs,
                    std::clamp(finite_or(e.f(kZoom), 200.0f) * 0.01f, 0.05f, 50.0f)};
        u.p1 = Vec4{pct(e, kRim, 0.0f, 1.0f) * hs, pct(e, kSoftness, 0.0f, 1.0f), 0.0f, 0.0f};
        u.color = e.color(kRimColor);
        return same_region_pass(ctx, input, u, ShaderId::effects_magnify_frag, "lupa", CommonSampler::LinearBorder, out);
    }
};

// --- Sombra longa -----------------------------------------------------------------
class LongShadow final : public Effect {
public:
    enum : u32 { kColor = 0, kOpacity, kDirection, kLength, kFade, kShadowOnly };
    const EffectInfo& info() const noexcept override {
        static const EffectInfo i{kLongShadowKey, "Sombra longa", "Estilizar", EffectClass::Neighborhood};
        return i;
    }
    void declare_parameters(ParameterRegistry& p) const override {
        p.add_color("shadow_color", "Cor da sombra", Vec4{0.0f, 0.0f, 0.0f, 1.0f});
        p.add_float("opacity", "Opacidade", 60.0f, 0.0f, 100.0f, kParamAnimatable | kParamPercent, "%");
        p.add_angle("direction", "Direção", 45.0f);
        p.add_float("length", "Comprimento", 300.0f, 0.0f, 2000.0f, kParamAnimatable | kParamPixels, "px");
        p.typed_range(0.0f, 10000.0f);
        p.add_float("fade", "Esmaecer", 0.0f, 0.0f, 100.0f, kParamAnimatable | kParamPercent, "%");
        p.add_bool("shadow_only", "Só a sombra", false);
    }
    bool is_identity(const EffectEval& e) const noexcept override {
        if (e.b(kShadowOnly)) return false;
        return e.f(kOpacity) < 0.01f || e.f(kLength) < 0.5f || !(e.color(kColor).w > 0.0f);
    }
    f32 input_margin(const EffectEval& e) const noexcept override {
        return std::clamp(finite_or(e.f(kLength), 0.0f), 0.0f, 10000.0f);
    }
    void pipelines(std::vector<PipelineKey>& out, SurfaceFormat work) const override {
        out.push_back(PipelineKey::fullscreen(ShaderId::effects_long_shadow_frag, work));
    }
    Status build(EffectBuildContext& ctx, const EffectEval& e, const LayerImage& input, f32 margin, LayerImage& out) const override {
        const f32 length = std::clamp(finite_or(e.f(kLength), 0.0f), 0.0f, 10000.0f);
        const f32 ang = finite_or(e.f(kDirection), 45.0f) * kDeg2Rad;
        const f32 dx = std::cos(ang), dy = std::sin(ang);
        const Rect region = spread_region(input.region, std::fabs(dx) * length, std::fabs(dy) * length, e.placement, margin);
        if (region.w <= 0.0f || region.h <= 0.0f) return Errc::InvalidArgument;
        u32 w = 0, h = 0;
        ctx.region_size(region, input.texel_scale_x(), w, h);
        EffectUniforms u{};
        u.uvMap = EffectBuildContext::uv_map(region, input.region);
        u.texel = Vec4{1.0f / region.w, 1.0f / region.h, input.texel_scale_x(), input.texel_scale_y()};
        // Um passo por ~1,5 texel do comprimento, até 256 (a silhueta não fura).
        const f32 steps = std::clamp(length * input.texel_scale_x() / 1.5f, 1.0f, 256.0f);
        u.p0 = Vec4{dx, dy, length, steps};
        u.p1 = Vec4{pct(e, kOpacity), pct(e, kFade), e.b(kShadowOnly) ? 1.0f : 0.0f, 0.0f};
        u.p3 = Vec4{std::max(input.region.w, 1e-4f), std::max(input.region.h, 1e-4f), 0.0f, 0.0f};
        u.color = e.color(kColor);
        const FGTexture result = ctx.texture("sombra-longa", w, h);
        if (ctx.fullscreen_pass("sombra-longa", PassStage::Effects, result, ShaderId::effects_long_shadow_frag,
                                {PassTexture{input.texture, {}, CommonSampler::LinearBorder}}, &u, sizeof(u)) == kInvalidIndex)
            return Errc::PipelineCompileFailed;
        out = LayerImage{result, region, w, h};
        return OkStatus;
    }
};

// --- Bordas ásperas ---------------------------------------------------------------
class RoughenEdges final : public Effect {
public:
    enum : u32 { kBorder = 0, kScale, kEvolution, kSharpness };
    const EffectInfo& info() const noexcept override {
        static const EffectInfo i{kRoughenKey, "Bordas ásperas", "Estilizar", EffectClass::Neighborhood};
        return i;
    }
    void declare_parameters(ParameterRegistry& p) const override {
        p.add_float("border", "Aspereza", 8.0f, 0.0f, 200.0f, kParamAnimatable | kParamPixels, "px");
        p.add_float("scale", "Escala", 30.0f, 2.0f, 500.0f, kParamAnimatable | kParamPixels, "px");
        p.add_angle("evolution", "Evolução", 0.0f);
        p.add_float("sharpness", "Nitidez", 70.0f, 0.0f, 100.0f, kParamAnimatable | kParamPercent, "%");
    }
    bool is_identity(const EffectEval& e) const noexcept override { return e.f(kBorder) < 0.05f; }
    f32 input_margin(const EffectEval& e) const noexcept override { return std::clamp(finite_or(e.f(kBorder), 0.0f), 0.0f, 500.0f); }
    void pipelines(std::vector<PipelineKey>& out, SurfaceFormat work) const override {
        out.push_back(PipelineKey::fullscreen(ShaderId::effects_roughen_edges_frag, work));
    }
    Status build(EffectBuildContext& ctx, const EffectEval& e, const LayerImage& input, f32, LayerImage& out) const override {
        EffectUniforms u = base_uniforms(input);
        u.p0 = Vec4{std::clamp(finite_or(e.f(kBorder), 8.0f), 0.0f, 500.0f), std::clamp(finite_or(e.f(kScale), 30.0f), 1.0f, 5000.0f),
                    finite_or(e.f(kEvolution), 0.0f) / 360.0f, pct(e, kSharpness)};
        u.p3 = Vec4{std::max(input.region.w, 1e-4f), std::max(input.region.h, 1e-4f), 0.0f, 0.0f};
        return same_region_pass(ctx, input, u, ShaderId::effects_roughen_edges_frag, "bordas-asperas", CommonSampler::LinearBorder, out);
    }
};

// --- Curvas de nível -----------------------------------------------------------------
class ContourLines final : public Effect {
public:
    enum : u32 { kLevels = 0, kWidth, kColor, kOffset, kLinesOnly };
    const EffectInfo& info() const noexcept override {
        // Neighborhood: lê a derivada da luminância (e um PerPixel precisaria de operação fundida).
        static const EffectInfo i{kContourKey, "Curvas de nível", "Estilizar", EffectClass::Neighborhood};
        return i;
    }
    void declare_parameters(ParameterRegistry& p) const override {
        p.add_float("levels", "Níveis", 10.0f, 1.0f, 64.0f);
        p.add_float("width", "Espessura", 1.5f, 0.2f, 20.0f, kParamAnimatable | kParamPixels, "px");
        p.add_color("color", "Cor", Vec4{1.0f, 1.0f, 1.0f, 1.0f});
        p.add_float("offset", "Deslocamento", 0.0f, 0.0f, 100.0f, kParamAnimatable | kParamPercent, "%");
        p.add_bool("lines_only", "Só as linhas", false);
    }
    void pipelines(std::vector<PipelineKey>& out, SurfaceFormat work) const override {
        out.push_back(PipelineKey::fullscreen(ShaderId::effects_contour_lines_frag, work));
    }
    Status build(EffectBuildContext& ctx, const EffectEval& e, const LayerImage& input, f32, LayerImage& out) const override {
        EffectUniforms u = base_uniforms(input);
        u.p0 = Vec4{std::clamp(finite_or(e.f(kLevels), 10.0f), 1.0f, 256.0f),
                    std::clamp(finite_or(e.f(kWidth), 1.5f), 0.1f, 50.0f) * input.texel_scale_x(),
                    pct(e, kOffset), e.b(kLinesOnly) ? 1.0f : 0.0f};
        u.color = e.color(kColor);
        return same_region_pass(ctx, input, u, ShaderId::effects_contour_lines_frag, "curvas-nivel", CommonSampler::LinearClamp, out);
    }
};

// --- Brilho interno ----------------------------------------------------------------
class InnerGlow final : public Effect {
public:
    enum : u32 { kColor = 0, kRadius, kIntensity, kSource, kChoke };
    const EffectInfo& info() const noexcept override {
        static const EffectInfo i{kInnerGlowKey, "Brilho interno", "Luz", EffectClass::Neighborhood};
        return i;
    }
    void declare_parameters(ParameterRegistry& p) const override {
        p.add_color("color", "Cor", Vec4{1.0f, 0.85f, 0.4f, 1.0f});
        p.add_float("radius", "Raio", 20.0f, 0.5f, 300.0f, kParamAnimatable | kParamPixels, "px");
        p.add_float("intensity", "Intensidade", 80.0f, 0.0f, 200.0f, kParamAnimatable | kParamPercent, "%");
        static const char* const sources[] = {"Borda", "Centro"};
        p.add_enum("source", "Origem", sources, 2, 0);
        p.add_float("choke", "Concentrar", 0.0f, 0.0f, 100.0f, kParamAnimatable | kParamPercent, "%");
    }
    bool is_identity(const EffectEval& e) const noexcept override {
        return e.f(kIntensity) < 0.01f || !(e.color(kColor).w > 0.0f);
    }
    f32 input_margin(const EffectEval& e) const noexcept override { return std::clamp(finite_or(e.f(kRadius), 0.0f), 0.0f, 300.0f); }
    void pipelines(std::vector<PipelineKey>& out, SurfaceFormat work) const override {
        out.push_back(PipelineKey::fullscreen(ShaderId::effects_inner_glow_frag, work));
    }
    Status build(EffectBuildContext& ctx, const EffectEval& e, const LayerImage& input, f32, LayerImage& out) const override {
        EffectUniforms u = base_uniforms(input);
        u.p0 = Vec4{std::clamp(finite_or(e.f(kRadius), 20.0f), 0.5f, 300.0f), pct(e, kIntensity, 0.0f, 2.0f),
                    static_cast<f32>(e.e(kSource)), pct(e, kChoke)};
        u.p3 = Vec4{std::max(input.region.w, 1e-4f), std::max(input.region.h, 1e-4f), 0.0f, 0.0f};
        u.color = e.color(kColor);
        return same_region_pass(ctx, input, u, ShaderId::effects_inner_glow_frag, "brilho-interno", CommonSampler::LinearBorder, out);
    }
};

// --- Raio ---------------------------------------------------------------------------
class Lightning final : public Effect {
public:
    enum : u32 { kStart = 0, kEnd, kColor, kDisplacement, kWidth, kGlow, kDetail, kBranches, kIntensity, kEvolution };
    const EffectInfo& info() const noexcept override {
        // Neighborhood: um PerPixel sem operação de cor fundida seria descartado pelo grafo.
        static const EffectInfo i{kLightningKey, "Raio", "Gerar", EffectClass::Neighborhood};
        return i;
    }
    void declare_parameters(ParameterRegistry& p) const override {
        p.add_point2("start", "Ponto inicial", Vec2{0.5f, 0.05f}, -1.0f, 2.0f, kParamAnimatable | kParamRelative);
        p.typed_range(-10.0f, 10.0f);
        p.add_point2("end", "Ponto final", Vec2{0.5f, 0.95f}, -1.0f, 2.0f, kParamAnimatable | kParamRelative);
        p.typed_range(-10.0f, 10.0f);
        p.add_color("color", "Cor", Vec4{0.45f, 0.65f, 1.0f, 1.0f});
        p.add_float("displacement", "Desvio", 60.0f, 0.0f, 500.0f, kParamAnimatable | kParamPixels, "px");
        p.add_float("width", "Espessura", 3.0f, 0.5f, 40.0f, kParamAnimatable | kParamPixels, "px");
        p.add_float("glow", "Brilho", 18.0f, 0.0f, 200.0f, kParamAnimatable | kParamPixels, "px");
        p.add_float("detail", "Detalhe", 10.0f, 1.0f, 40.0f);
        p.add_float("branches", "Galhos", 50.0f, 0.0f, 100.0f, kParamAnimatable | kParamPercent, "%");
        p.add_float("intensity", "Intensidade", 100.0f, 0.0f, 300.0f, kParamAnimatable | kParamPercent, "%");
        p.add_float("evolution", "Evolução", 0.0f, -1000.0f, 1000.0f);
        p.typed_range(-1000000.0f, 1000000.0f);
    }
    bool is_identity(const EffectEval& e) const noexcept override { return e.f(kIntensity) < 0.01f; }
    void pipelines(std::vector<PipelineKey>& out, SurfaceFormat work) const override {
        out.push_back(PipelineKey::fullscreen(ShaderId::effects_lightning_frag, work));
    }
    Status build(EffectBuildContext& ctx, const EffectEval& e, const LayerImage& input, f32, LayerImage& out) const override {
        EffectUniforms u = base_uniforms(input);
        // Pontos relativos (0..1 da CAMADA, não da região: um efeito anterior
        // pode ter crescido a região) → px da região da entrada.
        const Rect& r = input.region;
        // Com Motion Tile a parede é o plano (como antes); senão, a caixa da camada.
        const Vec2 ls = input.tiled ? Vec2{r.w, r.h} : layer_size(e, input);
        const Vec2 a = e.p2(kStart), b = e.p2(kEnd);
        u.p0 = Vec4{a.x * ls.x, a.y * ls.y, b.x * ls.x, b.y * ls.y};
        // A região pode ser maior que a camada: os pontos andam pela origem dela.
        u.p0.x -= r.x; u.p0.z -= r.x; u.p0.y -= r.y; u.p0.w -= r.y;
        u.p1 = Vec4{std::clamp(finite_or(e.f(kDisplacement), 60.0f), 0.0f, 5000.0f), std::clamp(finite_or(e.f(kWidth), 3.0f), 0.2f, 100.0f),
                    std::clamp(finite_or(e.f(kGlow), 18.0f), 0.0f, 1000.0f), finite_or(e.f(kEvolution), 0.0f)};
        u.p2 = Vec4{std::clamp(finite_or(e.f(kDetail), 10.0f), 1.0f, 100.0f), pct(e, kIntensity, 0.0f, 3.0f), pct(e, kBranches), 0.0f};
        u.p3 = Vec4{std::max(r.w, 1e-4f), std::max(r.h, 1e-4f), 0.0f, 0.0f};
        u.color = e.color(kColor);
        return same_region_pass(ctx, input, u, ShaderId::effects_lightning_frag, "raio", CommonSampler::LinearClamp, out);
    }
};

// --- Células (Voronoi) ----------------------------------------------------------------
class VoronoiCells final : public Effect {
public:
    enum : u32 { kSize = 0, kMode, kEdgeColor, kEdgeWidth, kRandomness, kEvolution, kMix };
    const EffectInfo& info() const noexcept override {
        static const EffectInfo i{kVoronoiKey, "Células", "Gerar", EffectClass::Neighborhood};
        return i;
    }
    void declare_parameters(ParameterRegistry& p) const override {
        p.add_float("size", "Tamanho da célula", 60.0f, 4.0f, 500.0f, kParamAnimatable | kParamPixels, "px");
        static const char* const modes[] = {"Bordas", "Vitral", "Bolhas"};
        p.add_enum("mode", "Estilo", modes, 3, 0);
        p.add_color("edge_color", "Cor da borda", Vec4{1.0f, 1.0f, 1.0f, 1.0f});
        p.add_float("edge_width", "Espessura da borda", 2.0f, 0.5f, 40.0f, kParamAnimatable | kParamPixels, "px");
        p.add_float("randomness", "Aleatoriedade", 100.0f, 0.0f, 100.0f, kParamAnimatable | kParamPercent, "%");
        p.add_angle("evolution", "Evolução", 0.0f);
        p.add_float("mix", "Mistura", 100.0f, 0.0f, 100.0f, kParamAnimatable | kParamPercent, "%");
    }
    bool is_identity(const EffectEval& e) const noexcept override { return e.f(kMix) < 0.01f; }
    void pipelines(std::vector<PipelineKey>& out, SurfaceFormat work) const override {
        out.push_back(PipelineKey::fullscreen(ShaderId::effects_voronoi_cells_frag, work));
    }
    Status build(EffectBuildContext& ctx, const EffectEval& e, const LayerImage& input, f32, LayerImage& out) const override {
        EffectUniforms u = base_uniforms(input);
        u.p0 = Vec4{std::clamp(finite_or(e.f(kSize), 60.0f), 2.0f, 5000.0f), static_cast<f32>(e.e(kMode)),
                    finite_or(e.f(kEvolution), 0.0f) / 360.0f * 6.2831853f, std::clamp(finite_or(e.f(kEdgeWidth), 2.0f), 0.2f, 200.0f)};
        u.p1 = Vec4{pct(e, kMix), pct(e, kRandomness), 0.0f, 0.0f};
        u.p3 = Vec4{std::max(input.region.w, 1e-4f), std::max(input.region.h, 1e-4f), 0.0f, 0.0f};
        u.color = e.color(kEdgeColor);
        return same_region_pass(ctx, input, u, ShaderId::effects_voronoi_cells_frag, "celulas", CommonSampler::LinearClamp, out);
    }
};

} // namespace

void register_gap_effects_046(EffectRegistry& r) {
    (void)r.add(std::make_unique<Swirl>());
    (void)r.add(std::make_unique<PolarCoordinates>());
    (void)r.add(std::make_unique<Tunnel>());
    (void)r.add(std::make_unique<Magnify>());
    (void)r.add(std::make_unique<LongShadow>());
    (void)r.add(std::make_unique<RoughenEdges>());
    (void)r.add(std::make_unique<ContourLines>());
    (void)r.add(std::make_unique<InnerGlow>());
    (void)r.add(std::make_unique<Lightning>());
    (void)r.add(std::make_unique<VoronoiCells>());
}

} // namespace aurea::builtin
