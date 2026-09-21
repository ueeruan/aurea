// =============================================================================
//  Transform (efeito) — posição, escala, rotação, âncora e opacidade DENTRO da
//  pilha de efeitos.
//
//  A regra que define este efeito: QUANDO ELE É O ÚLTIMO DA PILHA, NÃO EXISTE
//  PASSE. A matriz dele é multiplicada na matriz da layer e a composição
//  amostra a textura já transformada — nenhuma textura intermediária, nenhuma
//  reamostragem extra (que borraria a imagem uma vez a mais).
//
//  Só quando há um efeito DEPOIS dele (um blur que precisa ver a imagem já
//  girada, por exemplo) é que ele vira um passe de reamostragem afim.
//
//  O Transform DA LAYER (a caixa "Transformar" de toda layer) não é este
//  efeito: ele entra direto na matriz de composição, sempre.
// =============================================================================
#include "BuiltinEffects.hpp"

#include <algorithm>
#include <cmath>

namespace aurea::builtin {
namespace {

class TransformEffect final : public Effect {
public:
    enum : u32 { kAnchor = 0, kPosition, kScale, kRotation, kOpacity };

    const EffectInfo& info() const noexcept override {
        static const EffectInfo i{effect_keys::kTransform, "Transformar", "Distorcer", EffectClass::Domain};
        return i;
    }
    void declare_parameters(ParameterRegistry& p) const override {
        p.add_point2("anchor", "Ponto de âncora", Vec2{0.5f, 0.5f}, -10.0f, 10.0f,
                     kParamAnimatable | kParamRelative);
        p.add_point2("position", "Posição", Vec2{0.5f, 0.5f}, -10.0f, 10.0f,
                     kParamAnimatable | kParamRelative);
        p.add_point2("scale", "Escala", Vec2{100.0f, 100.0f}, -10000.0f, 10000.0f,
                     kParamAnimatable | kParamPercent);
        p.add_angle("rotation", "Rotação", 0.0f);
        p.add_float("opacity", "Opacidade", 100.0f, 0.0f, 100.0f, kParamAnimatable | kParamPercent, "%");
    }
    void pipelines(std::vector<PipelineKey>& out, SurfaceFormat work) const override {
        out.push_back(PipelineKey::fullscreen(ShaderId::effects_affine_resample_frag, work));
    }

    /// Matriz no plano da layer: T(posição) · R · S · T(-âncora).
    static Mat4 matrix(const EffectEval& e) noexcept {
        const f32 w = e.placement ? static_cast<f32>(e.placement->layerWidth) : 1.0f;
        const f32 h = e.placement ? static_cast<f32>(e.placement->layerHeight) : 1.0f;
        const Vec2 a = e.p2(kAnchor);
        const Vec2 p = e.p2(kPosition);
        const Vec2 s = e.p2(kScale);
        const f32 r = e.f(kRotation) * kDeg2Rad;
        const Mat4 t = Mat4::translation(Vec3{p.x * w, p.y * h, 0.0f});
        const Mat4 rot = Mat4::from_quat(Quat::from_axis_angle(Vec3{0, 0, 1}, r));
        const Mat4 sc = Mat4::scale(Vec3{s.x / 100.0f, s.y / 100.0f, 1.0f});
        const Mat4 an = Mat4::translation(Vec3{-a.x * w, -a.y * h, 0.0f});
        return t * rot * sc * an;
    }

    bool is_identity(const EffectEval& e) const noexcept override {
        if (std::fabs(e.f(kOpacity) - 100.0f) > 1e-4f) return false;
        const Mat4 m = matrix(e);
        const Mat4 id = Mat4::identity();
        for (int c = 0; c < 4; ++c) {
            const Vec4 d = m.col[c] - id.col[c];
            if (std::fabs(d.x) > 1e-4f || std::fabs(d.y) > 1e-4f || std::fabs(d.z) > 1e-4f
                || std::fabs(d.w) > 1e-4f) {
                return false;
            }
        }
        return true;
    }

    bool fold_into_composite(const EffectEval& e, Mat4& layerMatrix, f32& opacity) const noexcept override {
        layerMatrix = matrix(e);
        opacity = std::clamp(e.f(kOpacity) / 100.0f, 0.0f, 1.0f);
        return true;
    }

    Status build(EffectBuildContext& ctx, const EffectEval& e, const LayerImage& input, f32 margin,
                 LayerImage& out) const override {
        const Mat4 m = matrix(e);
        const f32 a = m.col[0].x, b = m.col[0].y, c = m.col[1].x, d = m.col[1].y;
        const f32 tx = m.col[3].x, ty = m.col[3].y;
        const f32 det = a * d - b * c;
        if (std::fabs(det) < 1e-9f) return Errc::InvalidArgument;   // escala zero: nada a desenhar

        // Região de saída: a caixa da entrada transformada, recortada ao quadro.
        const Rect in = input.region;
        const f32 xs[2] = {in.x, in.x + in.w};
        const f32 ys[2] = {in.y, in.y + in.h};
        f32 minX = 1e30f, minY = 1e30f, maxX = -1e30f, maxY = -1e30f;
        for (f32 x : xs) for (f32 y : ys) {
            const f32 px = a * x + c * y + tx;
            const f32 py = b * x + d * y + ty;
            minX = std::min(minX, px); maxX = std::max(maxX, px);
            minY = std::min(minY, py); maxY = std::max(maxY, py);
        }
        const Rect box{minX, minY, maxX - minX, maxY - minY};
        const Rect region = spread_region(box, 0.0f, 0.0f, e.placement, margin);

        u32 w = 0, h = 0;
        ctx.region_size(region, input.texel_scale_x(), w, h);

        // uv de saída → ponto no plano (região) → inversa → uv de entrada.
        const f32 inv = 1.0f / det;
        const f32 ia = d * inv, ib = -b * inv, ic = -c * inv, id = a * inv;
        const f32 itx = -(ia * tx + ic * ty), ity = -(ib * tx + id * ty);
        // p = region.xy + uv * region.wh ; q = M^-1 p ; uvIn = (q - in.xy) / in.wh
        const f32 sx = region.w, sy = region.h;
        const f32 r00 = ia * sx / in.w, r01 = ic * sy / in.w;
        const f32 r02 = (ia * region.x + ic * region.y + itx - in.x) / in.w;
        const f32 r10 = ib * sx / in.h, r11 = id * sy / in.h;
        const f32 r12 = (ib * region.x + id * region.y + ity - in.y) / in.h;

        struct {
            Vec4 row0;
            Vec4 row1;
            Vec4 params;
        } u{};
        u.row0 = Vec4{r00, r01, r02, 0.0f};
        u.row1 = Vec4{r10, r11, r12, 0.0f};
        u.params = Vec4{std::clamp(e.f(kOpacity) / 100.0f, 0.0f, 1.0f), 0, 0, 0};

        const FGTexture tex = ctx.texture("transformar", w, h);
        if (ctx.fullscreen_pass("transformar", PassStage::Transform, tex, ShaderId::effects_affine_resample_frag,
                                {PassTexture{input.texture, {}, CommonSampler::LinearBorder}},
                                &u, sizeof(u)) == kInvalidIndex) {
            return Errc::PipelineCompileFailed;
        }
        out = LayerImage{tex, region, w, h};
        return OkStatus;
    }
};

} // namespace

void register_transform_effect(EffectRegistry& r) { (void)r.add(std::make_unique<TransformEffect>()); }

} // namespace aurea::builtin
