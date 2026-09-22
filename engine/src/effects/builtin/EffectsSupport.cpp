// =============================================================================
//  Peças comuns dos efeitos novos (Fase 7.3 §22).
//
//  Cada efeito novo é: a conta no shader + o preenchimento dos uniforms. Este
//  arquivo é o que sobra disso — o passe, o mapa de uv e a escala de texel.
//
//  Sem ele, cada efeito repetiria vinte linhas de `region_size` + `uv_map` +
//  `fullscreen_pass` + tratamento de erro, e a chance de um deles esquecer a
//  escala de texel (e ficar diferente entre preview e export) seria grande.
// =============================================================================
#include "BuiltinEffects.hpp"

namespace aurea::builtin {

EffectUniforms base_uniforms(const LayerImage& input) noexcept {
    EffectUniforms u;
    u.uvMap = Vec4{1.0f, 1.0f, 0.0f, 0.0f};   // mesma região: identidade
    u.texel = Vec4{
        input.width > 0 ? 1.0f / static_cast<f32>(input.width) : 0.0f,
        input.height > 0 ? 1.0f / static_cast<f32>(input.height) : 0.0f,
        input.texel_scale_x(),
        input.texel_scale_y(),
    };
    return u;
}

Status single_pass(EffectBuildContext& ctx, ShaderId frag, const LayerImage& input,
                   const EffectUniforms& u, const char* name, LayerImage& out) {
    out = input;
    out.texture = ctx.texture(name, input.width, input.height);
    if (ctx.fullscreen_pass(name, PassStage::Effects, out.texture, frag,
                            {PassTexture{input.texture, {}, CommonSampler::LinearClamp}},
                            &u, sizeof(u)) == kInvalidIndex) {
        return Errc::PipelineCompileFailed;
    }
    return OkStatus;
}

} // namespace aurea::builtin
