// =============================================================================
//  Motion Tile — a ÚNICA lógica de efeito portada do Aurea antigo.
//
//  De lá vieram: a grade do shader (período, fase, espelho, esticar), a trava
//  de meio texel, e a conta de cobertura (`fatoresQueCobremMotionTile`). NÃO
//  vieram: o render object do Flutter, a "foto" da camada (`toImageSync`), a
//  ordem de `setFloat`, o caminho de identidade que tirava foto mesmo assim.
//
//  Na engine nova:
//    - a entrada é a textura de trabalho da layer, já na densidade do frame
//      (não há foto e não há "ratio de captura" para errar);
//    - a região ladrilhada é CENTRADA NO CENTRO DA LAYER e só cresce para fora;
//    - a textura de saída cobre só a parte da região que o quadro mostra
//      (mais a margem que os efeitos seguintes leem) — com a layer em 10% de
//      escala, a região teórica é 24x a layer, mas o que se aloca é o quadro;
//    - a composição desenha essa textura com a MESMA matriz da layer, então a
//      cópia central cai exatamente onde a layer estava, do mesmo tamanho.
// =============================================================================
#include "BuiltinEffects.hpp"

#include <algorithm>
#include <cmath>

namespace aurea {
namespace motion_tile {

namespace {
f32 finite_or(f32 v, f32 fallback) noexcept { return std::isfinite(v) ? v : fallback; }
bool near(f32 a, f32 b) noexcept { return std::fabs(a - b) < 1e-4f; }
} // namespace

bool Params::identity_params() const noexcept {
    return near(tileX, 1.0f) && near(tileY, 1.0f) && near(outputX, 1.0f) && near(outputY, 1.0f)
        && near(centerX, 0.5f) && near(centerY, 0.5f) && near(phaseTurns, 0.0f) && !mirror && !clamp;
}

Params params_from(const EffectEval& e) noexcept {
    Params p;
    const Vec2 c = e.p2(kCenter);
    p.centerX = finite_or(c.x, 0.5f);
    p.centerY = finite_or(c.y, 0.5f);
    // Mesmos limites do antigo: o ladrilho vai de 1% a 300% (passa de 100% e
    // fica MAIOR que a layer — o teto antigo de 100% era o "a imagem só
    // encolhe" do relato), a saída pedida de 1% a 600%.
    p.tileX = std::clamp(finite_or(e.f(kTileWidth), 100.0f) / 100.0f, 0.01f, 3.0f);
    p.tileY = std::clamp(finite_or(e.f(kTileHeight), 100.0f) / 100.0f, 0.01f, 3.0f);
    p.outputX = std::clamp(finite_or(e.f(kOutputWidth), 100.0f) / 100.0f, 0.01f, 6.0f);
    p.outputY = std::clamp(finite_or(e.f(kOutputHeight), 100.0f) / 100.0f, 0.01f, 6.0f);
    p.mirror = e.b(kMirror);
    p.clamp = e.b(kClampEdges);
    p.horizontalPhase = e.b(kHorizontalPhase);
    p.phaseTurns = finite_or(e.f(kPhase), 0.0f) / 360.0f;
    return p;
}

Vec2 coverage_factors(const Params& p, const LayerPlacement& pl) noexcept {
    const Vec2 requested{std::clamp(p.outputX, 0.01f, 6.0f), std::clamp(p.outputY, 0.01f, 6.0f)};
    const f32 w = static_cast<f32>(pl.layerWidth);
    const f32 h = static_cast<f32>(pl.layerHeight);
    if (w <= 0.0f || h <= 0.0f || pl.compWidth == 0 || pl.compHeight == 0) return requested;

    // A INVERSA DA TRANSFORMAÇÃO, e não uma aproximação. A layer vai à
    // composição por q = M p; para a região cobrir o quadro, os QUATRO CANTOS
    // do quadro, levados de volta ao plano da layer (p = M⁻¹ q), têm de cair
    // dentro dela. Um canto é sempre o pior caso de uma transformação linear.
    const Mat4& m = pl.compFromLayer;
    const f32 a = m.col[0].x, b = m.col[0].y, c = m.col[1].x, d = m.col[1].y;
    const f32 tx = m.col[3].x, ty = m.col[3].y;
    const f32 det = a * d - b * c;
    if (!std::isfinite(det) || std::fabs(det) < 1e-9f) return requested;   // escala zero: não cresce
    const f32 inv = 1.0f / det;

    const f32 cw = static_cast<f32>(pl.compWidth), ch = static_cast<f32>(pl.compHeight);
    const f32 corners[4][2] = {{0, 0}, {cw, 0}, {0, ch}, {cw, ch}};
    f32 maxX = 0.0f, maxY = 0.0f;
    for (const auto& q : corners) {
        const f32 qx = q[0] - tx, qy = q[1] - ty;
        const f32 lx = ( d * qx - c * qy) * inv;
        const f32 ly = (-b * qx + a * qy) * inv;
        // Distância ao CENTRO da layer (a cópia central é a layer; as cópias
        // nascem para fora dela). Com a âncora no centro, é a conta antiga.
        maxX = std::max(maxX, std::fabs(lx - w * 0.5f));
        maxY = std::max(maxY, std::fabs(ly - h * 0.5f));
    }
    auto join = [](f32 req, f32 need) noexcept {
        const f32 v = std::isfinite(need) ? std::max(req, need) : req;
        return std::clamp(v, 0.01f, kMaxCoverage);
    };
    return Vec2{join(requested.x, 2.0f * maxX / w), join(requested.y, 2.0f * maxY / h)};
}

Rect tiled_region(const Params& p, const LayerPlacement& pl) noexcept {
    const f32 w = static_cast<f32>(pl.layerWidth);
    const f32 h = static_cast<f32>(pl.layerHeight);
    const Vec2 f = coverage_factors(p, pl);
    const f32 rw = w * f.x, rh = h * f.y;
    return Rect{w * 0.5f - rw * 0.5f, h * 0.5f - rh * 0.5f, rw, rh};
}

Vec2 reference_lookup(const Params& p, Vec2 pos) noexcept {
    Vec2 q{(pos.x - p.centerX) / p.tileX + 0.5f, (pos.y - p.centerY) / p.tileY + 0.5f};
    if (p.clamp) return Vec2{std::clamp(q.x, 0.0f, 1.0f), std::clamp(q.y, 0.0f, 1.0f)};
    auto mod2 = [](f32 x) noexcept { return x - 2.0f * std::floor(x * 0.5f); };
    if (p.horizontalPhase) q.x -= mod2(std::floor(q.y)) * p.phaseTurns;
    else                   q.y -= mod2(std::floor(q.x)) * p.phaseTurns;
    const f32 cx = std::floor(q.x), cy = std::floor(q.y);
    Vec2 f{q.x - cx, q.y - cy};
    if (p.mirror) {
        if (mod2(cx) >= 1.0f) f.x = 1.0f - f.x;
        if (mod2(cy) >= 1.0f) f.y = 1.0f - f.y;
    }
    return f;
}

} // namespace motion_tile

namespace builtin {
namespace {

class MotionTile final : public Effect {
public:
    const EffectInfo& info() const noexcept override {
        static const EffectInfo i{effect_keys::kMotionTile, "Motion Tile", "Estilizar", EffectClass::Domain};
        return i;
    }
    void declare_parameters(ParameterRegistry& p) const override {
        using namespace motion_tile;
        // A ORDEM É O `ParamIndex` de MotionTile.hpp.
        p.add_point2("tile_center", "Centro do mosaico", Vec2{0.5f, 0.5f}, -1.0f, 2.0f,
                     kParamAnimatable | kParamRelative);
        p.add_float("tile_width", "Largura do mosaico", 100.0f, 1.0f, 300.0f,
                    kParamAnimatable | kParamPercent, "%");
        p.add_float("tile_height", "Altura do mosaico", 100.0f, 1.0f, 300.0f,
                    kParamAnimatable | kParamPercent, "%");
        p.add_float("output_width", "Largura da saída", 100.0f, 1.0f, 600.0f,
                    kParamAnimatable | kParamPercent, "%");
        p.add_float("output_height", "Altura da saída", 100.0f, 1.0f, 600.0f,
                    kParamAnimatable | kParamPercent, "%");
        p.add_bool("mirror_edges", "Bordas espelhadas", false);
        p.add_bool("clamp_edges", "Esticar bordas", false);
        p.add_angle("phase", "Fase", 0.0f);
        p.add_bool("horizontal_phase_shift", "Deslocamento de fase horizontal", false);
    }
    void pipelines(std::vector<PipelineKey>& out, SurfaceFormat work) const override {
        out.push_back(PipelineKey::fullscreen(ShaderId::effects_motion_tile_frag, work));
    }

    bool is_identity(const EffectEval& e) const noexcept override {
        const motion_tile::Params p = motion_tile::params_from(e);
        if (!p.identity_params()) return false;
        // IDENTIDADE SÓ QUANDO A LAYER JÁ COBRE O QUADRO. Com a layer reduzida,
        // girada ou deslocada, ladrilho 100% numa saída 100% JÁ NÃO é a própria
        // layer: é a parede que cobre a composição. Tratar como identidade ali
        // devolveria o defeito antigo da moldura vazia.
        if (!e.placement) return true;
        const Vec2 f = motion_tile::coverage_factors(p, *e.placement);
        return std::fabs(f.x - 1.0f) < 1e-4f && std::fabs(f.y - 1.0f) < 1e-4f;
    }

    Status build(EffectBuildContext& ctx, const EffectEval& e, const LayerImage& input, f32 margin,
                 LayerImage& out) const override {
        const motion_tile::Params p = motion_tile::params_from(e);

        Rect region = input.region;
        if (e.placement) {
            region = motion_tile::tiled_region(p, *e.placement);
            // Só a parte que o quadro mostra (mais a margem dos efeitos
            // seguintes) vira textura.
            Rect vis = visible_layer_rect(*e.placement);
            if (vis.w > 0.0f && vis.h > 0.0f) {
                vis = Rect{vis.x - margin - 1.0f, vis.y - margin - 1.0f,
                           vis.w + 2.0f * margin + 2.0f, vis.h + 2.0f * margin + 2.0f};
                const Rect clipped = Rect::intersect(region, vis);
                region = (clipped.w > 0.5f && clipped.h > 0.5f) ? clipped : Rect{0, 0, 1, 1};
            }
        }

        u32 w = 0, h = 0;
        ctx.region_size(region, input.texel_scale_x(), w, h);

        struct {
            Vec4 uvMap;
            Vec4 tile;
            Vec4 flags;
            Vec4 halfTexel;
        } u{};
        // p = coordenada normalizada NA ENTRADA do pixel de saída.
        u.uvMap = EffectBuildContext::uv_map(region, input.region);
        u.tile = Vec4{p.tileX, p.tileY, p.centerX, p.centerY};
        u.flags = Vec4{p.mirror ? 1.0f : 0.0f, p.phaseTurns, p.horizontalPhase ? 1.0f : 0.0f,
                       p.clamp ? 1.0f : 0.0f};
        // Meio texel DA TEXTURA DE ENTRADA — não meio pixel da layer (o erro
        // antigo que achatava um pixel e meio em cada emenda).
        u.halfTexel = Vec4{0.5f / static_cast<f32>(input.width), 0.5f / static_cast<f32>(input.height), 0, 0};

        const FGTexture tex = ctx.texture("motion-tile", w, h);
        if (ctx.fullscreen_pass("motion-tile", PassStage::Effects, tex, ShaderId::effects_motion_tile_frag,
                                {PassTexture{input.texture, {}, CommonSampler::LinearClamp}},
                                &u, sizeof(u)) == kInvalidIndex) {
            return Errc::PipelineCompileFailed;
        }
        out = LayerImage{tex, region, w, h};
        return OkStatus;
    }
};

} // namespace

void register_motion_tile_effect(EffectRegistry& r) { (void)r.add(std::make_unique<MotionTile>()); }

} // namespace builtin

void register_builtin_effects(EffectRegistry& registry) {
    // Ordem do menu "adicionar efeito".
    builtin::register_transform_effect(registry);
    builtin::register_color_effects(registry);
    builtin::register_blur_effects(registry);
    builtin::register_glow_effect(registry);
    builtin::register_motion_tile_effect(registry);
    builtin::register_keying_effects(registry);
}

} // namespace aurea
