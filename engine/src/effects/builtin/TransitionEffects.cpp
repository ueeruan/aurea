#include "BuiltinEffects.hpp"
#include <algorithm>

namespace aurea::builtin {
namespace {
// Native alpha transitions. Coordinates stay in layer space, including cropped
// preview tiles, so changing preview resolution cannot move the transition.
class Wipe final : public Effect {
public:
    explicit Wipe(u32 mode) : mode_(mode) {}
    const EffectInfo& info() const noexcept override {
        static const EffectInfo infos[] = {
            {effect_keys::kLinearWipe, "Varredura linear", "Transições", EffectClass::Domain},
            {effect_keys::kRadialWipe, "Varredura radial", "Transições", EffectClass::Domain},
            {effect_keys::kBlockDissolve, "Dissolver em blocos", "Transições", EffectClass::Domain}
        };
        return infos[mode_];
    }
    void declare_parameters(ParameterRegistry& p) const override {
        p.add_float("completion", "Conclusão", 50.f, 0.f, 100.f, kParamAnimatable | kParamPercent, "%");
        p.add_angle("angle", "Ângulo inicial", 0.f);
        p.add_float("feather", "Suavidade", 0.f, 0.f, 100.f, kParamAnimatable | kParamPercent, "%");
        p.add_bool("reverse", "Inverter direção", false);
        if (mode_ == 1) {
            p.add_float("center_x", "Centro X", 50.f, -100.f, 200.f, kParamAnimatable | kParamPercent, "%");
            p.add_float("center_y", "Centro Y", 50.f, -100.f, 200.f, kParamAnimatable | kParamPercent, "%");
        } else if (mode_ == 2) {
            p.add_float("block_width", "Largura do bloco", 32.f, 1.f, 1024.f, kParamAnimatable | kParamPixels, "px");
            p.add_float("block_height", "Altura do bloco", 32.f, 1.f, 1024.f, kParamAnimatable | kParamPixels, "px");
            p.add_int("seed", "Semente", 1, 0, 65535);
        }
    }
    bool is_identity(const EffectEval& e) const noexcept override { return e.f(0) <= 0.f; }
    void pipelines(std::vector<PipelineKey>& out, SurfaceFormat work) const override {
        out.push_back(PipelineKey::fullscreen(ShaderId::effects_wipe_frag, work));
    }
    Status build(EffectBuildContext& ctx, const EffectEval& e, const LayerImage& input,
                 f32, LayerImage& out) const override {
        auto u = base_uniforms(input);
        u.p0 = {std::clamp(e.f(0) / 100.f, 0.f, 1.f), e.f(1), e.f(2) / 100.f, e.b(3) ? 1.f : 0.f};
        u.p1 = {static_cast<f32>(mode_), mode_ == 0 ? 0.f : e.f(4), mode_ == 0 ? 0.f : e.f(5), mode_ == 2 ? static_cast<f32>(e.e(6)) : 0.f};
        u.p2 = {input.region.x, input.region.y, input.region.w, input.region.h};
        u.p3 = {e.placement ? static_cast<f32>(e.placement->layerWidth) : input.region.w,
                e.placement ? static_cast<f32>(e.placement->layerHeight) : input.region.h, 0.f, 0.f};
        return single_pass(ctx, ShaderId::effects_wipe_frag, input, u, "wipe", out);
    }
private:
    u32 mode_;
};
}
void register_transition_effects(EffectRegistry& r) {
    for (u32 mode = 0; mode < 3; ++mode) (void)r.add(std::make_unique<Wipe>(mode));
}
}
