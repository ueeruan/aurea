// =============================================================================
//  Aurea / effects / builtin / DepthEffects.cpp
//
//  Mapa de profundidade (IA): a profundidade estimada da FONTE da camada (a
//  foto ou o quadro do vídeo), em cinza — perto claro, longe escuro.
//
//  A rede (MiDaS v2.1 small, ai/DepthEstimator) roda a 256×256 fora da GPU de
//  render, uma vez por quadro-fonte (ai/DepthMapService); o renderer entrega o
//  mapa como textura no planejamento (`EffectResources::depth_map`) e este
//  passe só o amplia na GPU (bilinear), prende nos limites e mistura com a
//  camada. A estimativa é da fonte, não do resultado dos efeitos anteriores:
//  um efeito antes dele (cor, desfoque) não muda o mapa.
// =============================================================================
#include "BuiltinEffects.hpp"

#include "aurea/effects/EffectRegistry.hpp"

#include <algorithm>
#include <cmath>
#include <memory>

namespace aurea::builtin {
namespace {

f32 finite_or(f32 v, f32 fallback) noexcept { return std::isfinite(v) ? v : fallback; }

class DepthMapEffect final : public Effect {
public:
    enum : u32 { kMix = 0, kInvert, kSmoothing };

    const EffectInfo& info() const noexcept override {
        static const EffectInfo i{effect_keys::kDepthMap, "Mapa de profundidade (IA)", "Utilitário",
                                  EffectClass::Neighborhood};
        return i;
    }
    void declare_parameters(ParameterRegistry& p) const override {
        p.add_float("mix", "Mistura", 100.0f, 0.0f, 100.0f, kParamAnimatable | kParamPercent, "%");
        p.add_bool("invert", "Inverter", false);
        p.add_float("smoothing", "Suavização", 70.0f, 0.0f, 100.0f, kParamAnimatable | kParamPercent, "%");
    }
    void pipelines(std::vector<PipelineKey>& out, SurfaceFormat work) const override {
        out.push_back(PipelineKey::fullscreen(ShaderId::effects_depth_map_frag, work));
    }
    bool is_identity(const EffectEval& e) const noexcept override {
        return !(finite_or(e.f(kMix), 100.0f) > 0.0f);
    }
    void resolve_resources(EffectEval& e) const noexcept override {
        e.aux = TextureHandle{};
        e.auxInfo = Vec4{0.0f, 1.0f, 0.0f, 0.0f};
        // Sem camada = a prévia do catálogo (o renderer usa a foto das prévias).
        if (!e.resources) return;
        DepthMapRequest req;
        req.host = e.layer;
        req.instance = e.instance;
        req.localTime = e.localTime;
        req.smoothing = std::clamp(finite_or(e.f(kSmoothing), 70.0f) / 100.0f, 0.0f, 1.0f);
        const DepthMapResult r = e.resources->depth_map(req);
        if (!r.texture.valid()) return;
        e.aux = r.texture;
        e.auxInfo = Vec4{r.lo, r.hi, 1.0f, 0.0f};
    }
    Status build(EffectBuildContext& ctx, const EffectEval& e, const LayerImage& input, f32,
                 LayerImage& out) const override {
        EffectUniforms u = base_uniforms(input);
        const bool has = e.aux.valid();
        u.p0 = Vec4{std::clamp(finite_or(e.f(kMix), 100.0f) / 100.0f, 0.0f, 1.0f), e.b(kInvert) ? 1.0f : 0.0f,
                    has ? 1.0f : 0.0f, 0.0f};
        u.p1 = Vec4{e.auxInfo.x, e.auxInfo.y, 0.0f, 0.0f};
        u.p2 = Vec4{input.region.x, input.region.y, input.region.w, input.region.h};
        // O mapa cobre a caixa NATURAL da camada (a fonte inteira).
        f32 lw = input.region.w, lh = input.region.h;
        if (e.placement && e.placement->layerWidth && e.placement->layerHeight) {
            lw = static_cast<f32>(e.placement->layerWidth);
            lh = static_cast<f32>(e.placement->layerHeight);
        }
        u.p3 = Vec4{lw, lh, 0.0f, 0.0f};
        out = input;
        out.texture = ctx.texture("profundidade", input.width, input.height);
        // Sem mapa (camada sem foto/vídeo, rede indisponível, quadro ainda
        // calculando no preview), o slot recebe a própria entrada e o shader
        // devolve a camada como está.
        const PassTexture depth = has ? PassTexture{{}, e.aux, CommonSampler::LinearClamp}
                                      : PassTexture{input.texture, {}, CommonSampler::LinearClamp};
        if (ctx.fullscreen_pass("profundidade", PassStage::Effects, out.texture, ShaderId::effects_depth_map_frag,
                                {PassTexture{input.texture, {}, CommonSampler::LinearClamp}, depth},
                                &u, sizeof(u)) == kInvalidIndex) {
            return Errc::PipelineCompileFailed;
        }
        return OkStatus;
    }
};

} // namespace

void register_depth_effects(EffectRegistry& r) {
    (void)r.add(std::make_unique<DepthMapEffect>());
}

} // namespace aurea::builtin
