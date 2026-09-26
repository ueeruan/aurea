#include "BuiltinEffects.hpp"
#include <algorithm>
#include <cmath>
namespace aurea::builtin {
namespace {
class SampledBlur final : public Effect {
public:
    explicit SampledBlur(bool box) : box_(box) {}
    const EffectInfo& info() const noexcept override {
        static const EffectInfo box{effect_keys::kBoxBlur, "Desfoque de caixa", "Desfoque", EffectClass::Neighborhood};
        static const EffectInfo directional{effect_keys::kDirectionalBlur, "Desfoque direcional", "Desfoque", EffectClass::Neighborhood};
        return box_ ? box : directional;
    }
    void declare_parameters(ParameterRegistry& p) const override {
        p.add_float("radius", "Raio", 8.f, 0.f, 64.f, kParamAnimatable | kParamPixels, "px");
        p.add_angle("angle", "Ângulo", 0.f);
        p.add_bool("repeat_edges", "Estender bordas", false);
    }
    bool is_identity(const EffectEval& e) const noexcept override { return e.f(0) < .001f; }
    f32 input_margin(const EffectEval& e) const noexcept override { return std::clamp(e.f(0),0.f,64.f); }
    void pipelines(std::vector<PipelineKey>& out, SurfaceFormat work) const override {
        out.push_back(PipelineKey::fullscreen(ShaderId::effects_box_line_frag, work));
    }
    Status build(EffectBuildContext& ctx, const EffectEval& e, const LayerImage& input, f32 margin, LayerImage& out) const override {
        const f32 radius = std::clamp(e.f(0), 0.f, 64.f);
        const f32 angle = e.f(1) * kDeg2Rad;
        LayerImage current = input;
        for (u32 pass = 0; pass < (box_ ? 2u : 1u); ++pass) {
            const f32 dx = pass == 0 ? std::cos(angle) : -std::sin(angle);
            const f32 dy = pass == 0 ? std::sin(angle) : std::cos(angle);
            Rect region = e.b(2) ? current.region : spread_region(current.region, std::abs(dx)*radius,
                std::abs(dy)*radius, e.placement, margin + (box_ && pass == 0 ? radius : 0.f));
            u32 w=0,h=0;
            ctx.region_size(region,current.texel_scale_x(),w,h);
            auto u = base_uniforms(current);
            u.uvMap = EffectBuildContext::uv_map(region,current.region);
            const f32 density = std::max(.001f,std::min(1.f,current.texel_scale_x()));
            u.p0 = {dx/(current.region.w*density),dy/(current.region.h*density),radius*density,e.b(2)?1.f:0.f};
            out = LayerImage{ctx.texture("box-line",w,h),region,w,h};
            if (ctx.fullscreen_pass("box-line",PassStage::Effects,out.texture,ShaderId::effects_box_line_frag,
                {PassTexture{current.texture,{},e.b(2)?CommonSampler::LinearClamp:CommonSampler::LinearBorder}},
                &u,sizeof(u)) == kInvalidIndex) return Errc::PipelineCompileFailed;
            current = out;
        }
        return OkStatus;
    }
private:
    bool box_;
};
}
void register_sampled_blur_effects(EffectRegistry& r) {
    (void)r.add(std::make_unique<SampledBlur>(true));
    (void)r.add(std::make_unique<SampledBlur>(false));
}
}
