#include "BuiltinEffects.hpp"
#include <algorithm>
#include <cmath>
namespace aurea::builtin {
namespace {
class Optical final : public Effect {
public:
    explicit Optical(u32 mode) : mode_(mode) {}
    const EffectInfo& info() const noexcept override {
        static const EffectInfo infos[] = {
            {effect_keys::kLensFlare, "Reflexo de lente", "Luz", EffectClass::Domain},
            {effect_keys::kRipple, "Ondulação radial", "Distorcer", EffectClass::Domain},
            {effect_keys::kOpticsCompensation, "Compensação óptica", "Distorcer", EffectClass::Domain}
        };
        return infos[mode_];
    }
    void declare_parameters(ParameterRegistry& p) const override {
        p.add_float("center_x", "Centro X", 50.f, -100.f, 200.f, kParamAnimatable | kParamPercent, "%");
        p.add_float("center_y", "Centro Y", 50.f, -100.f, 200.f, kParamAnimatable | kParamPercent, "%");
        if (mode_ == 0) {
            p.add_float("brightness", "Brilho", 100.f, 0.f, 500.f, kParamAnimatable | kParamPercent, "%");
            p.add_float("size", "Tamanho", 100.f, 1.f, 400.f, kParamAnimatable | kParamPercent, "%");
            p.add_float("ghosts", "Reflexos internos", 60.f, 0.f, 100.f, kParamAnimatable | kParamPercent, "%");
            p.add_color("tint", "Cor", {1.f, .7f, .4f, 1.f});
        } else if (mode_ == 1) {
            p.add_float("amplitude", "Amplitude", 12.f, 0.f, 128.f, kParamAnimatable | kParamPixels, "px");
            p.add_float("wavelength", "Comprimento da onda", 80.f, 2.f, 2000.f, kParamAnimatable | kParamPixels, "px");
            p.add_angle("phase", "Fase", 0.f);
            p.add_float("decay", "Atenuação", 0.f, 0.f, 10.f);
        } else {
            p.add_float("fov", "Campo de visão", 60.f, 0.f, 160.f, kParamAnimatable, "°");
            p.add_bool("reverse", "Inverter distorção", false);
        }
    }
    bool is_identity(const EffectEval& e) const noexcept override { return e.f(2) < .0001f; }
    f32 input_margin(const EffectEval& e) const noexcept override { return mode_ == 1 ? e.f(2) : 0.f; }
    void pipelines(std::vector<PipelineKey>& out, SurfaceFormat work) const override {
        out.push_back(PipelineKey::fullscreen(ShaderId::effects_optical_frag, work));
    }
    Status build(EffectBuildContext& ctx, const EffectEval& e, const LayerImage& input, f32, LayerImage& out) const override {
        auto u = base_uniforms(input);
        const f32 w = e.placement ? static_cast<f32>(e.placement->layerWidth) : input.region.w;
        const f32 h = e.placement ? static_cast<f32>(e.placement->layerHeight) : input.region.h;
        u.p0 = {e.f(0)*w/100.f, e.f(1)*h/100.f, e.f(2), e.f(3)};
        u.p1 = {static_cast<f32>(mode_), mode_ == 2 ? 0.f : e.f(4), mode_ == 1 ? e.f(5) : 0.f, 0.f};
        u.p2 = {input.region.x, input.region.y, input.region.w, input.region.h};
        u.p3 = {w, h, 0.f, 0.f};
        if (mode_ == 0) u.color = e.color(5);
        return single_pass(ctx, ShaderId::effects_optical_frag, input, u, "optical", out);
    }
private:
    u32 mode_;
};
}
void register_optical_effects(EffectRegistry& r) {
    for (u32 mode = 0; mode < 3; ++mode) (void)r.add(std::make_unique<Optical>(mode));
}
}
