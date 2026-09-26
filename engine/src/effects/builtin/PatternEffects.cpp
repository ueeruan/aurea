#include "BuiltinEffects.hpp"
#include <algorithm>

namespace aurea::builtin {
namespace {
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
    }
    bool is_identity(const EffectEval&) const noexcept override { return true; }
};
}
void register_pattern_effects(EffectRegistry& r) {
    for (u32 mode = 0; mode < 3; ++mode) (void)r.add(std::make_unique<Pattern>(mode));
    (void)r.add(std::make_unique<ParentingHelper>());
    (void)r.add(std::make_unique<Text3DLayout>());
}
}
