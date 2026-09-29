#include "BuiltinEffects.hpp"
#include <algorithm>
#include <cmath>

namespace aurea::builtin {
namespace {

f32 finite_or(f32 v, f32 fallback) noexcept { return std::isfinite(v) ? v : fallback; }

// Original, single-pass patterns in layer coordinates. No frame-dependent
// randomness: only user-authored keyframes move the pattern.
class Pattern final : public Effect {
public:
    explicit Pattern(u32 mode) : mode_(mode) {}
    const EffectInfo& info() const noexcept override {
        static const EffectInfo infos[] = {
            {effect_keys::kStripes, "Stripes", "Pattern", EffectClass::Domain},
            {effect_keys::kRadialRays, "Radial Rays", "Pattern", EffectClass::Domain},
            {effect_keys::kGrid, "Grid", "Pattern", EffectClass::Domain}
        };
        return infos[mode_];
    }
    void declare_parameters(ParameterRegistry& p) const override {
        p.add_float("count", mode_ == 1 ? "Rays" : "Count", 12.f, 1.f, 200.f);
        p.add_float("width", "Width", mode_ == 2 ? 6.f : 50.f, 0.f, 100.f, kParamAnimatable | kParamPercent, "%");
        p.add_angle("angle", "Angle", mode_ == 0 ? 45.f : 0.f);
        p.add_float("phase", "Phase", 0.f, -100.f, 100.f);
        p.add_float("center_x", "Center X", 50.f, -200.f, 300.f, kParamAnimatable | kParamPercent, "%");
        p.add_float("center_y", "Center Y", 50.f, -200.f, 300.f, kParamAnimatable | kParamPercent, "%");
        p.add_color("color", "Color", {0.05f, .65f, 1.f, 1.f});
        p.add_float("opacity", "Opacity", 100.f, 0.f, 100.f, kParamAnimatable | kParamPercent, "%");
        p.add_float("stretch", "Stretch Y", 100.f, 10.f, 1000.f, kParamAnimatable | kParamPercent, "%");
        p.add_float("feather", "Feather", 0.f, 0.f, 100.f, kParamAnimatable | kParamPercent, "%");
        static const char* modes[] = {"Normal", "Multiply", "Screen"};
        p.add_enum("blend", "Blend", modes, 3, 0);
    }
    bool is_identity(const EffectEval& e) const noexcept override { return e.f(1) <= 0.f || e.f(7) <= 0.f || e.color(6).w <= 0.f; }
    void pipelines(std::vector<PipelineKey>& out, SurfaceFormat work) const override {
        out.push_back(PipelineKey::fullscreen(ShaderId::effects_pattern_frag, work));
    }
    Status build(EffectBuildContext& ctx, const EffectEval& e, const LayerImage& input, f32, LayerImage& out) const override {
        auto u = base_uniforms(input);
        const f32 w = e.placement ? static_cast<f32>(e.placement->layerWidth) : input.region.w;
        const f32 h = e.placement ? static_cast<f32>(e.placement->layerHeight) : input.region.h;
        const f32 count = std::clamp(e.f(0), 1.f, 200.f);
        u.p0 = {input.region.x, input.region.y, input.region.w, input.region.h};
        u.p1 = {w * e.f(4) * .01f, h * e.f(5) * .01f, std::max(1.f, w / count), count};
        u.p2 = {e.f(2) * kDeg2Rad, e.f(3), std::clamp(e.f(1) * .01f, 0.f, 1.f), e.f(9) * .005f};
        u.p3 = {static_cast<f32>(mode_), e.f(7) * .01f, static_cast<f32>(e.e(10)), std::max(.1f, e.f(8) * .01f)};
        u.color = e.color(6);
        return single_pass(ctx, ShaderId::effects_pattern_frag, input, u, info().name, out);
    }
private:
    u32 mode_;
};

// Evaluated by the scene transform chain, not by a pixel shader.
class ParentingHelper final : public Effect {
public:
    const EffectInfo& info() const noexcept override {
        static const EffectInfo i{effect_keys::kParentingHelper, "Parenting Helper", "Transform", EffectClass::Domain};
        return i;
    }
    void declare_parameters(ParameterRegistry& p) const override {
        p.add_float("rotation", "Inherit Rotation", 100.f, -200.f, 200.f, kParamAnimatable | kParamPercent, "%");
        p.add_float("scale", "Inherit Scale", 100.f, 0.f, 200.f, kParamAnimatable | kParamPercent, "%");
    }
    bool is_identity(const EffectEval&) const noexcept override { return true; }
};

class Text3DLayout final : public Effect {
public:
    const EffectInfo& info() const noexcept override {
        static const EffectInfo i{effect_keys::kText3DLayout, "Text 3D Layout", "3D", EffectClass::Domain};
        return i;
    }
    void declare_parameters(ParameterRegistry& p) const override {
        p.add_angle("rotation_x", "Letter Rotation X", 0.f);
        p.add_angle("rotation_y", "Letter Rotation Y", 0.f);
        p.add_angle("rotation_z", "Letter Rotation Z", 0.f);
        p.add_angle("bend", "Cylinder Bend", 0.f, -360.f, 360.f);
        p.add_float("spacing", "Letter Spacing", 100.f, 10.f, 500.f, kParamAnimatable | kParamPercent, "%");
        p.add_angle("twist", "Twist", 0.f, -720.f, 720.f);
        p.add_int("first", "First Letter", 1, 1, 256);
        p.add_int("last", "Last Letter", 256, 1, 256);
        p.add_float("amount", "Amount", 100.f, 0.f, 100.f, kParamAnimatable | kParamPercent, "%");
        p.add_float("letter_delay", "Delay per Letter", 0.f, 0.f, 30.f, kParamAnimatable, "frames");
        p.add_float("rotation_variation", "Rotation Variation", 0.f, 0.f, 100.f, kParamAnimatable | kParamPercent, "%");
        p.add_int("seed", "Variation Seed", 1, 0, 9999);
        // Anexados (os índices acima são os dos projetos salvos): cada letra
        // gira e anda do SEU jeito, todas ao mesmo tempo; velocidade 0 = parado.
        p.add_float("random", "Random", 0.f, 0.f, 100.f, kParamAnimatable | kParamPercent, "%");
        p.add_float("random_speed", "Random Speed", 1.f, 0.f, 10.f, kParamAnimatable);
    }
    bool is_identity(const EffectEval&) const noexcept override { return true; }
};

// Shape 3D Layout: o MESMO sistema do Text 3D Layout (mesmos índices, a
// mesma conta em scene3d::apply_node_layout), com cada PARTE da forma 3D no
// papel de uma letra. "Espalhar" afasta as partes do centro nos três eixos.
class Shape3DLayout final : public Effect {
public:
    const EffectInfo& info() const noexcept override {
        static const EffectInfo i{effect_keys::kShape3DLayout, "Shape 3D Layout", "3D", EffectClass::Domain};
        return i;
    }
    void declare_parameters(ParameterRegistry& p) const override {
        p.add_angle("rotation_x", "Part Rotation X", 0.f);
        p.add_angle("rotation_y", "Part Rotation Y", 0.f);
        p.add_angle("rotation_z", "Part Rotation Z", 0.f);
        p.add_angle("bend", "Bend", 0.f, -360.f, 360.f);
        p.add_float("spread", "Spread", 100.f, 10.f, 500.f, kParamAnimatable | kParamPercent, "%");
        p.add_angle("twist", "Twist", 0.f, -720.f, 720.f);
        p.add_int("first", "First Part", 1, 1, 256);
        p.add_int("last", "Last Part", 256, 1, 256);
        p.add_float("amount", "Amount", 100.f, 0.f, 100.f, kParamAnimatable | kParamPercent, "%");
        p.add_float("part_delay", "Delay per Part", 0.f, 0.f, 30.f, kParamAnimatable, "frames");
        p.add_float("rotation_variation", "Rotation Variation", 0.f, 0.f, 100.f, kParamAnimatable | kParamPercent, "%");
        p.add_int("seed", "Variation Seed", 1, 0, 9999);
        p.add_float("random", "Random", 0.f, 0.f, 100.f, kParamAnimatable | kParamPercent, "%");
        p.add_float("random_speed", "Random Speed", 1.f, 0.f, 10.f, kParamAnimatable);
    }
    bool is_identity(const EffectEval&) const noexcept override { return true; }
};

// -----------------------------------------------------------------------------
// Xadrez — as células alternadas da malha, pintadas com a cor do padrão. O
// tamanho é em pixels da CAMADA, então o mesmo número vale no preview e no
// export e continua valendo quando a camada é escalada.
// -----------------------------------------------------------------------------
class Checkerboard final : public Effect {
public:
    enum : u32 { kWidth = 0, kHeight, kAnchor, kRotation, kFeather, kInvert, kOpacity, kColor };

    const EffectInfo& info() const noexcept override {
        static const EffectInfo i{effect_keys::kCheckerboard, "Xadrez", "Pattern", EffectClass::Domain};
        return i;
    }
    void declare_parameters(ParameterRegistry& p) const override {
        p.add_float("width", "Largura da célula", 60.f, 2.f, 2000.f, kParamAnimatable | kParamPixels, "px");
        p.typed_range(0.5f, 20000.f);
        p.add_float("height", "Altura da célula", 60.f, 2.f, 2000.f, kParamAnimatable | kParamPixels, "px");
        p.typed_range(0.5f, 20000.f);
        p.add_point2("anchor", "Âncora", Vec2{0.5f, 0.5f}, -2.0f, 3.0f, kParamAnimatable | kParamRelative);
        p.typed_range(-20.0f, 20.0f);
        p.add_angle("rotation", "Rotação", 0.f);
        p.add_float("feather", "Suavidade da borda", 0.f, 0.f, 200.f, kParamAnimatable | kParamPixels, "px");
        p.typed_range(0.f, 2000.f);
        p.add_bool("invert", "Inverter", false);
        p.add_float("opacity", "Opacidade", 100.f, 0.f, 100.f, kParamAnimatable | kParamPercent, "%");
        p.add_color("color", "Cor", Vec4{0.05f, 0.65f, 1.f, 1.f});
    }
    bool is_identity(const EffectEval& e) const noexcept override {
        return e.f(kOpacity) <= 0.f || e.color(kColor).w <= 0.f || e.f(kWidth) <= 0.f || e.f(kHeight) <= 0.f;
    }
    bool demo_values(EffectInstance&, std::vector<ParamValue>& v) const noexcept override {
        v[kWidth] = ParamValue::scalar(90.0f);
        v[kHeight] = ParamValue::scalar(90.0f);
        return true;
    }
    void pipelines(std::vector<PipelineKey>& out, SurfaceFormat work) const override {
        out.push_back(PipelineKey::fullscreen(ShaderId::effects_checkerboard_frag, work));
    }
    Status build(EffectBuildContext& ctx, const EffectEval& e, const LayerImage& input, f32,
                 LayerImage& out) const override {
        EffectUniforms u = base_uniforms(input);
        // Pixels da camada → uv: a densidade da entrada diz quantos texels há
        // por pixel dela, e a textura quantos texels tem a imagem inteira.
        const f32 uvPerPxX = input.width > 0 ? input.texel_scale_x() / static_cast<f32>(input.width) : 0.f;
        const f32 uvPerPxY = input.height > 0 ? input.texel_scale_y() / static_cast<f32>(input.height) : 0.f;
        const Vec2 a = e.p2(kAnchor);
        u.p0 = Vec4{std::max(finite_or(e.f(kWidth), 60.f), 0.5f) * uvPerPxX,
                    std::max(finite_or(e.f(kHeight), 60.f), 0.5f) * uvPerPxY, a.x, a.y};
        u.p1 = Vec4{e.f(kRotation) * kDeg2Rad, std::max(finite_or(e.f(kFeather), 0.f), 0.f) * uvPerPxY,
                    e.b(kInvert) ? 1.f : 0.f, std::clamp(e.f(kOpacity) / 100.f, 0.f, 1.f)};
        u.color = e.color(kColor);
        return single_pass(ctx, ShaderId::effects_checkerboard_frag, input, u, "xadrez", out);
    }
};

// -----------------------------------------------------------------------------
// Matriz hexagonal — o CONTORNO das células de uma malha de favos. O tamanho é
// a largura de face a face, em pixels da camada, e o traço também: o painel
// continua com o mesmo desenho em qualquer zoom.
// -----------------------------------------------------------------------------
class HexagonalArray final : public Effect {
public:
    enum : u32 { kSize = 0, kAnchor, kRotation, kBorder, kFeather, kInvert, kOpacity, kColor };

    const EffectInfo& info() const noexcept override {
        static const EffectInfo i{effect_keys::kHexagonalArray, "Matriz hexagonal", "Pattern",
                                  EffectClass::Domain};
        return i;
    }
    void declare_parameters(ParameterRegistry& p) const override {
        p.add_float("size", "Tamanho da célula", 80.f, 4.f, 4000.f, kParamAnimatable | kParamPixels, "px");
        p.typed_range(1.f, 40000.f);
        p.add_point2("anchor", "Âncora", Vec2{0.5f, 0.5f}, -2.0f, 3.0f, kParamAnimatable | kParamRelative);
        p.typed_range(-20.0f, 20.0f);
        p.add_angle("rotation", "Rotação", 0.f);
        p.add_float("border", "Largura do traço", 6.f, 0.5f, 400.f, kParamAnimatable | kParamPixels, "px");
        p.typed_range(0.1f, 4000.f);
        p.add_float("feather", "Suavidade da borda", 0.f, 0.f, 200.f, kParamAnimatable | kParamPixels, "px");
        p.typed_range(0.f, 2000.f);
        p.add_bool("invert", "Inverter", false);
        p.add_float("opacity", "Opacidade", 100.f, 0.f, 100.f, kParamAnimatable | kParamPercent, "%");
        p.add_color("color", "Cor", Vec4{0.05f, 0.65f, 1.f, 1.f});
    }
    bool is_identity(const EffectEval& e) const noexcept override {
        return e.f(kOpacity) <= 0.f || e.color(kColor).w <= 0.f || e.f(kSize) <= 0.f || e.f(kBorder) <= 0.f;
    }
    bool demo_values(EffectInstance&, std::vector<ParamValue>& v) const noexcept override {
        v[kSize] = ParamValue::scalar(120.0f);
        v[kBorder] = ParamValue::scalar(10.0f);
        return true;
    }
    void pipelines(std::vector<PipelineKey>& out, SurfaceFormat work) const override {
        out.push_back(PipelineKey::fullscreen(ShaderId::effects_hexagonal_array_frag, work));
    }
    Status build(EffectBuildContext& ctx, const EffectEval& e, const LayerImage& input, f32,
                 LayerImage& out) const override {
        EffectUniforms u = base_uniforms(input);
        const f32 uvPerPxY = input.height > 0 ? input.texel_scale_y() / static_cast<f32>(input.height) : 0.f;
        const Vec2 a = e.p2(kAnchor);
        u.p0 = Vec4{std::max(finite_or(e.f(kSize), 80.f), 1.f) * uvPerPxY, a.x, a.y,
                    std::max(finite_or(e.f(kBorder), 6.f), 0.1f) * uvPerPxY};
        u.p1 = Vec4{e.f(kRotation) * kDeg2Rad, std::max(finite_or(e.f(kFeather), 0.f), 0.f) * uvPerPxY,
                    e.b(kInvert) ? 1.f : 0.f, std::clamp(e.f(kOpacity) / 100.f, 0.f, 1.f)};
        u.color = e.color(kColor);
        return single_pass(ctx, ShaderId::effects_hexagonal_array_frag, input, u, "matriz-hexagonal", out);
    }
};

} // namespace

void register_pattern_effects(EffectRegistry& r) {
    for (u32 mode = 0; mode < 3; ++mode) (void)r.add(std::make_unique<Pattern>(mode));
    (void)r.add(std::make_unique<ParentingHelper>());
    (void)r.add(std::make_unique<Text3DLayout>());
    (void)r.add(std::make_unique<Checkerboard>());
    (void)r.add(std::make_unique<HexagonalArray>());
}

void register_shape3d_layout_effect(EffectRegistry& r) { (void)r.add(std::make_unique<Shape3DLayout>()); }

} // namespace aurea::builtin
