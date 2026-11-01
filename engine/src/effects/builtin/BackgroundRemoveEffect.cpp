// =============================================================================
//  Aurea / effects / builtin / BackgroundRemoveEffect.cpp
//
//  "Remover fundo" (Keying): o fundo de uma camada de vídeo/imagem sai em
//  todos os quadros, sem tela verde. O recorte por quadro vem do motor
//  (ai::BgRemoveService via Renderer::bg_map: cache por quadro, estabilizado
//  no tempo, salvo com o projeto); o refino da borda é do shader do Rotobrush
//  (os mesmos uniformes), então mexer nos controles não recalcula nada.
//
//  Índices gravados nos projetos: nunca reordenar; novos só no fim.
// =============================================================================
#include "BuiltinEffects.hpp"

#include "aurea/ai/BackgroundRemoval.hpp"
#include "aurea/effects/EffectRegistry.hpp"

#include <algorithm>
#include <cmath>

namespace aurea::builtin {
namespace {

class BackgroundRemove final : public Effect {
public:
    const EffectInfo& info() const noexcept override {
        static const EffectInfo i{effect_keys::kBackgroundRemove, "Remover fundo", "Keying", EffectClass::Neighborhood};
        return i;
    }
    void declare_parameters(ParameterRegistry& p) const override {
        p.add_float("threshold", "Limite", 50, 0, 100, kParamAnimatable | kParamPercent, "%");                  // 0
        p.add_float("feather", "Suavidade da borda", 10, 0, 100, kParamAnimatable | kParamPercent, "%");        // 1
        p.add_float("expand", "Contrair/expandir", 0, -49, 49, kParamAnimatable | kParamPercent, "%");          // 2
        p.add_bool("invert", "Inverter (manter o fundo)", false);                                               // 3
        p.add_float("mix", "Mistura", 100, 0, 100, kParamAnimatable | kParamPercent, "%");                      // 4
        static const char* const kViews[] = {"Final", "Máscara", "Sobreposição"};
        p.add_enum("view", "Visualização", kViews, 3, 0);                                                        // 5
        p.add_float("contrast", "Contraste", 0, 0, 100, kParamAnimatable | kParamPercent, "%");                 // 6
        p.add_float("smooth", "Suavizar", 0, 0, 100, kParamAnimatable | kParamPercent, "%");                    // 7
        p.add_float("decontaminate", "Descontaminar cores da borda", 30, 0, 100, kParamAnimatable | kParamPercent, "%"); // 8
        // Borda fina (ai::RotoRefine): px da FONTE de cada lado da borda,
        // decididos de novo na resolução do quadro. 0 = a grade 320². Não
        // animável: o refinado vai para o cache.
        p.add_float("edge_radius", "Borda fina", 6, 0, 100, kParamPixels, "px");                                // 9
        p.typed_range(0, 250);
        // Modelo: Automático (objeto em destaque, embutido, as duas plataformas)
        // ou Pessoa (segmentador nativo; sem ele, o automático).
        static const char* const kModels[] = {"Automático", "Pessoa"};
        p.add_enum("model", "Modelo", kModels, 2, ai::kBgModelAuto);                                             // 10
    }
    bool is_identity(const EffectEval& e) const noexcept override { return e.f(kMix) <= 0; }
    void pipelines(std::vector<PipelineKey>& p, SurfaceFormat work) const override {
        p.push_back(PipelineKey::fullscreen(ShaderId::effects_rotobrush_frag, work));
    }
    void resolve_resources(EffectEval& e) const noexcept override {
        if (!e.resources) return;
        DepthMapRequest req;
        req.host = e.layer;
        req.instance = e.instance;
        req.localTime = e.localTime;
        req.foreground = true;
        req.backgroundRemove = true;
        req.smoothing = 0.0f;
        req.bgModel = e.count > kModel ? e.e(kModel) : ai::kBgModelAuto;
        req.rotoEdgeRadius = e.count > kEdgeRadius ? std::max(0.f, e.f(kEdgeRadius)) : 0.f;
        req.rotoDecontaminate = e.count > kDecontaminate && e.f(kDecontaminate) > 0.f;
        const auto r = e.resources->depth_map(req);
        e.aux = r.texture;
        // x: 0 grade, 1 alfa refinado, 2 refinado com a cor do objeto; w: falhou.
        e.auxInfo = {static_cast<f32>(r.rotoRefined), 0.f, 0.f, r.failed ? 1.f : 0.f};
        e.foregroundSourceTimeUs = r.sourceTimeUs;
    }
    Status build(EffectBuildContext& ctx, const EffectEval& e, const LayerImage& input, f32, LayerImage& out) const override {
        // Export sem recorte: falha (nunca exporta o fundo que a pessoa removeu).
        if (e.auxInfo.w > 0) return Status{Errc::MediaSourceMissing, "Remover fundo: modelo ou quadro indisponivel"};
        auto u = base_uniforms(input);
        // p0: limite (contrair/expandir desloca), largura da borda, inverter, mistura.
        u.p0 = {std::clamp((e.f(kThreshold) - e.f(kExpand)) * .01f, 0.f, 1.f), std::max(.001f, e.f(kFeather) * .005f),
                e.b(kInvert) ? 1.f : 0.f, e.f(kMix) * .01f};
        // p1: mostrar máscara (pela visualização), tem máscara, visualização, contraste.
        u.p1 = {0.f, e.aux.valid() ? 1.f : 0.f, static_cast<f32>(std::min(e.e(kView), 2u)), e.f(kContrast) * .01f};
        u.p2 = {input.region.x, input.region.y, input.region.w, input.region.h};
        const f32 lw = e.placement ? static_cast<f32>(e.placement->layerWidth) : input.region.w;
        const f32 lh = e.placement ? static_cast<f32>(e.placement->layerHeight) : input.region.h;
        u.p3 = {lw, lh, e.f(kSmooth) * .04f, e.f(kDecontaminate) * .01f};
        const f32 kind = e.aux.valid() ? std::clamp(e.auxInfo.x, 0.f, 2.f) : 0.f;
        if (kind > 0.f) {
            // Refinado: Suavizar (px da camada) e Suavidade da borda (fração do
            // raio da borda fina) viram desfoque do alfa já na resolução do quadro.
            const f32 radius = e.count > kEdgeRadius ? std::max(0.f, e.f(kEdgeRadius)) : 0.f;
            const f32 blur = e.f(kSmooth) * .04f * std::max(lw, lh) / 320.f + std::clamp(e.f(kFeather) * .01f, 0.f, 1.f) * radius;
            u.color = {kind, std::isfinite(blur) ? std::max(0.f, blur) : 0.f, 0.f, 0.f};
        }
        out = input;
        out.texture = ctx.texture("RemoverFundo", input.width, input.height);
        const PassTexture mask = e.aux.valid() ? PassTexture{{}, e.aux, CommonSampler::LinearClamp}
                                               : PassTexture{input.texture, {}, CommonSampler::LinearClamp};
        if (ctx.fullscreen_pass("RemoverFundo", PassStage::Effects, out.texture, ShaderId::effects_rotobrush_frag,
                                {PassTexture{input.texture, {}, CommonSampler::LinearClamp}, mask}, &u, sizeof(u)) == kInvalidIndex)
            return Errc::PipelineCompileFailed;
        return OkStatus;
    }

private:
    static constexpr u32 kThreshold = 0, kFeather = 1, kExpand = 2, kInvert = 3, kMix = 4, kView = 5, kContrast = 6,
                         kSmooth = 7, kDecontaminate = 8, kEdgeRadius = 9, kModel = 10;
};

} // namespace

void register_background_remove_effect(EffectRegistry& r) { (void)r.add(std::make_unique<BackgroundRemove>()); }

} // namespace aurea::builtin
