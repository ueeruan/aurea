// =============================================================================
//  Explosão (aurea.3d.explode) — pedido do dono, 23/11/2026.
//
//  Num objeto 3D (modelo importado, forma 3D, texto 3D) o efeito não desenha
//  nada no 2D: o renderer 3D troca o modelo pelas peças da fratura e as move
//  (scene3d/Explode3D.cpp, chamado por place_model) — luz, sombra, materiais e
//  desfoque de movimento continuam os da cena.
//
//  Em qualquer outra camada (e na prévia do catálogo) a camada estilhaça em
//  peças planas com espessura, pelos shaders do 3D Shatter (effects/
//  shatter3d.*): os mesmos parâmetros viram a força/giro/gravidade dele.
//
//  Os índices dos parâmetros ficam em Explode3D.hpp (fixos nos projetos).
// =============================================================================
#include "BuiltinEffects.hpp"

#include "aurea/effects/Shatter3D.hpp"
#include "aurea/scene3d/Explode3D.hpp"
#include "aurea/timeline/Layer.hpp"

#include <algorithm>
#include <cmath>

namespace aurea::builtin {
namespace {

namespace ex = scene3d::explode3d;

constexpr f32 kDeg = 0.01745329252f;

f32 finite_or(f32 v, f32 fallback) noexcept { return std::isfinite(v) ? v : fallback; }

/// Progresso efetivo (Tempo automático e Montar), igual ao do renderer 3D.
f32 effective_progress(const EffectEval& e) noexcept {
    f32 p = 0.0f;
    if (e.e(ex::kTiming) == ex::kTimingAuto) {
        const f64 fps = e.framesPerSecond > 0.0 && std::isfinite(e.framesPerSecond) ? e.framesPerSecond : 30.0;
        const f64 seconds = e.time_frames() / fps;
        const f64 duration = std::max(0.01, static_cast<f64>(finite_or(e.f(ex::kDuration), 2.0f)));
        p = static_cast<f32>(std::clamp((seconds - finite_or(e.f(ex::kStart), 0.0f)) / duration, 0.0, 1.0));
    } else {
        p = std::clamp(finite_or(e.f(ex::kProgress), 0.0f) / 100.0f, 0.0f, 1.0f);
    }
    return e.b(ex::kReassemble) ? 1.0f - p : p;
}

Vec2 layer_extent(const EffectEval& e, const LayerImage& input) noexcept {
    if (e.placement && e.placement->layerWidth && e.placement->layerHeight)
        return Vec2{static_cast<f32>(e.placement->layerWidth), static_cast<f32>(e.placement->layerHeight)};
    return Vec2{input.region.w, input.region.h};
}

class Explode3D final : public Effect {
public:
    const EffectInfo& info() const noexcept override {
        static const EffectInfo i{effect_keys::kExplode3D, "Explosão", "3D", EffectClass::Domain};
        return i;
    }
    void declare_parameters(ParameterRegistry& p) const override { ex::declare_parameters(p); }

    bool is_identity(const EffectEval& e) const noexcept override {
        // Objeto 3D: quem desenha é a cena (o modelo fica no grupo 3D comum).
        if (e.layer && e.layer->kind == LayerKind::Model3D) return true;
        return !(effective_progress(e) > 0.0f);
    }
    bool needs_full_input() const noexcept override { return true; }
    void pipelines(std::vector<PipelineKey>& out, SurfaceFormat work) const override {
        PipelineKey k = PipelineKey::graphics(ShaderId::effects_shatter3d_vert, ShaderId::effects_shatter3d_frag, work);
        k.hasDepth = true;
        k.depthTest = true;
        k.depthWrite = true;
        k.depthCompare = CompareOp::GreaterOrEqual;
        k.depthFormat = SurfaceFormat::Depth32F;
        out.push_back(k);
    }
    bool demo_values(EffectInstance&, std::vector<ParamValue>& v) const noexcept override {
        v[ex::kProgress] = ParamValue::scalar(55.0f);
        v[ex::kPieces] = ParamValue::scalar(60.0f);
        return true;
    }

    std::span<const EffectPreset> presets() const noexcept override {
        using V = EffectPresetValue;
        static constexpr V kExplode[] = {
            {ex::kPieces, 120}, {ex::kThickness, 60}, {ex::kForce, 100}, {ex::kSpread, 75}, {ex::kVelocityRandom, 40},
            {ex::kSpin, 360}, {ex::kAxisRandom, 100}, {ex::kGravity, 40}, {ex::kDrag, 20}, {ex::kWave, 35},
            {ex::kEndScale, 100}, {ex::kFade, 0}, {ex::kReassemble, 0}, {ex::kGlow, 60}, {ex::kInteriorDark, 60},
            {ex::kImpact, 0, 0}, {ex::kImpact, 0, 1}, {ex::kImpact, 0, 2},
            {ex::kDirection, 0, 0}, {ex::kDirection, 0, 1}, {ex::kDirection, -1, 2}};
        static constexpr V kGlass[] = {
            {ex::kPieces, 300}, {ex::kThickness, 12}, {ex::kForce, 70}, {ex::kSpread, 90}, {ex::kVelocityRandom, 50},
            {ex::kSpin, 720}, {ex::kAxisRandom, 100}, {ex::kGravity, 90}, {ex::kDrag, 10}, {ex::kWave, 55},
            {ex::kEndScale, 100}, {ex::kFade, 0}, {ex::kReassemble, 0}, {ex::kGlow, 0}, {ex::kInteriorDark, 20},
            {ex::kImpact, 0, 0}, {ex::kImpact, 0, 1}, {ex::kImpact, -100, 2},
            {ex::kDirection, 0, 0}, {ex::kDirection, 0, 1}, {ex::kDirection, -1, 2}};
        static constexpr V kDisintegrate[] = {
            {ex::kPieces, 1200}, {ex::kThickness, 40}, {ex::kForce, 35}, {ex::kSpread, 100}, {ex::kVelocityRandom, 85},
            {ex::kSpin, 200}, {ex::kAxisRandom, 100}, {ex::kGravity, -15}, {ex::kDrag, 70}, {ex::kWave, 80},
            {ex::kEndScale, 0}, {ex::kFade, 100}, {ex::kReassemble, 0}, {ex::kGlow, 140}, {ex::kInteriorDark, 70},
            {ex::kImpact, -100, 0}, {ex::kImpact, 0, 1}, {ex::kImpact, 0, 2},
            {ex::kDirection, 1, 0}, {ex::kDirection, 0, 1}, {ex::kDirection, 0, 2}};
        static constexpr V kAssemble[] = {
            {ex::kPieces, 150}, {ex::kThickness, 60}, {ex::kForce, 100}, {ex::kSpread, 80}, {ex::kVelocityRandom, 40},
            {ex::kSpin, 360}, {ex::kAxisRandom, 100}, {ex::kGravity, 0}, {ex::kDrag, 20}, {ex::kWave, 50},
            {ex::kEndScale, 100}, {ex::kFade, 0}, {ex::kReassemble, 1}, {ex::kGlow, 30}, {ex::kInteriorDark, 60},
            {ex::kImpact, 0, 0}, {ex::kImpact, 0, 1}, {ex::kImpact, 0, 2},
            {ex::kDirection, 0, 0}, {ex::kDirection, 0, 1}, {ex::kDirection, -1, 2}};
        static constexpr V kSide[] = {
            {ex::kPieces, 160}, {ex::kThickness, 60}, {ex::kForce, 140}, {ex::kSpread, 25}, {ex::kVelocityRandom, 35},
            {ex::kSpin, 540}, {ex::kAxisRandom, 100}, {ex::kGravity, 60}, {ex::kDrag, 15}, {ex::kWave, 70},
            {ex::kEndScale, 100}, {ex::kFade, 0}, {ex::kReassemble, 0}, {ex::kGlow, 80}, {ex::kInteriorDark, 60},
            {ex::kImpact, -100, 0}, {ex::kImpact, 0, 1}, {ex::kImpact, 0, 2},
            {ex::kDirection, 1, 0}, {ex::kDirection, 0, 1}, {ex::kDirection, 0, 2}};
        static const EffectPreset kAll[] = {
            {"explode", "Explosão", kExplode}, {"glass", "Estilhaçar vidro", kGlass},
            {"disintegrate", "Desintegrar", kDisintegrate}, {"assemble", "Montar", kAssemble},
            {"side_impact", "Impacto lateral", kSide}};
        return kAll;
    }

    /// Camada 2D (e prévia do catálogo): o bloco do 3D Shatter com os
    /// parâmetros da Explosão. Devolve o número de peças; 0 = intacta.
    static u32 fill_uniforms(const EffectEval& e, const LayerImage& input, shatter3d::Uniforms& u) noexcept {
        u = {};
        const Rect src = input.region;
        if (!(src.w > 0.0f) || !(src.h > 0.0f)) return 0;
        const f32 progress = effective_progress(e);
        if (!(progress > 0.0f)) return 0;
        const Vec2 size = layer_extent(e, input);
        const f32 big = std::max(size.x, size.y);
        const f32 pieces = static_cast<f32>(std::clamp(e.value(ex::kPieces).as_int(), 8, 2000));
        const f32 cols = std::clamp(std::round(std::sqrt(pieces * src.w / src.h)), 2.0f, 48.0f);
        const f32 rows = std::clamp(std::round(cols * src.h / src.w), 1.0f, 96.0f);
        const Vec3 impact = e.value(ex::kImpact).as_vec3();
        const Vec3 dir = e.value(ex::kDirection).as_vec3();
        const Vec2 center{(0.5f + std::clamp(finite_or(impact.x, 0.0f), -200.0f, 200.0f) / 200.0f) * size.x,
                          (0.5f + std::clamp(finite_or(impact.y, 0.0f), -200.0f, 200.0f) / 200.0f) * size.y};
        f32 far = 1.0f;
        for (const Vec2 c : {Vec2{src.x, src.y}, Vec2{src.x + src.w, src.y}, Vec2{src.x, src.y + src.h},
                             Vec2{src.x + src.w, src.y + src.h}})
            far = std::max(far, std::hypot(c.x - center.x, c.y - center.y));
        const f32 spread = std::clamp(finite_or(e.f(ex::kSpread), 75.0f) / 100.0f, 0.0f, 1.0f);
        const f32 force = std::clamp(finite_or(e.f(ex::kForce), 100.0f) / 100.0f, 0.0f, 20.0f) * 0.9f * big;
        const f32 gravity = std::clamp(finite_or(e.f(ex::kGravity), 40.0f) / 100.0f, -10.0f, 10.0f) * 1.5f * big;
        const f32 thickness = std::clamp(finite_or(e.f(ex::kThickness), 60.0f) / 100.0f, 0.0f, 1.0f) * 0.05f * big;
        const f32 toward = std::clamp(-finite_or(dir.z, -1.0f) * (1.0f - spread) + 0.35f * spread, -1.0f, 1.0f);
        u.src = Vec4{src.x, src.y, src.w, src.h};
        u.grid = Vec4{cols, rows, 2.0f, static_cast<f32>(std::clamp(e.value(ex::kSeed).as_int(), 0, 9999))};
        u.motion = Vec4{progress, force, std::clamp(finite_or(e.f(ex::kSpin), 360.0f), 0.0f, 3600.0f) * kDeg, gravity};
        u.shape = Vec4{thickness, toward, std::clamp(finite_or(e.f(ex::kVelocityRandom), 40.0f) / 100.0f, 0.0f, 1.0f),
                       std::clamp(finite_or(e.f(ex::kWave), 35.0f) / 100.0f, 0.0f, 1.0f)};
        u.view = Vec4{center.x, center.y, 0.0f, 0.6f};
        u.misc = Vec4{std::clamp(finite_or(e.f(ex::kFade), 0.0f) / 100.0f, 0.0f, 1.0f), src.w / cols, src.h / rows, far};
        return static_cast<u32>(cols * rows) * 2u;
    }

    Status build(EffectBuildContext& ctx, const EffectEval& e, const LayerImage& input, f32 margin,
                 LayerImage& out) const override {
        shatter3d::Uniforms u{};
        const u32 pieces = fill_uniforms(e, input, u);
        if (!pieces) { out = input; return OkStatus; }
        const Rect src = input.region;
        const f32 cols = u.grid.x, rows = u.grid.y;
        const f32 force = u.motion.y, toward = u.shape.y, gravity = u.motion.w, thickness = u.shape.x;
        const f32 camera = 4.75f * std::max(src.w, src.h);   // perspectiva média (a do 3D Shatter em 50%)
        const f32 cell = std::max(src.w / cols, src.h / rows);
        const f32 reach = (force * 1.7f + std::fabs(gravity) + thickness + cell) * (1.0f + std::max(toward, 0.0f) * 2.0f);
        const bool clipped = e.placement && !e.placement->inScene3d && !e.placement->preserveFullExtent
                          && e.placement->compWidth && e.placement->compHeight;
        const f32 grow = clipped ? reach : std::min(reach, std::max(0.25f * std::max(src.w, src.h), 512.0f));
        const Rect region = spread_region(src, grow, grow, e.placement, margin);
        u32 w = 0, h = 0;
        ctx.region_size(region, input.texel_scale_x(), w, h);
        if (!w || !h) { out = input; return OkStatus; }
        u.dst = Vec4{region.x, region.y, region.w, region.h};
        u.view.z = camera;
        out = LayerImage{ctx.texture("explosao-2d", w, h), region, w, h};
        if (ctx.geometry_pass("explosao-2d", PassStage::Effects, out.texture, ShaderId::effects_shatter3d_vert,
                              ShaderId::effects_shatter3d_frag,
                              {PassTexture{input.texture, {}, CommonSampler::LinearBorder}}, &u, sizeof(u),
                              pieces * 36u, true) == kInvalidIndex) {
            return Errc::PipelineCompileFailed;
        }
        return OkStatus;
    }
};

} // namespace

void register_explode3d_effect(EffectRegistry& r) { (void)r.add(std::make_unique<Explode3D>()); }

} // namespace aurea::builtin
