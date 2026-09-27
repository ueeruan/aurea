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
            {effect_keys::kLinearWipe, "Varredura linear", "Transição", EffectClass::Domain},
            {effect_keys::kRadialWipe, "Varredura radial", "Transição", EffectClass::Domain},
            {effect_keys::kBlockDissolve, "Dissolver em blocos", "Transição", EffectClass::Domain}
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

// -----------------------------------------------------------------------------
// Transições por forma: Íris, Caixa e Persianas.
//
// Mesmo contrato das varreduras acima (conclusão 0% = camada inteira, 100% =
// vazia, EXATOS; suavidade não muda as pontas; "Inverter direção" é o
// complemento). A diferença é a forma do limiar — ver shape_wipe.frag.
// -----------------------------------------------------------------------------
class ShapeWipe final : public Effect {
public:
    enum Mode : u32 { kIris = 0, kBox, kBlinds };
    enum : u32 { kCompletion = 0, kFeather, kReverse };

    explicit ShapeWipe(u32 mode) : mode_(mode) {}
    const EffectInfo& info() const noexcept override {
        static const EffectInfo infos[] = {
            {effect_keys::kIrisWipe, "Íris", "Transição", EffectClass::Domain},
            {effect_keys::kBoxWipe, "Caixa", "Transição", EffectClass::Domain},
            {effect_keys::kVenetianBlinds, "Persianas", "Transição", EffectClass::Domain},
        };
        return infos[mode_];
    }
    void declare_parameters(ParameterRegistry& p) const override {
        p.add_float("completion", "Conclusão", 50.f, 0.f, 100.f, kParamAnimatable | kParamPercent, "%");
        p.add_float("feather", "Suavidade", 0.f, 0.f, 100.f, kParamAnimatable | kParamPercent, "%");
        p.add_bool("reverse", "Inverter direção", false);
        if (mode_ == kBlinds) {
            p.add_angle("direction", "Direção", 0.f);
            // O custo é fixo (uma conta por pixel); o teto é de legibilidade:
            // mais faixas que pixels vira ruído.
            p.add_float("count", "Faixas", 10.f, 1.f, 200.f);
            p.typed_range(1.f, 2000.f);
            return;
        }
        p.add_point2("center", "Centro", Vec2{0.5f, 0.5f}, -1.f, 2.f, kParamAnimatable | kParamRelative);
        p.typed_range(-10.f, 10.f);
        p.add_angle("rotation", "Rotação", 0.f);
        if (mode_ == kIris) p.add_int("sides", "Lados da íris", 0, 0, 16);   // 0..2 = círculo
    }
    bool is_identity(const EffectEval& e) const noexcept override { return e.f(kCompletion) <= 0.f; }
    void pipelines(std::vector<PipelineKey>& out, SurfaceFormat work) const override {
        out.push_back(PipelineKey::fullscreen(ShaderId::effects_shape_wipe_frag, work));
    }
    Status build(EffectBuildContext& ctx, const EffectEval& e, const LayerImage& input,
                 f32, LayerImage& out) const override {
        const f32 w = e.placement ? static_cast<f32>(e.placement->layerWidth) : input.region.w;
        const f32 h = e.placement ? static_cast<f32>(e.placement->layerHeight) : input.region.h;
        auto u = base_uniforms(input);
        u.p0 = {std::clamp(e.f(kCompletion) / 100.f, 0.f, 1.f), std::clamp(e.f(kFeather) / 100.f, 0.f, 1.f),
                e.b(kReverse) ? 1.f : 0.f, static_cast<f32>(mode_)};
        if (mode_ == kBlinds) {
            u.p1 = {0.f, 0.f, std::clamp(e.f(4), 1.f, 2000.f), e.f(3) * kDeg2Rad};
        } else {
            const Vec2 c = e.p2(3);
            u.p1 = {c.x * w, c.y * h, mode_ == kIris ? static_cast<f32>(e.value(5).as_int()) : 0.f,
                    e.f(4) * kDeg2Rad};
        }
        u.p2 = {input.region.x, input.region.y, input.region.w, input.region.h};
        u.p3 = {w, h, 0.f, 0.f};
        return single_pass(ctx, ShaderId::effects_shape_wipe_frag, input, u, "shape-wipe", out);
    }
private:
    u32 mode_;
};
}
void register_transition_effects(EffectRegistry& r) {
    for (u32 mode = 0; mode < 3; ++mode) (void)r.add(std::make_unique<Wipe>(mode));
}
void register_shape_transition_effects(EffectRegistry& r) {
    for (u32 mode = 0; mode < 3; ++mode) (void)r.add(std::make_unique<ShapeWipe>(mode));
}
}
