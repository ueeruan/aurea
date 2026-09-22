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

#include <algorithm>
#include <cmath>

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

Status affine_pass(EffectBuildContext& ctx, const LayerImage& input, const Mat4& m,
                   const LayerPlacement* placement, f32 opacity, const char* name, f32 margin,
                   LayerImage& out) {
    const f32 a = m.col[0].x, b = m.col[0].y, c = m.col[1].x, d = m.col[1].y;
    const f32 tx = m.col[3].x, ty = m.col[3].y;
    const f32 det = a * d - b * c;
    if (std::fabs(det) < 1e-9f) return Errc::InvalidArgument;   // escala zero: não há o que desenhar

    // A região de saída é a CAIXA da entrada transformada (recortada ao que o
    // quadro mostra): com a camada em 1000% de escala, o que se aloca é o
    // quadro, não a imagem ampliada inteira.
    const Rect in = input.region;
    const f32 xs[2] = {in.x, in.x + in.w};
    const f32 ys[2] = {in.y, in.y + in.h};
    f32 minX = 1e30f, minY = 1e30f, maxX = -1e30f, maxY = -1e30f;
    for (f32 x : xs) {
        for (f32 y : ys) {
            const f32 px = a * x + c * y + tx;
            const f32 py = b * x + d * y + ty;
            minX = std::min(minX, px); maxX = std::max(maxX, px);
            minY = std::min(minY, py); maxY = std::max(maxY, py);
        }
    }
    const Rect box{minX, minY, maxX - minX, maxY - minY};
    const Rect region = spread_region(box, 0.0f, 0.0f, placement, margin);

    u32 w = 0, h = 0;
    ctx.region_size(region, input.texel_scale_x(), w, h);

    // uv de saída → ponto no plano (região) → inversa → uv de entrada.
    const f32 inv = 1.0f / det;
    const f32 ia = d * inv, ib = -b * inv, ic = -c * inv, id = a * inv;
    const f32 itx = -(ia * tx + ic * ty), ity = -(ib * tx + id * ty);
    const f32 sx = region.w, sy = region.h;
    struct {
        Vec4 row0;
        Vec4 row1;
        Vec4 params;
    } u{};
    u.row0 = Vec4{ia * sx / in.w, ic * sy / in.w, (ia * region.x + ic * region.y + itx - in.x) / in.w, 0.0f};
    u.row1 = Vec4{ib * sx / in.h, id * sy / in.h, (ib * region.x + id * region.y + ity - in.y) / in.h, 0.0f};
    u.params = Vec4{std::clamp(opacity, 0.0f, 1.0f), 0, 0, 0};

    out = LayerImage{ctx.texture(name, w, h), region, w, h};
    if (ctx.fullscreen_pass(name, PassStage::Transform, out.texture, ShaderId::effects_affine_resample_frag,
                            {PassTexture{input.texture, {}, CommonSampler::LinearBorder}},
                            &u, sizeof(u)) == kInvalidIndex) {
        return Errc::PipelineCompileFailed;
    }
    return OkStatus;
}

} // namespace aurea::builtin
