// =============================================================================
//  Aurea / effects / builtin / AeEffects2026Stylize.cpp
//
//  Lote "AE 2026", estilizar/distorcer/desfoque: Deformar (estilos), Relevo e
//  Relevo colorido, Sombra radial, Sombra interna, Bisel das bordas,
//  Pinceladas, Remoção simples de fio, Texturizar, Plástico, Desfoque
//  bilateral, Desfoque inteligente e Desfoque em cruz.
//
//  Parâmetros: os do efeito equivalente do After Effects (documentação
//  pública), com rótulos em pt-BR; a ordem é contrato (só acrescentar no fim).
// =============================================================================
#include "AeEffects2026.hpp"

namespace aurea::builtin::ae26 {
namespace {

constexpr u16 kAnimPct = kParamAnimatable | kParamPercent;
constexpr u16 kAnimPx = kParamAnimatable | kParamPixels;

// =============================================================================
// Deformar (estilos) — ae_warp.frag
// =============================================================================
class WarpStyles final : public Effect {
public:
    enum : u32 { kStyle = 0, kAxis, kBend, kHorizontal, kVertical };
    const EffectInfo& info() const noexcept override {
        static const EffectInfo i{"aurea.distort.warp_styles", "Deformar (estilos)", "Distorcer", EffectClass::Domain};
        return i;
    }
    void declare_parameters(ParameterRegistry& p) const override {
        static const char* const kStyles[] = {"Arco", "Arco inferior", "Arco superior", "Arcada", "Bojo",
                                              "Concha inferior", "Concha superior", "Bandeira", "Onda", "Peixe",
                                              "Subir", "Olho de peixe", "Inflar", "Espremer", "Torcer"};
        p.add_enum("style", "Estilo da deformação", kStyles, 15, 0);
        static const char* const kAxes[] = {"Horizontal", "Vertical"};
        p.add_enum("axis", "Eixo da deformação", kAxes, 2, 0);
        p.add_float("bend", "Curvatura", 50.0f, -100.0f, 100.0f, kParamAnimatable);
        p.add_float("horizontal_distortion", "Distorção horizontal", 0.0f, -100.0f, 100.0f, kParamAnimatable);
        p.add_float("vertical_distortion", "Distorção vertical", 0.0f, -100.0f, 100.0f, kParamAnimatable);
    }
    bool is_identity(const EffectEval& e) const noexcept override {
        return std::fabs(fin(e.f(kBend), 0.0f)) < 0.01f && std::fabs(fin(e.f(kHorizontal), 0.0f)) < 0.01f
            && std::fabs(fin(e.f(kVertical), 0.0f)) < 0.01f;
    }
    void pipelines(std::vector<PipelineKey>& out, SurfaceFormat work) const override {
        out.push_back(PipelineKey::fullscreen(ShaderId::effects_ae_warp_frag, work));
    }
    Status build(EffectBuildContext& ctx, const EffectEval& e, const LayerImage& input, f32 margin, LayerImage& out) const override {
        const f32 b = clampf(e.f(kBend), -100, 100, 0) / 100.0f;
        const f32 h = clampf(e.f(kHorizontal), -100, 100, 0) / 100.0f;
        const f32 v = clampf(e.f(kVertical), -100, 100, 0) / 100.0f;
        const Vec2 ls = layer_size(e, input);
        // Os estilos levam a imagem para fora da caixa (o Arco desce os lados):
        // a saída cresce na proporção da curvatura e das distorções.
        const f32 grow = (0.6f * std::fabs(b) + 0.35f * (std::fabs(h) + std::fabs(v))) * std::max(ls.x, ls.y);
        const Rect region = grown_region(e, input, grow, margin);
        AeUniforms u = ae_uniforms(e, input, region);
        u.p[0] = Vec4{en(e, kStyle, 15), en(e, kAxis, 2), b, 0.0f};
        u.p[1] = Vec4{h, v, 0.0f, 0.0f};
        return ae_pass(ctx, input, region, u, ShaderId::effects_ae_warp_frag, "ae-deformar", CommonSampler::LinearBorder, out);
    }
};

// =============================================================================
// ae_stylize.frag: Relevo (0), Relevo colorido (1), Bisel das bordas (2),
// Pinceladas (3), Remoção simples de fio (4)
// =============================================================================
class AeStylizeEffect : public Effect {
public:
    void pipelines(std::vector<PipelineKey>& out, SurfaceFormat work) const override {
        out.push_back(PipelineKey::fullscreen(ShaderId::effects_ae_stylize_frag, work));
    }
    Status build(EffectBuildContext& ctx, const EffectEval& e, const LayerImage& input, f32, LayerImage& out) const override {
        AeUniforms u = ae_uniforms(e, input);
        fill(e, input, u);
        u.p[11].w = static_cast<f32>(op());
        return ae_pass(ctx, input, u, ShaderId::effects_ae_stylize_frag, "ae-estilizar", sampler(e), out);
    }

protected:
    [[nodiscard]] virtual u32 op() const noexcept = 0;
    [[nodiscard]] virtual CommonSampler sampler(const EffectEval&) const noexcept { return CommonSampler::LinearClamp; }
    virtual void fill(const EffectEval& e, const LayerImage& input, AeUniforms& u) const noexcept = 0;
};

class Emboss final : public AeStylizeEffect {
public:
    enum : u32 { kDirection = 0, kRelief, kContrast, kBlend };
    explicit Emboss(bool color) noexcept : color_(color) {}
    const EffectInfo& info() const noexcept override {
        static const EffectInfo c{"aurea.stylize.color_emboss", "Relevo colorido", "Estilizar", EffectClass::Neighborhood};
        static const EffectInfo g{"aurea.stylize.emboss", "Relevo", "Estilizar", EffectClass::Neighborhood};
        return color_ ? c : g;
    }
    void declare_parameters(ParameterRegistry& p) const override {
        p.add_angle("direction", "Direção", 45.0f);
        p.add_float("relief", "Relevo", 1.5f, 0.0f, 10.0f, kAnimPx, "px");
        p.typed_range(0.0f, 50.0f);
        p.add_float("contrast", "Contraste", 100.0f, 0.0f, 500.0f, kAnimPct, "%");
        p.add_float("blend", "Misturar com o original", 0.0f, 0.0f, 100.0f, kAnimPct, "%");
    }
    bool is_identity(const EffectEval& e) const noexcept override { return pct(e, kBlend) >= 0.999f; }
    f32 input_margin(const EffectEval& e) const noexcept override { return clampf(e.f(kRelief), 0, 50, 1.5f) + 1.0f; }

protected:
    u32 op() const noexcept override { return color_ ? 1u : 0u; }
    void fill(const EffectEval& e, const LayerImage&, AeUniforms& u) const noexcept override {
        u.p[0] = Vec4{fin(e.f(kDirection), 45.0f) * kDeg2Rad, clampf(e.f(kRelief), 0, 50, 1.5f), pct(e, kContrast, 0, 5),
                      pct(e, kBlend)};
    }

private:
    bool color_;
};

class BevelEdges final : public AeStylizeEffect {
public:
    enum : u32 { kThickness = 0, kAngle, kColor, kIntensity };
    const EffectInfo& info() const noexcept override {
        static const EffectInfo i{"aurea.stylize.bevel_edges", "Bisel das bordas", "Estilizar", EffectClass::Neighborhood};
        return i;
    }
    void declare_parameters(ParameterRegistry& p) const override {
        p.add_float("edge_thickness", "Espessura da borda", 10.0f, 0.0f, 50.0f, kAnimPct, "%");
        p.add_angle("light_angle", "Ângulo da luz", -60.0f);
        p.add_color("light_color", "Cor da luz", Vec4{1.0f, 1.0f, 1.0f, 1.0f});
        p.add_float("light_intensity", "Intensidade da luz", 25.0f, 0.0f, 100.0f, kAnimPct, "%");
    }
    bool is_identity(const EffectEval& e) const noexcept override {
        return pct(e, kThickness) < 1e-4f || pct(e, kIntensity) < 1e-4f;
    }
    bool demo_values(EffectInstance&, std::vector<ParamValue>& v) const noexcept override {
        v[kIntensity] = ParamValue::scalar(60.0f);
        return true;
    }

protected:
    u32 op() const noexcept override { return 2; }
    void fill(const EffectEval& e, const LayerImage&, AeUniforms& u) const noexcept override {
        u.p[0] = Vec4{pct(e, kThickness, 0, 0.5f), fin(e.f(kAngle), -60.0f) * kDeg2Rad, pct(e, kIntensity), 0.0f};
        u.c[0] = e.color(kColor);
    }
};

class BrushStrokes final : public AeStylizeEffect {
public:
    enum : u32 { kAngle = 0, kSize, kLength, kDensity, kRandomness, kSurface, kBlend };
    const EffectInfo& info() const noexcept override {
        static const EffectInfo i{"aurea.stylize.brush_strokes", "Pinceladas", "Estilizar", EffectClass::Neighborhood};
        return i;
    }
    void declare_parameters(ParameterRegistry& p) const override {
        p.add_angle("stroke_angle", "Ângulo do traço", 135.0f);
        p.add_float("brush_size", "Tamanho do pincel", 2.0f, 0.5f, 10.0f, kAnimPx, "px");
        p.typed_range(0.25f, 40.0f);
        p.add_float("stroke_length", "Comprimento do traço", 10.0f, 0.0f, 40.0f, kAnimPx, "px");
        p.typed_range(0.0f, 120.0f);
        p.add_float("stroke_density", "Densidade do traço", 1.0f, 0.0f, 2.0f, kParamAnimatable);
        p.add_float("stroke_randomness", "Aleatoriedade do traço", 1.0f, 0.0f, 2.0f, kParamAnimatable);
        static const char* const kSurfaces[] = {"Pintar na original", "Pintar em transparente", "Pintar em branco",
                                                "Pintar em preto"};
        p.add_enum("paint_surface", "Superfície da pintura", kSurfaces, 4, 0);
        p.add_float("blend", "Misturar com o original", 0.0f, 0.0f, 100.0f, kAnimPct, "%");
    }
    bool is_identity(const EffectEval& e) const noexcept override { return pct(e, kBlend) >= 0.999f; }
    f32 input_margin(const EffectEval& e) const noexcept override { return clampf(e.f(kLength), 0, 120, 10) + clampf(e.f(kSize), 0.25f, 40, 2); }

protected:
    u32 op() const noexcept override { return 3; }
    void fill(const EffectEval& e, const LayerImage&, AeUniforms& u) const noexcept override {
        u.p[0] = Vec4{fin(e.f(kAngle), 135.0f) * kDeg2Rad, clampf(e.f(kSize), 0.25f, 40.0f, 2.0f),
                      clampf(e.f(kLength), 0.0f, 120.0f, 10.0f), clampf(e.f(kDensity), 0.0f, 2.0f, 1.0f)};
        u.p[1] = Vec4{clampf(e.f(kRandomness), 0.0f, 2.0f, 1.0f), en(e, kSurface, 4), pct(e, kBlend), 0.0f};
    }
};

class SimpleWireRemoval final : public AeStylizeEffect {
public:
    enum : u32 { kPointA = 0, kPointB, kStyle, kThickness, kSlope, kMirror };
    const EffectInfo& info() const noexcept override {
        static const EffectInfo i{"aurea.key.simple_wire_removal", "Remoção simples de fio", "Recorte", EffectClass::Neighborhood};
        return i;
    }
    void declare_parameters(ParameterRegistry& p) const override {
        p.add_point2("point_a", "Ponto A", Vec2{0.3f, 0.2f}, -1.0f, 2.0f, kParamAnimatable | kParamRelative);
        p.typed_range(-10.0f, 10.0f);
        p.add_point2("point_b", "Ponto B", Vec2{0.7f, 0.8f}, -1.0f, 2.0f, kParamAnimatable | kParamRelative);
        p.typed_range(-10.0f, 10.0f);
        static const char* const kStyles[] = {"Esmaecer", "Deslocar", "Deslocar na horizontal"};
        p.add_enum("removal_style", "Estilo de remoção", kStyles, 3, 0);
        p.add_float("thickness", "Espessura", 6.0f, 0.0f, 100.0f, kAnimPx, "px");
        p.typed_range(0.0f, 1000.0f);
        p.add_float("slope", "Inclinação", 50.0f, 0.0f, 100.0f, kAnimPct, "%");
        p.add_float("mirror_blend", "Mistura espelhada", 0.0f, 0.0f, 100.0f, kAnimPct, "%");
    }
    bool is_identity(const EffectEval& e) const noexcept override { return !(fin(e.f(kThickness), 0.0f) > 0.05f); }
    f32 input_margin(const EffectEval& e) const noexcept override { return clampf(e.f(kThickness), 0, 1000, 6) * 3.0f; }

protected:
    u32 op() const noexcept override { return 4; }
    void fill(const EffectEval& e, const LayerImage& input, AeUniforms& u) const noexcept override {
        const Vec2 a = layer_point(e, input, kPointA), b = layer_point(e, input, kPointB);
        u.p[0] = Vec4{a.x, a.y, b.x, b.y};
        u.p[1] = Vec4{en(e, kStyle, 3), clampf(e.f(kThickness), 0.0f, 1000.0f, 6.0f), pct(e, kSlope), pct(e, kMirror)};
    }
};

// =============================================================================
// ae_shadow.frag: Sombra radial (0) e Sombra interna (1, u_tex1 = alfa desfocado)
// =============================================================================
class RadialShadow final : public Effect {
public:
    enum : u32 { kColor = 0, kOpacity, kLight, kDistance, kSoftness, kRender, kInfluence, kShadowOnly, kResize };
    const EffectInfo& info() const noexcept override {
        static const EffectInfo i{"aurea.stylize.radial_shadow", "Sombra radial", "Estilizar", EffectClass::Neighborhood};
        return i;
    }
    void declare_parameters(ParameterRegistry& p) const override {
        p.add_color("shadow_color", "Cor da sombra", Vec4{0.0f, 0.0f, 0.0f, 1.0f});
        p.add_float("opacity", "Opacidade", 50.0f, 0.0f, 100.0f, kAnimPct, "%");
        p.add_point2("light_source", "Fonte de luz", Vec2{0.3f, 0.2f}, -1.0f, 2.0f, kParamAnimatable | kParamRelative);
        p.typed_range(-10.0f, 10.0f);
        p.add_float("projection_distance", "Distância da projeção", 10.0f, 0.0f, 100.0f, kParamAnimatable);
        p.typed_range(0.0f, 1000.0f);
        p.add_float("softness", "Suavidade", 0.0f, 0.0f, 100.0f, kAnimPx, "px");
        p.typed_range(0.0f, 500.0f);
        static const char* const kRenders[] = {"Normal", "Borda de vidro"};
        p.add_enum("render", "Renderizar", kRenders, 2, 0);
        p.add_float("color_influence", "Influência da cor", 0.0f, 0.0f, 100.0f, kAnimPct, "%");
        p.add_bool("shadow_only", "Só a sombra", false);
        p.add_bool("resize_layer", "Redimensionar camada", true);
    }
    bool is_identity(const EffectEval& e) const noexcept override {
        if (e.b(kShadowOnly)) return false;
        return pct(e, kOpacity) < 1e-4f || !(e.color(kColor).w > 0.0f || pct(e, kInfluence) > 0.0f);
    }
    f32 input_margin(const EffectEval& e) const noexcept override { return clampf(e.f(kSoftness), 0, 500, 0); }
    void pipelines(std::vector<PipelineKey>& out, SurfaceFormat work) const override {
        out.push_back(PipelineKey::fullscreen(ShaderId::effects_ae_shadow_frag, work));
    }
    static f32 scale_of(const EffectEval& e) noexcept { return 1.0f + clampf(e.f(kDistance), 0.0f, 1000.0f, 10.0f) / 50.0f; }
    Status build(EffectBuildContext& ctx, const EffectEval& e, const LayerImage& input, f32 margin, LayerImage& out) const override {
        const Vec2 L = layer_point(e, input, kLight);
        const f32 k = scale_of(e);
        const f32 soft = clampf(e.f(kSoftness), 0.0f, 500.0f, 0.0f);
        Rect region = input.region;
        if (e.b(kResize)) {
            // A sombra é a camada projetada a partir da luz (escala k em volta dela).
            const Rect& r = input.region;
            // A luz no plano da camada (L é medido no plano do efeito: a parede, com Motion Tile).
            const Vec2 o = plane_origin(input);
            const Vec2 Lc{L.x + o.x, L.y + o.y};
            const f32 x0 = Lc.x + (r.x - Lc.x) * k, x1 = Lc.x + (r.right() - Lc.x) * k;
            const f32 y0 = Lc.y + (r.y - Lc.y) * k, y1 = Lc.y + (r.bottom() - Lc.y) * k;
            const f32 left = std::min({r.x, x0, x1}) - soft, right = std::max({r.right(), x0, x1}) + soft;
            const f32 top = std::min({r.y, y0, y1}) - soft, bottom = std::max({r.bottom(), y0, y1}) + soft;
            const Rect grown{left, top, right - left, bottom - top};
            region = spread_region(grown, 0.0f, 0.0f, e.placement, margin);
        }
        AeUniforms u = ae_uniforms(e, input, region);
        u.p[0] = Vec4{L.x, L.y, k, soft};
        u.p[1] = Vec4{pct(e, kOpacity), en(e, kRender, 2), pct(e, kInfluence), flag(e.b(kShadowOnly))};
        u.p[11].w = 0.0f;
        u.c[0] = e.color(kColor);
        const PassTexture self{input.texture, {}, CommonSampler::LinearBorder};
        return ae_pass(ctx, input, region, u, ShaderId::effects_ae_shadow_frag, "ae-sombra-radial", CommonSampler::LinearBorder,
                       out, &self, 1);
    }
};

class InnerShadow final : public Effect {
public:
    enum : u32 { kColor = 0, kOpacity, kAngle, kDistance, kChoke, kSize };
    const EffectInfo& info() const noexcept override {
        static const EffectInfo i{"aurea.stylize.inner_shadow", "Sombra interna", "Estilizar", EffectClass::Neighborhood};
        return i;
    }
    void declare_parameters(ParameterRegistry& p) const override {
        p.add_color("color", "Cor", Vec4{0.0f, 0.0f, 0.0f, 1.0f});
        p.add_float("opacity", "Opacidade", 75.0f, 0.0f, 100.0f, kAnimPct, "%");
        p.add_angle("angle", "Ângulo", 120.0f);
        p.add_float("distance", "Distância", 5.0f, 0.0f, 100.0f, kAnimPx, "px");
        p.typed_range(0.0f, 2000.0f);
        p.add_float("choke", "Engasgar", 0.0f, 0.0f, 100.0f, kAnimPct, "%");
        p.add_float("size", "Tamanho", 5.0f, 0.0f, 100.0f, kAnimPx, "px");
        p.typed_range(0.0f, 1000.0f);
    }
    bool is_identity(const EffectEval& e) const noexcept override {
        return pct(e, kOpacity) < 1e-4f || !(e.color(kColor).w > 0.0f);
    }
    f32 input_margin(const EffectEval& e) const noexcept override {
        return clampf(e.f(kDistance), 0, 2000, 5) + clampf(e.f(kSize), 0, 1000, 5) * 1.5f;
    }
    void pipelines(std::vector<PipelineKey>& out, SurfaceFormat work) const override {
        out.push_back(PipelineKey::fullscreen(ShaderId::effects_ae_shadow_frag, work));
    }
    Status build(EffectBuildContext& ctx, const EffectEval& e, const LayerImage& input, f32, LayerImage& out) const override {
        BlurRequest req;
        const f32 size = clampf(e.f(kSize), 0.0f, 1000.0f, 5.0f);
        req.sigmaX = req.sigmaY = size * 0.5f;
        req.outRegion = input.region;
        req.label = "ae-sombra-interna-desfoque";
        LayerImage blurred;
        if (const Status s = build_gaussian(ctx, input, req, blurred); !s.ok()) return s;
        AeUniforms u = ae_uniforms(e, input);
        const f32 ang = fin(e.f(kAngle), 120.0f) * kDeg2Rad;
        const f32 d = clampf(e.f(kDistance), 0.0f, 2000.0f, 5.0f);
        // Luz vinda do ângulo (y para cima): a sombra cai do lado oposto.
        u.p[0] = Vec4{-std::cos(ang) * d, std::sin(ang) * d, pct(e, kOpacity), pct(e, kChoke, 0.0f, 0.99f)};
        const Vec2 o = plane_origin(input);
        u.p[2] = Vec4{blurred.region.x - o.x, blurred.region.y - o.y, std::max(blurred.region.w, 1e-3f),
                      std::max(blurred.region.h, 1e-3f)};
        u.p[11].w = 1.0f;
        u.c[0] = e.color(kColor);
        const PassTexture tex1{blurred.texture, {}, CommonSampler::LinearBorder};
        return ae_pass(ctx, input, u, ShaderId::effects_ae_shadow_frag, "ae-sombra-interna", CommonSampler::LinearClamp, out, &tex1, 1);
    }
};

// =============================================================================
// ae_relief.frag: Texturizar (0) e Plástico (1) — u_tex1 = textura / relevo
// =============================================================================
class Texturize final : public Effect {
public:
    enum : u32 { kLayer = 0, kDirection, kContrast, kPlacement };
    const EffectInfo& info() const noexcept override {
        static const EffectInfo i{"aurea.stylize.texturize", "Texturizar", "Estilizar", EffectClass::Neighborhood};
        return i;
    }
    void declare_parameters(ParameterRegistry& p) const override {
        p.add_layer_ref("texture_layer", "Camada da textura");
        p.add_angle("light_direction", "Direção da luz", 135.0f);
        p.add_float("texture_contrast", "Contraste da textura", 1.0f, 0.0f, 2.0f, kParamAnimatable);
        static const char* const kPlacements[] = {"Ladrilhar textura", "Centralizar textura", "Esticar textura para caber"};
        p.add_enum("texture_placement", "Posição da textura", kPlacements, 3, 0);
    }
    i32 input_layer_param() const noexcept override { return static_cast<i32>(kLayer); }
    bool is_identity(const EffectEval& e) const noexcept override { return !(fin(e.f(kContrast), 1.0f) > 1e-3f); }
    void pipelines(std::vector<PipelineKey>& out, SurfaceFormat work) const override {
        out.push_back(PipelineKey::fullscreen(ShaderId::effects_ae_relief_frag, work));
    }
    Status build(EffectBuildContext& ctx, const EffectEval& e, const LayerImage& input, f32, LayerImage& out) const override {
        const u64 ref = e.value(kLayer).ref;
        const LayerImage* tex = ref ? ctx.layer_input(ref) : nullptr;
        if (ref && !tex) { out = input; return OkStatus; }
        AeUniforms u = ae_uniforms(e, input);
        u.p[0] = Vec4{fin(e.f(kDirection), 135.0f) * kDeg2Rad, clampf(e.f(kContrast), 0.0f, 2.0f, 1.0f), en(e, kPlacement, 3),
                      tex ? 1.0f : 0.0f};
        // Região da textura (px da composição) — ou a própria entrada, sem camada (tela procedural).
        const Rect tr = tex ? tex->region : input.region;
        u.p[2] = Vec4{tr.x, tr.y, std::max(tr.w, 1.0f), std::max(tr.h, 1.0f)};
        u.p[11].w = 0.0f;
        const PassTexture tex1{tex ? tex->texture : input.texture, {}, CommonSampler::LinearRepeat};
        return ae_pass(ctx, input, u, ShaderId::effects_ae_relief_frag, "ae-texturizar", CommonSampler::LinearClamp, out, &tex1, 1);
    }
};

class Plastic final : public Effect {
public:
    enum : u32 { kLayer = 0, kProperty, kSoftness, kHeight, kCutMin, kCutMax, kIntensity, kLightColor, kLightType,
                 kLightHeight, kLightDirection, kAmbient, kDiffuse, kSpecular, kRoughness, kMetal };
    const EffectInfo& info() const noexcept override {
        static const EffectInfo i{"aurea.stylize.plastic", "Plástico", "Estilizar", EffectClass::Neighborhood};
        return i;
    }
    void declare_parameters(ParameterRegistry& p) const override {
        p.add_layer_ref("bump_layer", "Camada do relevo");
        static const char* const kProps[] = {"Luminância", "Alfa", "Vermelho", "Verde", "Azul"};
        p.add_enum("property", "Propriedade", kProps, 5, 0);
        p.add_float("softness", "Suavidade", 10.0f, 0.0f, 100.0f, kAnimPx, "px");
        p.typed_range(0.0f, 500.0f);
        p.add_float("height", "Altura", 25.0f, -100.0f, 100.0f, kParamAnimatable);
        p.add_float("cut_min", "Corte mínimo", 0.0f, 0.0f, 100.0f, kAnimPct, "%");
        p.add_float("cut_max", "Corte máximo", 100.0f, 0.0f, 100.0f, kAnimPct, "%");
        p.add_float("light_intensity", "Intensidade da luz", 100.0f, 0.0f, 200.0f, kAnimPct, "%");
        p.add_color("light_color", "Cor da luz", Vec4{1.0f, 1.0f, 1.0f, 1.0f});
        static const char* const kTypes[] = {"Distante", "Pontual"};
        p.add_enum("light_type", "Tipo de luz", kTypes, 2, 0);
        p.add_float("light_height", "Altura da luz", 35.0f, 0.0f, 90.0f, kParamAnimatable, "°");
        p.add_angle("light_direction", "Direção da luz", 45.0f);
        p.add_float("ambient", "Ambiente", 15.0f, 0.0f, 100.0f, kAnimPct, "%");
        p.add_float("diffuse", "Difusa", 60.0f, 0.0f, 100.0f, kAnimPct, "%");
        p.add_float("specular", "Especular", 40.0f, 0.0f, 100.0f, kAnimPct, "%");
        p.add_float("roughness", "Aspereza", 10.0f, 0.0f, 100.0f, kAnimPct, "%");
        p.add_float("metal", "Metal", 100.0f, 0.0f, 100.0f, kAnimPct, "%");
    }
    i32 input_layer_param() const noexcept override { return static_cast<i32>(kLayer); }
    f32 input_margin(const EffectEval& e) const noexcept override { return clampf(e.f(kSoftness), 0, 500, 10) * 1.5f + 2.0f; }
    void pipelines(std::vector<PipelineKey>& out, SurfaceFormat work) const override {
        out.push_back(PipelineKey::fullscreen(ShaderId::effects_ae_relief_frag, work));
    }
    Status build(EffectBuildContext& ctx, const EffectEval& e, const LayerImage& input, f32, LayerImage& out) const override {
        const u64 ref = e.value(kLayer).ref;
        const LayerImage* bump = ref ? ctx.layer_input(ref) : nullptr;
        if (ref && !bump) { out = input; return OkStatus; }
        const LayerImage& src = bump ? *bump : input;
        BlurRequest req;
        const f32 soft = clampf(e.f(kSoftness), 0.0f, 500.0f, 10.0f);
        req.sigmaX = req.sigmaY = soft * 0.5f;
        req.outRegion = src.region;
        req.label = "ae-plastico-relevo";
        LayerImage blurred;
        if (const Status s = build_gaussian(ctx, src, req, blurred); !s.ok()) return s;
        AeUniforms u = ae_uniforms(e, input);
        u.p[0] = Vec4{en(e, kProperty, 5), std::max(soft * 0.35f, 1.0f), clampf(e.f(kHeight), -100.0f, 100.0f, 25.0f) / 25.0f,
                      bump ? 1.0f : 0.0f};
        u.p[1] = Vec4{pct(e, kCutMin), pct(e, kCutMax), pct(e, kIntensity, 0, 2), en(e, kLightType, 2)};
        // Relevo da própria camada: no plano dela; de outra camada: px da composição.
        const Vec2 o = bump ? Vec2{0.0f, 0.0f} : plane_origin(input);
        u.p[2] = Vec4{blurred.region.x - o.x, blurred.region.y - o.y, std::max(blurred.region.w, 1e-3f),
                      std::max(blurred.region.h, 1e-3f)};
        u.p[3] = Vec4{clampf(e.f(kLightHeight), 0.0f, 90.0f, 35.0f) * kDeg2Rad, fin(e.f(kLightDirection), 45.0f) * kDeg2Rad,
                      pct(e, kAmbient), pct(e, kDiffuse)};
        u.p[4] = Vec4{pct(e, kSpecular), pct(e, kRoughness), pct(e, kMetal), 0.0f};
        u.p[11].w = 1.0f;
        u.c[0] = e.color(kLightColor);
        const PassTexture tex1{blurred.texture, {}, CommonSampler::LinearClamp};
        return ae_pass(ctx, input, u, ShaderId::effects_ae_relief_frag, "ae-plastico", CommonSampler::LinearClamp, out, &tex1, 1);
    }
};

// =============================================================================
// ae_blur.frag: bilateral (0), inteligente (1), em cruz (2)
// =============================================================================
class AeBlurEffect : public Effect {
public:
    void pipelines(std::vector<PipelineKey>& out, SurfaceFormat work) const override {
        out.push_back(PipelineKey::fullscreen(ShaderId::effects_ae_blur_frag, work));
    }
};

class BilateralBlur final : public AeBlurEffect {
public:
    enum : u32 { kRadius = 0, kThreshold, kColorize };
    const EffectInfo& info() const noexcept override {
        static const EffectInfo i{"aurea.blur.bilateral", "Desfoque bilateral", "Desfoque", EffectClass::Neighborhood};
        return i;
    }
    void declare_parameters(ParameterRegistry& p) const override {
        p.add_float("radius", "Raio", 5.0f, 0.0f, 50.0f, kAnimPx, "px");
        p.typed_range(0.0f, 300.0f);
        p.add_float("threshold", "Limiar", 20.0f, 0.0f, 100.0f, kAnimPct, "%");
        p.add_bool("colorize", "Colorir", true);
    }
    bool is_identity(const EffectEval& e) const noexcept override {
        return !(fin(e.f(kRadius), 0.0f) > 0.05f) && e.b(kColorize);
    }
    f32 input_margin(const EffectEval& e) const noexcept override { return clampf(e.f(kRadius), 0, 300, 5); }
    Status build(EffectBuildContext& ctx, const EffectEval& e, const LayerImage& input, f32, LayerImage& out) const override {
        AeUniforms u = ae_uniforms(e, input);
        u.p[0] = Vec4{clampf(e.f(kRadius), 0.0f, 300.0f, 5.0f), std::max(pct(e, kThreshold), 0.005f), flag(e.b(kColorize)), 0.0f};
        u.p[11].w = 0.0f;
        return ae_pass(ctx, input, u, ShaderId::effects_ae_blur_frag, "ae-desfoque-bilateral", CommonSampler::LinearClamp, out);
    }
};

class SmartBlur final : public AeBlurEffect {
public:
    enum : u32 { kRadius = 0, kThreshold, kQuality, kMode };
    const EffectInfo& info() const noexcept override {
        static const EffectInfo i{"aurea.blur.smart", "Desfoque inteligente", "Desfoque", EffectClass::Neighborhood};
        return i;
    }
    void declare_parameters(ParameterRegistry& p) const override {
        p.add_float("radius", "Raio", 3.0f, 0.0f, 50.0f, kAnimPx, "px");
        p.typed_range(0.0f, 300.0f);
        p.add_float("threshold", "Limiar", 25.0f, 0.0f, 100.0f, kAnimPct, "%");
        static const char* const kQualities[] = {"Baixa", "Média", "Alta"};
        p.add_enum("quality", "Qualidade", kQualities, 3, 2);
        static const char* const kModes[] = {"Normal", "Só as bordas", "Sobrepor bordas"};
        p.add_enum("mode", "Modo", kModes, 3, 0);
    }
    bool is_identity(const EffectEval& e) const noexcept override {
        return !(fin(e.f(kRadius), 0.0f) > 0.05f) && e.e(kMode) == 0;
    }
    f32 input_margin(const EffectEval& e) const noexcept override { return clampf(e.f(kRadius), 0, 300, 3); }
    Status build(EffectBuildContext& ctx, const EffectEval& e, const LayerImage& input, f32, LayerImage& out) const override {
        AeUniforms u = ae_uniforms(e, input);
        u.p[0] = Vec4{clampf(e.f(kRadius), 0.0f, 300.0f, 3.0f), std::max(pct(e, kThreshold), 0.005f), en(e, kQuality, 3),
                      en(e, kMode, 3)};
        u.p[11].w = 1.0f;
        return ae_pass(ctx, input, u, ShaderId::effects_ae_blur_frag, "ae-desfoque-inteligente", CommonSampler::LinearClamp, out);
    }
};

class CrossBlur final : public AeBlurEffect {
public:
    enum : u32 { kRadiusX = 0, kRadiusY, kTransfer, kRepeat };
    const EffectInfo& info() const noexcept override {
        static const EffectInfo i{"aurea.blur.cross", "Desfoque em cruz", "Desfoque", EffectClass::Neighborhood};
        return i;
    }
    void declare_parameters(ParameterRegistry& p) const override {
        p.add_float("radius_x", "Raio X", 10.0f, 0.0f, 100.0f, kAnimPx, "px");
        p.typed_range(0.0f, 1000.0f);
        p.add_float("radius_y", "Raio Y", 10.0f, 0.0f, 100.0f, kAnimPx, "px");
        p.typed_range(0.0f, 1000.0f);
        static const char* const kModes[] = {"Nenhum", "Somar", "Tela", "Mais claro", "Mais escuro", "Multiplicar"};
        p.add_enum("transfer_mode", "Modo de transferência", kModes, 6, 0);
        p.add_bool("repeat_edge", "Repetir pixels da borda", true);
    }
    bool is_identity(const EffectEval& e) const noexcept override {
        return !(fin(e.f(kRadiusX), 0.0f) > 0.05f) && !(fin(e.f(kRadiusY), 0.0f) > 0.05f) && e.e(kTransfer) == 0;
    }
    f32 input_margin(const EffectEval& e) const noexcept override {
        return std::max(clampf(e.f(kRadiusX), 0, 1000, 10), clampf(e.f(kRadiusY), 0, 1000, 10));
    }
    Status build(EffectBuildContext& ctx, const EffectEval& e, const LayerImage& input, f32, LayerImage& out) const override {
        AeUniforms u = ae_uniforms(e, input);
        u.p[0] = Vec4{clampf(e.f(kRadiusX), 0.0f, 1000.0f, 10.0f), clampf(e.f(kRadiusY), 0.0f, 1000.0f, 10.0f), en(e, kTransfer, 6),
                      flag(e.b(kRepeat))};
        u.p[11].w = 2.0f;
        return ae_pass(ctx, input, u, ShaderId::effects_ae_blur_frag, "ae-desfoque-cruz",
                       e.b(kRepeat) ? CommonSampler::LinearClamp : CommonSampler::LinearBorder, out);
    }
};

} // namespace

void register_stylize(EffectRegistry& r) {
    (void)r.add(std::make_unique<WarpStyles>());
    (void)r.add(std::make_unique<Emboss>(false));
    (void)r.add(std::make_unique<Emboss>(true));
    (void)r.add(std::make_unique<RadialShadow>());
    (void)r.add(std::make_unique<InnerShadow>());
    (void)r.add(std::make_unique<BevelEdges>());
    (void)r.add(std::make_unique<BrushStrokes>());
    (void)r.add(std::make_unique<SimpleWireRemoval>());
    (void)r.add(std::make_unique<Texturize>());
    (void)r.add(std::make_unique<Plastic>());
    (void)r.add(std::make_unique<BilateralBlur>());
    (void)r.add(std::make_unique<SmartBlur>());
    (void)r.add(std::make_unique<CrossBlur>());
}

} // namespace aurea::builtin::ae26
