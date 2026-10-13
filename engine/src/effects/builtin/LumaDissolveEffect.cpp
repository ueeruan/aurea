// =============================================================================
//  Luma Dissolve (Transição) — a camada some (ou aparece) pelo brilho.
//
//  Um MAPA 0..1 por pixel decide a ordem: o brilho da própria camada (os
//  escuros somem primeiro), um ruído, um degradê linear ou radial, ou o
//  brilho de OUTRA camada (no lugar onde ela está na composição; o renderer
//  a desenha mesmo com o olho desligado). A frente anda com o Progresso; a
//  suavidade é a rampa da borda e a borda pode ganhar uma cor (a queimadura).
//
//  Contrato das transições do app: Progresso 0% = a camada EXATA (o efeito
//  sai da cadeia), 100% = vazia. No modo "Aparecer" o sentido inverte (0% =
//  vazia, 100% = inteira), para entrar com o mesmo efeito. Numa camada de
//  ajuste, dissolve o que está abaixo.
//  Os índices de parâmetro ficam fixos nos projetos salvos (acrescentar só
//  no fim).
// =============================================================================
#include "CCCommon.hpp"

#include <algorithm>
#include <cmath>
#include <memory>

namespace aurea::builtin {
namespace {
using namespace cc;

class LumaDissolve final : public Effect {
public:
    enum : u32 { kProgress = 0, kSoftness, kInvert, kMap, kMapLayer, kNoiseScale, kSeed, kAngle,
                 kEdgeWidth, kEdgeColor, kMode, kCenter };
    enum : u32 { kMapLuma = 0, kMapNoise, kMapLinear, kMapRadial, kMapLayerLuma };
    enum : u32 { kVanish = 0, kAppear };

    const EffectInfo& info() const noexcept override {
        static const EffectInfo i{effect_keys::kLumaDissolve, "Luma Dissolve", "Transição", EffectClass::Neighborhood};
        return i;
    }

    void declare_parameters(ParameterRegistry& p) const override {
        static const char* const kMaps[] = {"Luminância da camada", "Ruído", "Gradiente linear", "Gradiente radial", "Outra camada"};
        static const char* const kModes[] = {"Sumir", "Aparecer"};
        p.add_float("progress", "Progresso", 50.0f, 0.0f, 100.0f, kAnimPct, "%");
        p.add_float("softness", "Suavidade da borda", 10.0f, 0.0f, 100.0f, kAnimPct, "%");
        p.add_bool("invert", "Inverter", false);
        p.add_enum("map", "Mapa", kMaps, 5, kMapLuma);
        p.add_layer_ref("map_layer", "Camada do mapa");
        p.add_float("noise_scale", "Escala do ruído", 60.0f, 2.0f, 500.0f, kAnimPx, "px");
        p.typed_range(1.0f, 10000.0f);
        p.add_int("seed", "Semente do ruído", 0, 0, 1000);
        p.add_angle("angle", "Ângulo do gradiente", 0.0f);
        p.add_float("edge_width", "Largura da borda", 0.0f, 0.0f, 100.0f, kAnimPct, "%");
        p.add_color("edge_color", "Cor da borda", Vec4{1.0f, 0.55f, 0.15f, 1.0f});
        p.add_enum("mode", "Modo", kModes, 2, kVanish);
        p.add_point2("center", "Centro do gradiente", Vec2{0.5f, 0.5f}, -1.0f, 2.0f, kAnimRel);
        p.typed_range(-10.0f, 10.0f);
    }

    bool demo_values(EffectInstance&, std::vector<ParamValue>& v) const noexcept override {
        // A prévia do catálogo: metade do caminho, com a borda acesa.
        v[kProgress] = ParamValue::scalar(45.0f);
        v[kSoftness] = ParamValue::scalar(8.0f);
        v[kEdgeWidth] = ParamValue::scalar(6.0f);
        return true;
    }

    i32 input_layer_param() const noexcept override { return static_cast<i32>(kMapLayer); }

    void pipelines(std::vector<PipelineKey>& out, SurfaceFormat work) const override {
        out.push_back(PipelineKey::fullscreen(ShaderId::effects_luma_dissolve_frag, work));
    }

    /// Progresso já no sentido "sumir" (0 = inteira, 1 = vazia).
    static f32 vanish(const EffectEval& e) noexcept {
        const f32 t = clampf(e.f(kProgress), 0.0f, 100.0f, 50.0f) / 100.0f;
        return std::min<u32>(e.e(kMode), 1u) == kAppear ? 1.0f - t : t;
    }

    bool is_identity(const EffectEval& e) const noexcept override { return !(vanish(e) > 0.0f); }

    Status build(EffectBuildContext& ctx, const EffectEval& e, const LayerImage& in, f32, LayerImage& out) const override {
        u32 kind = std::min<u32>(e.e(kMap), kMapLayerLuma);
        const u64 ref = kind == kMapLayerLuma ? e.value(kMapLayer).ref : 0;
        const LayerImage* map = ref ? ctx.layer_input(ref) : nullptr;
        // "Outra camada" sem camada escolhida (ou fora do tempo dela): o
        // brilho da própria camada, em vez de sumir tudo de uma vez.
        if (kind == kMapLayerLuma && !map) kind = kMapLuma;
        const f32 progress = vanish(e);
        const f32 soft = clampf(e.f(kSoftness), 0.0f, 100.0f, 10.0f) / 100.0f;
        const f32 edge = clampf(e.f(kEdgeWidth), 0.0f, 100.0f, 0.0f) / 100.0f;
        const Rect box = layer_box(e, in);
        const Vec2 c = layer_point(e, in, kCenter);
        const f32 angle = finite_or(e.f(kAngle), 0.0f) * kDeg;
        // Meia extensão do degradê linear: a projeção da caixa no eixo.
        const f32 half = 0.5f * (std::fabs(box.w * std::cos(angle)) + std::fabs(box.h * std::sin(angle)));
        f32 radius = 1.0f;
        for (const f32 x : {box.x, box.x + box.w})
            for (const f32 y : {box.y, box.y + box.h}) radius = std::max(radius, std::hypot(x - c.x, y - c.y));

        Uniforms u{};
        // A frente anda de −(suavidade + borda) até 1: 0% não toca nenhum
        // pixel (nem a borda) e 100% passa de todos.
        const f32 s = std::max(soft, 1e-4f);
        u.p0 = Vec4{progress * (1.0f + s + edge) - (s + edge), s, edge, static_cast<f32>(kind)};
        u.p1 = Vec4{e.b(kInvert) ? 1.0f : 0.0f, clampf(e.f(kNoiseScale), 1.0f, 10000.0f, 60.0f),
                    static_cast<f32>(std::clamp(e.value(kSeed).as_int(), 0, 1000)), angle};
        u.p2 = Vec4{c.x, c.y, std::max(half, 0.5f), radius};
        if (e.placement) {
            const Mat4& m = e.placement->compFromLayer;
            u.p3 = Vec4{m.col[0].x, m.col[0].y, m.col[1].x, m.col[1].y};
            u.p4 = Vec4{m.col[3].x, m.col[3].y, 0.0f, 0.0f};
        } else {
            u.p3 = Vec4{1.0f, 0.0f, 0.0f, 1.0f};
        }
        if (map) {
            u.p4.z = map->region.x;
            u.p4.w = map->region.y;
            u.p5 = Vec4{std::max(map->region.w, 1e-3f), std::max(map->region.h, 1e-3f), 1.0f, 0.0f};
        }
        u.p5.w = progress >= 1.0f ? 1.0f : 0.0f;
        u.color = premultiplied_linear(e.color(kEdgeColor));
        // O slot 1 sempre tem uma textura (a própria entrada quando não há mapa).
        return run_pass(ctx, e, ShaderId::effects_luma_dissolve_frag, in, in.region, u, "luma-dissolve", out,
                        CommonSampler::LinearBorder, map ? map : &in);
    }
};

} // namespace

void register_luma_dissolve_effect(EffectRegistry& r) { (void)r.add(std::make_unique<LumaDissolve>()); }

} // namespace aurea::builtin
