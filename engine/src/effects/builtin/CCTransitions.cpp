// =============================================================================
//  Pacote CC — transições.
//
//  Jaws · Scale Wipe · Radial Scale Wipe · Burn Film · Block Load
//
//  Referência de COMPORTAMENTO: CC Jaws, CC Scale Wipe, CC Radial ScaleWipe,
//  CC Burn Film e CC Block Load. Implementação própria (cc_*.frag).
//
//  O contrato das transições do app vale aqui também: Conclusão 0% = a camada
//  EXATA (is_identity tira o efeito da cadeia), 100% = vazia. O Scale Wipe é
//  a exceção de propósito: ele estica, não apaga (100% = só os riscos).
//  Coordenadas no plano da camada: a prévia em 1/4 corta no mesmo lugar do
//  export.
// =============================================================================
#include "CCCommon.hpp"

#include <algorithm>
#include <cmath>

namespace aurea::builtin {
namespace {
using namespace cc;

Status same_region(EffectBuildContext& ctx, const EffectEval& e, ShaderId frag, const LayerImage& in, Uniforms& u,
                   const char* name, LayerImage& out) {
    return run_pass(ctx, e, frag, in, in.region, u, name, out);
}

/// Distância do ponto ao canto mais longe da caixa (o raio que cobre a camada).
f32 farthest_corner(const Rect& box, Vec2 c) noexcept {
    f32 best = 1.0f;
    for (const f32 x : {box.x, box.x + box.w}) {
        for (const f32 y : {box.y, box.y + box.h}) {
            best = std::max(best, std::sqrt((x - c.x) * (x - c.x) + (y - c.y) * (y - c.y)));
        }
    }
    return best;
}

f32 completion(const EffectEval& e, u32 i) noexcept { return clampf(e.f(i), 0.0f, 100.0f, 50.0f) / 100.0f; }

// =============================================================================
// Jaws (CC Jaws)
// =============================================================================
class Jaws final : public Effect {
public:
    enum : u32 { kCompletion = 0, kCenter, kDirection, kHeight, kWidth, kShape };
    const EffectInfo& info() const noexcept override {
        static const EffectInfo i{effect_keys::kJaws, "Jaws", "Transição", EffectClass::Domain};
        return i;
    }
    void declare_parameters(ParameterRegistry& p) const override {
        static const char* const shapes[] = {"Dentes", "Robô", "Blocos", "Ondas"};
        p.add_float("completion", "Conclusão", 50.0f, 0.0f, 100.0f, kAnimPct, "%");
        p.add_point2("center", "Centro", Vec2{0.5f, 0.5f}, -1.0f, 2.0f, kAnimRel);
        p.typed_range(-10.0f, 10.0f);
        p.add_angle("direction", "Direção", 0.0f);
        // % do menor lado da camada.
        p.add_float("height", "Altura", 12.0f, 0.0f, 100.0f, kAnimPct, "%");
        p.typed_range(0.0f, 1000.0f);
        p.add_float("width", "Largura", 10.0f, 0.5f, 100.0f, kAnimPct, "%");
        p.typed_range(0.2f, 1000.0f);
        p.add_enum("shape", "Forma", shapes, 4, 0);
    }
    bool is_identity(const EffectEval& e) const noexcept override { return !(finite_or(e.f(kCompletion), 50.0f) > 0.0f); }
    bool needs_full_input() const noexcept override { return true; }
    void pipelines(std::vector<PipelineKey>& out, SurfaceFormat work) const override {
        out.push_back(PipelineKey::fullscreen(ShaderId::effects_cc_jaws_frag, work));
    }
    Status build(EffectBuildContext& ctx, const EffectEval& e, const LayerImage& in, f32, LayerImage& out) const override {
        const Rect box = layer_box(e, in);
        const Vec2 c = layer_point(e, in, kCenter);
        const f32 side = min_side(box);
        const f32 height = clampf(e.f(kHeight), 0.0f, 1000.0f, 12.0f) / 100.0f * side;
        const f32 width = std::max(clampf(e.f(kWidth), 0.2f, 1000.0f, 10.0f) / 100.0f * side,
                                   2.0f / std::max(in.texel_scale_x(), 1e-3f));
        const u32 shape = std::min(e.e(kShape), 3u);
        // A borda inclinada (serra, onda) mede o antisserrilhado ao longo da normal.
        const f32 slope = shape == 0u ? 2.0f * height / width : (shape == 3u ? kPi * height / width : 0.0f);
        // Cada metade anda até a borda mais longe da camada (medida na normal
        // do corte) mais meio dente: some exatamente em 100%.
        const f32 angle = finite_or(e.f(kDirection), 0.0f) * kDeg;
        const Vec2 nrm{-std::sin(angle), std::cos(angle)};
        f32 reach = 0.0f;
        for (const f32 x : {box.x, box.x + box.w})
            for (const f32 y : {box.y, box.y + box.h}) reach = std::max(reach, std::fabs((x - c.x) * nrm.x + (y - c.y) * nrm.y));
        Uniforms u{};
        u.p0 = Vec4{c.x, c.y, angle, completion(e, kCompletion)};
        u.p1 = Vec4{height, width, static_cast<f32>(shape), reach + 0.5f * height + 1.0f};
        u.p2 = Vec4{std::sqrt(1.0f + slope * slope), 0.0f, 0.0f, 0.0f};
        return same_region(ctx, e, ShaderId::effects_cc_jaws_frag, in, u, "mandibulas", out);
    }
};

// =============================================================================
// Scale Wipe (CC Scale Wipe) e Radial Scale Wipe (CC Radial ScaleWipe)
// =============================================================================
class ScaleWipe final : public Effect {
public:
    enum : u32 { kStretch = 0, kCenter, kDirection };
    const EffectInfo& info() const noexcept override {
        static const EffectInfo i{effect_keys::kScaleWipe, "Scale Wipe", "Transição", EffectClass::Domain};
        return i;
    }
    void declare_parameters(ParameterRegistry& p) const override {
        p.add_float("stretch", "Esticar", 50.0f, 0.0f, 100.0f, kAnimPct, "%");
        p.add_point2("center", "Centro", Vec2{0.5f, 0.5f}, -1.0f, 2.0f, kAnimRel);
        p.typed_range(-10.0f, 10.0f);
        p.add_angle("direction", "Direção", 0.0f);
    }
    bool is_identity(const EffectEval& e) const noexcept override { return !(finite_or(e.f(kStretch), 50.0f) > 0.0f); }
    bool needs_full_input() const noexcept override { return true; }
    void pipelines(std::vector<PipelineKey>& out, SurfaceFormat work) const override {
        out.push_back(PipelineKey::fullscreen(ShaderId::effects_cc_scale_wipe_frag, work));
    }
    Status build(EffectBuildContext& ctx, const EffectEval& e, const LayerImage& in, f32, LayerImage& out) const override {
        const Vec2 c = layer_point(e, in, kCenter);
        Uniforms u{};
        u.p0 = Vec4{c.x, c.y, finite_or(e.f(kDirection), 0.0f) * kDeg, completion(e, kStretch)};
        return same_region(ctx, e, ShaderId::effects_cc_scale_wipe_frag, in, u, "esticar-varredura", out);
    }
};

class RadialScaleWipe final : public Effect {
public:
    enum : u32 { kCompletion = 0, kCenter, kReverse };
    const EffectInfo& info() const noexcept override {
        static const EffectInfo i{effect_keys::kRadialScaleWipe, "Radial Scale Wipe", "Transição", EffectClass::Domain};
        return i;
    }
    void declare_parameters(ParameterRegistry& p) const override {
        p.add_float("completion", "Conclusão", 50.0f, 0.0f, 100.0f, kAnimPct, "%");
        p.add_point2("center", "Centro", Vec2{0.5f, 0.5f}, -1.0f, 2.0f, kAnimRel);
        p.typed_range(-10.0f, 10.0f);
        p.add_bool("reverse", "Inverter direção", false);
    }
    bool is_identity(const EffectEval& e) const noexcept override { return !(finite_or(e.f(kCompletion), 50.0f) > 0.0f); }
    bool needs_full_input() const noexcept override { return true; }
    void pipelines(std::vector<PipelineKey>& out, SurfaceFormat work) const override {
        out.push_back(PipelineKey::fullscreen(ShaderId::effects_cc_radial_scale_wipe_frag, work));
    }
    Status build(EffectBuildContext& ctx, const EffectEval& e, const LayerImage& in, f32, LayerImage& out) const override {
        const Vec2 c = layer_point(e, in, kCenter);
        Uniforms u{};
        u.p0 = Vec4{c.x, c.y, completion(e, kCompletion), farthest_corner(layer_box(e, in), c) + 1.0f};
        u.p1 = Vec4{e.b(kReverse) ? 1.0f : 0.0f, 0.0f, 0.0f, 0.0f};
        return same_region(ctx, e, ShaderId::effects_cc_radial_scale_wipe_frag, in, u, "varredura-radial-escala", out);
    }
};

// =============================================================================
// Burn Film (CC Burn Film)
// =============================================================================
class BurnFilm final : public Effect {
public:
    enum : u32 { kBurn = 0, kCenter, kSeed };
    const EffectInfo& info() const noexcept override {
        static const EffectInfo i{effect_keys::kBurnFilm, "Burn Film", "Transição", EffectClass::Domain};
        return i;
    }
    void declare_parameters(ParameterRegistry& p) const override {
        p.add_float("burn", "Queimar", 50.0f, 0.0f, 100.0f, kAnimPct, "%");
        p.add_point2("center", "Centro", Vec2{0.5f, 0.5f}, -1.0f, 2.0f, kAnimRel);
        p.typed_range(-10.0f, 10.0f);
        p.add_int("seed", "Semente", 0, 0, 9999);
    }
    bool is_identity(const EffectEval& e) const noexcept override { return !(finite_or(e.f(kBurn), 50.0f) > 0.0f); }
    void pipelines(std::vector<PipelineKey>& out, SurfaceFormat work) const override {
        out.push_back(PipelineKey::fullscreen(ShaderId::effects_cc_burn_film_frag, work));
    }
    Status build(EffectBuildContext& ctx, const EffectEval& e, const LayerImage& in, f32, LayerImage& out) const override {
        const Rect box = layer_box(e, in);
        const Vec2 c = layer_point(e, in, kCenter);
        Uniforms u{};
        u.p0 = Vec4{c.x, c.y, completion(e, kBurn), static_cast<f32>(std::clamp(e.value(kSeed).as_int(), 0, 9999) % 997)};
        u.p1 = Vec4{farthest_corner(box, c), 0.12f * std::max(box.w, box.h), 0.0f, 0.0f};
        return same_region(ctx, e, ShaderId::effects_cc_burn_film_frag, in, u, "filme-queimando", out);
    }
};

// =============================================================================
// Block Load (CC Block Load)
// =============================================================================
class BlockLoad final : public Effect {
public:
    enum : u32 { kCompletion = 0, kScanlines, kEndCleared, kBilinear };
    const EffectInfo& info() const noexcept override {
        static const EffectInfo i{effect_keys::kBlockLoad, "Block Load", "Transição", EffectClass::Domain};
        return i;
    }
    void declare_parameters(ParameterRegistry& p) const override {
        p.add_float("completion", "Conclusão", 50.0f, 0.0f, 100.0f, kAnimPct, "%");
        p.add_bool("scanlines", "Linhas de varredura", true);
        p.add_bool("end_cleared", "Terminar vazio", true);
        p.add_bool("bilinear", "Bilinear", false);
    }
    bool is_identity(const EffectEval& e) const noexcept override { return !(finite_or(e.f(kCompletion), 50.0f) > 0.0f); }
    bool needs_full_input() const noexcept override { return true; }
    void pipelines(std::vector<PipelineKey>& out, SurfaceFormat work) const override {
        out.push_back(PipelineKey::fullscreen(ShaderId::effects_cc_block_load_frag, work));
    }
    Status build(EffectBuildContext& ctx, const EffectEval& e, const LayerImage& in, f32, LayerImage& out) const override {
        const Rect box = layer_box(e, in);
        // Bloco base de ~2 texels; as etapas dobram até ~1/8 do menor lado.
        const f32 base = std::max(2.0f / std::max(in.texel_scale_x(), 1e-3f), 1.0f);
        const f32 levels = std::clamp(std::floor(std::log2(std::max(min_side(box) / 8.0f / base, 1.0f))) + 1.0f, 1.0f, 12.0f);
        Uniforms u{};
        u.p0 = Vec4{completion(e, kCompletion), levels + (e.b(kEndCleared) ? 1.0f : 0.0f), base * 0.5f,
                    e.b(kScanlines) ? 1.0f : 0.0f};
        u.p1 = Vec4{e.b(kEndCleared) ? 1.0f : 0.0f, e.b(kBilinear) ? 1.0f : 0.0f, 0.0f, 0.0f};
        return same_region(ctx, e, ShaderId::effects_cc_block_load_frag, in, u, "carga-em-blocos", out);
    }
};

} // namespace

void register_cc_transitions(EffectRegistry& r) {
    (void)r.add(std::make_unique<Jaws>());
    (void)r.add(std::make_unique<ScaleWipe>());
    (void)r.add(std::make_unique<RadialScaleWipe>());
    (void)r.add(std::make_unique<BurnFilm>());
    (void)r.add(std::make_unique<BlockLoad>());
}

} // namespace aurea::builtin
