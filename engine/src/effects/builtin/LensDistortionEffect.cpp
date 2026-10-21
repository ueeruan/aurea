// =============================================================================
//  Distorção da lente (aurea.distort.lens_distortion_match)
//
//  O par do Rastreio de câmera 3D com distorção: a câmera resolvida é a SEM
//  distorção (pinhole); este efeito leva a imagem de um mundo ao outro com o
//  MESMO modelo do solve (tracking::lens_distort): Brown–Conrady radial em
//  coordenadas normalizadas pela meia altura da camada,
//      p_d = p_u·(1 + k1·r² + k2·r⁴), r = |p_u|.
//    - Remover distorção: no vídeo filmado, deixa as retas retas (a CG da
//      câmera rastreada encaixa sem mudar nada nela).
//    - Aplicar distorção: na CG (ou numa pré-composição dela), curva como a
//      lente curvou o vídeo — o vídeo fica intocado.
//  Escala aproxima/afasta em volta do centro (esconde a borda que a
//  correção abre).
// =============================================================================
#include "BuiltinEffects.hpp"
#include "aurea/effects/EffectRegistry.hpp"

#include <algorithm>
#include <cmath>

namespace aurea::builtin {
namespace {

class LensDistortion final : public Effect {
public:
    const EffectInfo& info() const noexcept override {
        static const EffectInfo i{effect_keys::kLensDistortionMatch, "Distorção da lente", "Distorcer", EffectClass::Domain};
        return i;
    }
    void declare_parameters(ParameterRegistry& p) const override {
        p.add_float("k1", "Distorção radial K1", 0.f, -1.f, 1.f, kParamAnimatable);
        p.typed_range(-2.f, 2.f);
        p.add_float("k2", "Distorção radial K2", 0.f, -1.f, 1.f, kParamAnimatable);
        p.typed_range(-2.f, 2.f);
        p.add_float("center_x", "Centro X", 50.f, -100.f, 200.f, kParamAnimatable | kParamPercent, "%");
        p.typed_range(-1000.f, 1000.f);
        p.add_float("center_y", "Centro Y", 50.f, -100.f, 200.f, kParamAnimatable | kParamPercent, "%");
        p.typed_range(-1000.f, 1000.f);
        static const char* const kDirections[] = {"Remover distorção", "Aplicar distorção"};
        p.add_enum("direction", "Direção", kDirections, 2, 0);
        p.add_float("scale", "Escala", 100.f, 10.f, 400.f, kParamAnimatable | kParamPercent, "%");
        p.typed_range(1.f, 2000.f);
    }
    /// Padrão de fábrica é neutro (k = 0): a prévia do catálogo mostra um
    /// barril forte sendo corrigido.
    bool demo_values(EffectInstance& instance, std::vector<ParamValue>& values) const noexcept override {
        (void)instance;
        if (values.size() < 6) return false;
        values[0] = ParamValue::scalar(-0.25f);
        values[1] = ParamValue::scalar(0.03f);
        return true;
    }
    bool is_identity(const EffectEval& e) const noexcept override {
        return std::fabs(e.f(0)) < 1e-6f && std::fabs(e.f(1)) < 1e-6f && std::fabs(e.f(5) - 100.f) < 1e-4f;
    }
    /// A curva traz para dentro pixels de fora do quadro visível.
    bool needs_full_input() const noexcept override { return true; }
    void pipelines(std::vector<PipelineKey>& out, SurfaceFormat work) const override {
        out.push_back(PipelineKey::fullscreen(ShaderId::effects_lens_distortion_frag, work));
    }
    Status build(EffectBuildContext& ctx, const EffectEval& e, const LayerImage& input, f32, LayerImage& out) const override {
        auto u = base_uniforms(input);
        const f32 w = e.placement ? static_cast<f32>(e.placement->layerWidth) : input.region.w;
        const f32 h = e.placement ? static_cast<f32>(e.placement->layerHeight) : input.region.h;
        auto finite = [](f32 v, f32 fallback) { return std::isfinite(v) ? v : fallback; };
        const f32 k1 = std::clamp(finite(e.f(0), 0.f), -2.f, 2.f), k2 = std::clamp(finite(e.f(1), 0.f), -2.f, 2.f);
        const f32 scale = std::clamp(finite(e.f(5), 100.f), 1.f, 2000.f) * .01f;
        u.p0 = {k1, k2, e.e(4) == 1 ? 1.f : 0.f, scale};
        u.p1 = {finite(e.f(2), 50.f) * w * .01f, finite(e.f(3), 50.f) * h * .01f, std::max(h * .5f, 1.f), 0.f};
        u.p2 = {input.region.x, input.region.y, input.region.w, input.region.h};
        return single_pass(ctx, ShaderId::effects_lens_distortion_frag, input, u, "lens-distortion", out);
    }
};

} // namespace

void register_lens_distortion_effect(EffectRegistry& r) { (void)r.add(std::make_unique<LensDistortion>()); }

} // namespace aurea::builtin
