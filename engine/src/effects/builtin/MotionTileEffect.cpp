// =============================================================================
//  Motion Tile — o visual e os controles do app antigo, nativos no Aurea.
//
//  Do app antigo vem a conta do ladrilho: a grade presa à layer, cada ladrilho
//  com a layer INTEIRA, o centro do ladrilho em "Centro", a fase acumulada por
//  linha (ou por coluna), o espelho nos ímpares, a trava de meio texel e a
//  janela de saída centrada no QUADRO.
//
//  Do Aurea fica a forma de entregar isso:
//    - a entrada é a textura de trabalho da layer, já na densidade do frame;
//    - a região ladrilhada é CENTRADA NO CENTRO DA LAYER e só cresce para fora,
//      até cobrir o quadro depois do transform (a inversa da matriz da layer);
//    - a textura de saída cobre só a parte da região que o quadro mostra
//      (mais a margem que os efeitos seguintes leem);
//    - a composição desenha essa textura com a MESMA matriz da layer, então a
//      cópia central cai exatamente onde a layer estava, do mesmo tamanho.
// =============================================================================
#include "BuiltinEffects.hpp"

#include "aurea/timeline/Layer.hpp"

#include <algorithm>
#include <cmath>

namespace aurea {
namespace motion_tile {

namespace {
f32 finite_or(f32 v, f32 fallback) noexcept { return std::isfinite(v) ? v : fallback; }
bool near(f32 a, f32 b) noexcept { return std::fabs(a - b) < 1e-4f; }
/// Meia janela "desligada": maior que qualquer distância ao centro do quadro.
constexpr f32 kNoWindowHalf = 1.0e6f;
} // namespace

bool Params::identity_params() const noexcept {
    return near(tileX, 1.0f) && near(tileY, 1.0f) && outputX >= 1.0f - 1e-4f && outputY >= 1.0f - 1e-4f
        && near(centerX, 0.5f) && near(centerY, 0.5f) && near(phaseTurns, 0.0f) && !mirror && !legacyClamp;
}

Params params_from(const EffectEval& e) noexcept {
    Params p;
    const Vec2 c = e.p2(kCenter);
    p.centerX = finite_or(c.x, 0.5f);
    p.centerY = finite_or(c.y, 0.5f);
    // Faixas do app antigo no slider: ladrilho 1%..500%, saída 0%..500%.
    // Digitado, o ladrilho vai a 1000% (kMaxTileScale).
    p.tileX = std::clamp(finite_or(e.f(kTileWidth), 100.0f) / 100.0f, 0.01f, kMaxTileScale);
    p.tileY = std::clamp(finite_or(e.f(kTileHeight), 100.0f) / 100.0f, 0.01f, kMaxTileScale);
    p.outputX = std::clamp(finite_or(e.f(kOutputWidth), 100.0f) / 100.0f, 0.0f, kMaxOutput);
    p.outputY = std::clamp(finite_or(e.f(kOutputHeight), 100.0f) / 100.0f, 0.0f, kMaxOutput);
    p.mirror = e.b(kMirror);
    p.legacyClamp = e.count > kLegacyClamp && e.b(kLegacyClamp);
    p.horizontalPhase = e.b(kHorizontalPhase);
    p.phaseTurns = finite_or(e.f(kPhase), 0.0f) / 360.0f;
    return p;
}

Vec2 coverage_factors(const Params&, const LayerPlacement& pl) noexcept {
    const Vec2 none{1.0f, 1.0f};
    const f32 w = static_cast<f32>(pl.layerWidth);
    const f32 h = static_cast<f32>(pl.layerHeight);
    if (w <= 0.0f || h <= 0.0f || pl.compWidth == 0 || pl.compHeight == 0) return none;

    // A INVERSA DA TRANSFORMAÇÃO, e não uma aproximação. A layer vai à
    // composição por q = M p; para a região cobrir o quadro, os QUATRO CANTOS
    // do quadro, levados de volta ao plano da layer (p = M⁻¹ q), têm de cair
    // dentro dela. Um canto é sempre o pior caso de uma transformação linear.
    const Mat4& m = pl.compFromLayer;
    const f32 a = m.col[0].x, b = m.col[0].y, c = m.col[1].x, d = m.col[1].y;
    const f32 tx = m.col[3].x, ty = m.col[3].y;
    const f32 det = a * d - b * c;
    if (!std::isfinite(det) || std::fabs(det) < 1e-9f) return none;   // escala zero: não cresce
    const f32 inv = 1.0f / det;

    const f32 cw = static_cast<f32>(pl.compWidth), ch = static_cast<f32>(pl.compHeight);
    const f32 corners[4][2] = {{0, 0}, {cw, 0}, {0, ch}, {cw, ch}};
    f32 maxX = 0.0f, maxY = 0.0f;
    for (const auto& q : corners) {
        const f32 qx = q[0] - tx, qy = q[1] - ty;
        const f32 lx = ( d * qx - c * qy) * inv;
        const f32 ly = (-b * qx + a * qy) * inv;
        // Distância ao CENTRO da layer (a cópia central é a layer; as cópias
        // nascem para fora dela).
        maxX = std::max(maxX, std::fabs(lx - w * 0.5f));
        maxY = std::max(maxY, std::fabs(ly - h * 0.5f));
    }
    auto grow = [](f32 need) noexcept {
        const f32 v = std::isfinite(need) ? std::max(1.0f, need) : 1.0f;
        return std::clamp(v, 1.0f, kMaxCoverage);
    };
    return Vec2{grow(2.0f * maxX / w), grow(2.0f * maxY / h)};
}

Rect tiled_region(const Params& p, const LayerPlacement& pl) noexcept {
    const f32 w = static_cast<f32>(pl.layerWidth);
    const f32 h = static_cast<f32>(pl.layerHeight);
    const Vec2 f = coverage_factors(p, pl);
    const f32 rw = w * f.x, rh = h * f.y;
    return Rect{w * 0.5f - rw * 0.5f, h * 0.5f - rh * 0.5f, rw, rh};
}

Vec2 reference_lookup(const Params& p, Vec2 pos) noexcept {
    const f32 tx = std::max(p.tileX, 1e-4f), ty = std::max(p.tileY, 1e-4f);
    // Em unidades de LADRILHO: 0 no centro de um ladrilho, ±0,5 nas bordas.
    f32 ux = (pos.x - p.centerX) / tx;
    f32 uy = (pos.y - p.centerY) / ty;
    if (p.legacyClamp) return Vec2{std::clamp(ux + 0.5f, 0.0f, 1.0f), std::clamp(uy + 0.5f, 0.0f, 1.0f)};
    if (p.horizontalPhase) uy += p.phaseTurns * std::floor(ux + 0.5f);
    else                   ux += p.phaseTurns * std::floor(uy + 0.5f);
    const f32 cx = std::floor(ux + 0.5f), cy = std::floor(uy + 0.5f);
    f32 fx = ux - cx, fy = uy - cy;
    if (p.mirror) {
        if (std::fmod(std::fabs(cx), 2.0f) >= 0.5f) fx = -fx;
        if (std::fmod(std::fabs(cy), 2.0f) >= 0.5f) fy = -fy;
    }
    return Vec2{0.5f + fx, 0.5f + fy};
}

bool inside_output(const Params& p, Vec2 uv) noexcept {
    const f32 hx = p.outputX >= 1.0f ? kNoWindowHalf : p.outputX * 0.5f;
    const f32 hy = p.outputY >= 1.0f ? kNoWindowHalf : p.outputY * 0.5f;
    return std::fabs(uv.x - 0.5f) <= hx && std::fabs(uv.y - 0.5f) <= hy;
}

bool upgrade_legacy_layout(Layer& layer, EffectInstance& fx) noexcept {
    if (fx.type != effect_type_id(effect_keys::kMotionTile) || fx.params.size() != kLegacyParamCount) return false;
    auto track = [&](u32 param) { return layer.tracks.find(TrackProperty::EffectParam, fx.id, param_track_key(param, 0)); };

    // Saída: a anterior nunca recortava (só ampliava uma região que já cobria
    // o quadro), então o desenho antigo é a janela no quadro inteiro.
    for (u32 i : {static_cast<u32>(kOutputWidth), static_cast<u32>(kOutputHeight)}) {
        fx.params[i] = ParamSlot{};
        fx.params[i].constant = ParamValue::scalar(100.0f);
        const u32 key = param_track_key(i, 0);
        layer.tracks.remove_if([&](const Track& t) {
            return t.property == TrackProperty::EffectParam && t.effectIndex == fx.id && t.effectParamIndex == key;
        });
    }
    // Fase: o eixo troca (coluna em Y ↔ linha em X) e o sentido inverte.
    ParamSlot& horizontal = fx.params[kHorizontalPhase];
    horizontal.constant = ParamValue::boolean(!horizontal.constant.as_bool());
    if (Track* t = track(kHorizontalPhase)) {
        t->staticValue = t->staticValue > 0.5f ? 0.0f : 1.0f;
        for (Keyframe& k : t->keys) k.value = k.value > 0.5f ? 0.0f : 1.0f;
    }
    ParamSlot& phase = fx.params[kPhase];
    phase.constant.v[0] = -phase.constant.v[0];
    if (Track* t = track(kPhase)) {
        t->staticValue = -t->staticValue;
        for (Keyframe& k : t->keys) {
            k.value = -k.value;
            k.tangentIn = -k.tangentIn;
            k.tangentOut = -k.tangentOut;
        }
    }
    ParamSlot marker;
    marker.constant = ParamValue::scalar(1.0f);
    fx.params.push_back(marker);
    return true;
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
        // A ORDEM É O `ParamIndex` de MotionTile.hpp. Sliders nas faixas do
        // app antigo; digitado, o centro vai a ±10 layers e o ladrilho a 1000%.
        p.add_point2("tile_center", "Centro do ladrilho", Vec2{0.5f, 0.5f}, -1.0f, 2.0f,
                     kParamAnimatable | kParamRelative);
        p.typed_range(-10.0f, 10.0f);
        p.add_float("tile_width", "Largura do ladrilho", 100.0f, 1.0f, 500.0f,
                    kParamAnimatable | kParamPercent, "%");
        p.typed_range(1.0f, kMaxTileScale * 100.0f);
        p.add_float("tile_height", "Altura do ladrilho", 100.0f, 1.0f, 500.0f,
                    kParamAnimatable | kParamPercent, "%");
        p.typed_range(1.0f, kMaxTileScale * 100.0f);
        p.add_float("output_width", "Largura da saída", 100.0f, 0.0f, kMaxOutput * 100.0f,
                    kParamAnimatable | kParamPercent, "%");
        p.add_float("output_height", "Altura da saída", 100.0f, 0.0f, kMaxOutput * 100.0f,
                    kParamAnimatable | kParamPercent, "%");
        p.add_bool("mirror_edges", "Espelhar bordas", false);
        {
            // "Esticar bordas" do Motion Tile anterior: oculto, só projeto antigo.
            ParamSpec old;
            old.id = "clamp_edges";
            old.label = "Esticar bordas";
            old.type = ParamType::Bool;
            old.flags = kParamHidden;
            old.defaultValue = ParamValue::boolean(false);
            old.minValue = 0.0f;
            old.maxValue = 1.0f;
            p.add(old);
        }
        p.add_angle("phase", "Fase", 0.0f, -360.0f, 360.0f);
        p.typed_range(-36000.0f, 36000.0f);
        p.add_bool("horizontal_phase_shift", "Deslocamento de fase horizontal", false);
        // Marca da disposição atual (ver `upgrade_legacy_layout`).
        p.add_float("layout", "Disposição", 1.0f, 0.0f, 1.0f, kParamHidden);
    }
    bool demo_values(EffectInstance&, std::vector<ParamValue>& v) const noexcept override {
        using namespace motion_tile;
        v[kTileWidth] = ParamValue::scalar(34.0f);    // a miniatura do app antigo: 34%
        v[kTileHeight] = ParamValue::scalar(34.0f);
        v[kMirror] = ParamValue::boolean(true);
        return true;
    }
    void pipelines(std::vector<PipelineKey>& out, SurfaceFormat work) const override {
        out.push_back(PipelineKey::fullscreen(ShaderId::effects_motion_tile_frag, work));
    }

    bool is_identity(const EffectEval& e) const noexcept override {
        const motion_tile::Params p = motion_tile::params_from(e);
        if (!p.identity_params()) return false;
        // IDENTIDADE SÓ QUANDO A LAYER JÁ COBRE O QUADRO. Com a layer reduzida,
        // girada ou deslocada, ladrilho 100% JÁ NÃO é a própria layer: é a
        // parede que cobre a composição.
        if (!e.placement) return true;
        const Vec2 f = motion_tile::coverage_factors(p, *e.placement);
        return std::fabs(f.x - 1.0f) < 1e-4f && std::fabs(f.y - 1.0f) < 1e-4f;
    }

    Status build(EffectBuildContext& ctx, const EffectEval& e, const LayerImage& input, f32 margin,
                 LayerImage& out) const override {
        using motion_tile::kNoWindowHalf;
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
            Vec4 inset;
            Vec4 windowX;
            Vec4 windowY;
        } u{};
        // p = coordenada normalizada NA ENTRADA do pixel de saída.
        u.uvMap = EffectBuildContext::uv_map(region, input.region);
        u.tile = Vec4{std::max(p.tileX, 1e-4f), std::max(p.tileY, 1e-4f), p.centerX, p.centerY};
        u.flags = Vec4{p.mirror ? 1.0f : 0.0f, p.phaseTurns, p.horizontalPhase ? 1.0f : 0.0f,
                       p.legacyClamp ? 1.0f : 0.0f};
        // Meio texel DA TEXTURA DE ENTRADA para dentro da borda: uma emenda
        // amostra exatamente a borda, e o filtro linear ali traria a margem
        // transparente — um fio claro em toda emenda.
        u.inset = Vec4{0.5f / static_cast<f32>(std::max(input.width, 1u)),
                       0.5f / static_cast<f32>(std::max(input.height, 1u)), 0, 0};

        // A JANELA DE SAÍDA, medida no QUADRO: o uv da textura de saída vai ao
        // uv da composição por um afim (região → px da layer → px do quadro).
        // Em 100% ou mais (ou numa camada da cena 3D, sem quadro 2D) não corta.
        const f32 hx = p.outputX >= 1.0f ? kNoWindowHalf : p.outputX * 0.5f;
        const f32 hy = p.outputY >= 1.0f ? kNoWindowHalf : p.outputY * 0.5f;
        u.windowX = Vec4{0, 0, 0, kNoWindowHalf};
        u.windowY = Vec4{0, 0, 0, kNoWindowHalf};
        if (e.placement && !e.placement->inScene3d && e.placement->compWidth && e.placement->compHeight
            && (hx < kNoWindowHalf || hy < kNoWindowHalf)) {
            const Mat4& m = e.placement->compFromLayer;
            const f32 a = m.col[0].x, b = m.col[0].y, c = m.col[1].x, d = m.col[1].y;
            const f32 cw = static_cast<f32>(e.placement->compWidth);
            const f32 ch = static_cast<f32>(e.placement->compHeight);
            u.windowX = Vec4{a * region.w / cw, c * region.h / cw,
                             (a * region.x + c * region.y + m.col[3].x) / cw - 0.5f, hx};
            u.windowY = Vec4{b * region.w / ch, d * region.h / ch,
                             (b * region.x + d * region.y + m.col[3].y) / ch - 0.5f, hy};
        }

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
    // Ordem do menu "adicionar efeito". O bloco novo (Fase 7.3) entra depois
    // dos que já existiam para não mudar a ordem que o dono já conhece.
    builtin::register_transform_effect(registry);
    builtin::register_color_effects(registry);
    builtin::register_blur_effects(registry);
    builtin::register_glow_effect(registry);
    builtin::register_motion_tile_effect(registry);
    builtin::register_keying_effects(registry);
    builtin::register_expression_controls(registry);
    builtin::register_echo_effect(registry);
    builtin::register_distort_effects(registry);
    builtin::register_stylize_effects(registry);
    builtin::register_light_effects(registry);
    builtin::register_glitch_effects(registry);
    builtin::register_temporal_effects(registry);
    builtin::register_transition_effects(registry);
    builtin::register_optical_effects(registry);
    builtin::register_sampled_blur_effects(registry);
    builtin::register_pattern_effects(registry);
    builtin::register_corner_pin_effect(registry);
    builtin::register_media_lab_effects(registry);
    builtin::register_studio_light_effects(registry);
    builtin::register_tracery_effect(registry);
    // Pacote de paridade: comportamentos de movimento, transições por forma
    // e acabamento. No fim, para não mudar a ordem que o dono já conhece.
    builtin::register_motion_behavior_effects(registry);
    builtin::register_shape_transition_effects(registry);
    builtin::register_finishing_effects(registry);
    // Geradores e recorte do editor antigo, nativos. Sempre depois dos que
    // já existiam.
    builtin::register_generate_effects(registry);
    builtin::register_matte_effects(registry);
    // O VHS de Estilizar, depois de todos.
    builtin::register_vhs_look_effect(registry);
    // Mapa de profundidade (IA): no fim, depois de todos.
    builtin::register_depth_effects(registry);
    // Pacote de áudio: os efeitos de som da camada (categoria Áudio) e os
    // visuais Forma de onda, Espectro de áudio e Bolas. Sempre no fim.
    builtin::register_audio_pack_effects(registry);
    // Tremor em trancos (do app antigo). Depois de todos.
    builtin::register_twitch_effect(registry);
    // Particular (as partículas do app antigo). Depois de todos.
    builtin::register_particular_effect(registry);
}

} // namespace aurea
