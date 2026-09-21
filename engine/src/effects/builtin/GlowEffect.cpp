// =============================================================================
//  Glow — brilho que vaza das áreas claras.
//
//  Três etapas, todas no FrameGraph:
//    1. `bright_pass`: separa o que passa do limiar (joelho suave), já em
//       meia resolução;
//    2. o mesmo gaussiano em pirâmide do Gaussian Blur (reaproveitado, não
//       duplicado);
//    3. `glow_combine`: soma a luz borrada à imagem, em linear.
//
//  A região cresce pelo raio (o halo passa da caixa da layer) e é recortada ao
//  que o quadro mostra.
// =============================================================================
#include "BuiltinEffects.hpp"

#include <algorithm>
#include <cmath>

namespace aurea::builtin {
namespace {

f32 srgb_to_linear(f32 c) noexcept {
    return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
}

class Glow final : public Effect {
public:
    enum : u32 { kThreshold = 0, kRadius, kIntensity, kColor };

    const EffectInfo& info() const noexcept override {
        static const EffectInfo i{effect_keys::kGlow, "Brilho", "Luz", EffectClass::Neighborhood};
        return i;
    }
    void declare_parameters(ParameterRegistry& p) const override {
        p.add_float("threshold", "Limiar", 60.0f, 0.0f, 100.0f, kParamAnimatable | kParamPercent, "%");
        p.add_float("radius", "Raio", 30.0f, 0.0f, 500.0f, kParamAnimatable | kParamPixels, "px");
        p.add_float("intensity", "Intensidade", 1.0f, 0.0f, 10.0f);
        p.add_color("color", "Cor", Vec4{1, 1, 1, 1});
    }
    void pipelines(std::vector<PipelineKey>& out, SurfaceFormat work) const override {
        out.push_back(PipelineKey::fullscreen(ShaderId::effects_bright_pass_frag, work));
        out.push_back(PipelineKey::fullscreen(ShaderId::effects_glow_combine_frag, work));
    }
    bool is_identity(const EffectEval& e) const noexcept override { return e.f(kIntensity) < 1e-4f; }
    f32 input_margin(const EffectEval& e) const noexcept override { return std::max(0.0f, e.f(kRadius)); }

    Status build(EffectBuildContext& ctx, const EffectEval& e, const LayerImage& input, f32 margin,
                 LayerImage& out) const override {
        const f32 radius = std::max(0.0f, e.f(kRadius));
        const Rect region = spread_region(input.region, radius, radius, e.placement, margin);
        const f32 k = input.texel_scale_x();

        // 1. Brilho em meia resolução.
        u32 bw = 0, bh = 0;
        ctx.region_size(region, k * 0.5f, bw, bh);
        const FGTexture bright = ctx.texture("glow-brilho", bw, bh);
        const f32 t = srgb_to_linear(std::clamp(e.f(kThreshold) / 100.0f, 0.0f, 1.0f));
        const f32 knee = std::max(1e-4f, t * 0.5f);
        struct {
            Vec4 uvMap;
            Vec4 texel;
            Vec4 knee;
        } ub{};
        ub.uvMap = EffectBuildContext::uv_map(region, input.region);
        ub.texel = Vec4{1.0f / static_cast<f32>(input.width), 1.0f / static_cast<f32>(input.height), 0, 0};
        ub.knee = Vec4{t, knee, 1.0f / (4.0f * knee), 0.0f};
        if (ctx.fullscreen_pass("glow-brilho", PassStage::Effects, bright, ShaderId::effects_bright_pass_frag,
                                {PassTexture{input.texture, {}, CommonSampler::LinearBorder}},
                                &ub, sizeof(ub)) == kInvalidIndex) {
            return Errc::PipelineCompileFailed;
        }

        // 2. Borra o brilho (mesmo construtor do Gaussian Blur).
        LayerImage brightImage{bright, region, bw, bh};
        LayerImage blurred = brightImage;
        if (radius > 0.01f) {
            BlurRequest req;
            req.sigmaX = req.sigmaY = radius / 3.0f;
            req.repeatEdges = false;
            req.outRegion = region;
            req.label = "glow";
            if (const Status s = build_gaussian(ctx, brightImage, req, blurred); !s.ok()) return s;
        }

        // 3. Soma à original, na densidade da entrada.
        u32 ow = 0, oh = 0;
        ctx.region_size(region, k, ow, oh);
        const FGTexture result = ctx.texture("glow", ow, oh);
        const Vec4 color = e.color(kColor);
        struct {
            Vec4 uvMapSrc;
            Vec4 uvMapGlow;
            Vec4 tint;
        } uc{};
        uc.uvMapSrc = EffectBuildContext::uv_map(region, input.region);
        uc.uvMapGlow = EffectBuildContext::uv_map(region, blurred.region);
        uc.tint = Vec4{color.x, color.y, color.z, e.f(kIntensity)};
        if (ctx.fullscreen_pass("glow-soma", PassStage::Effects, result, ShaderId::effects_glow_combine_frag,
                                {PassTexture{input.texture, {}, CommonSampler::LinearBorder},
                                 PassTexture{blurred.texture, {}, CommonSampler::LinearClamp}},
                                &uc, sizeof(uc)) == kInvalidIndex) {
            return Errc::PipelineCompileFailed;
        }
        out = LayerImage{result, region, ow, oh};
        return OkStatus;
    }
};

} // namespace

void register_glow_effect(EffectRegistry& r) { (void)r.add(std::make_unique<Glow>()); }

} // namespace aurea::builtin
