// =============================================================================
//  Desintegrar (Transição): a camada se desfaz em fragmentos quadrados.
//
//  Referência de comportamento: os efeitos de "desintegração" de editores de
//  movimento — uma frente varre a camada na direção escolhida e solta pedaços
//  pequenos dela, que voam, giram, encolhem e somem. Implementação própria:
//
//    * cada célula de `Tamanho da partícula` px é um fragmento; o instante de
//      soltura dele (0..1 da Conclusão) mistura a posição ao longo da direção
//      com um sorteio por célula (Aleatoriedade) — a mesma conta no vértice
//      (disintegrate.vert) e na composição (disintegrate_compose.frag);
//    * depois de solto, o fragmento vive uma fração fixa da conclusão (kLife):
//      Conclusão 100% = todos apagados, 0% = a camada intacta (identidade);
//    * os fragmentos saem da caixa da camada: a região de saída cresce pelo
//      alcance do voo (recortada ao quadro visível, como o brilho e os raios).
// =============================================================================
#include "BuiltinEffects.hpp"

#include <algorithm>
#include <cmath>

namespace aurea::builtin {
namespace {

constexpr const char* kDisintegrateKey = "aurea.transition.disintegrate";

f32 finite_or(f32 v, f32 fallback) noexcept { return std::isfinite(v) ? v : fallback; }

/// Fração da conclusão que um fragmento vive depois de solto.
constexpr f32 kLife = 0.4f;
/// Teto de fragmentos (6 vértices cada): o tamanho cresce antes de passar.
constexpr f32 kMaxCells = 262144.0f;

class Disintegrate final : public Effect {
public:
    enum : u32 { kCompletion = 0, kDirection, kParticleSize, kRandomness, kSpread, kSpeed, kTurbulence,
                 kGravity, kFade, kSeed, kGlowColor, kGlowIntensity, kMix };

    const EffectInfo& info() const noexcept override {
        static const EffectInfo i{kDisintegrateKey, "Desintegrar", "Transição", EffectClass::Domain};
        return i;
    }
    void declare_parameters(ParameterRegistry& p) const override {
        constexpr u16 kAnim = kParamAnimatable;
        constexpr u16 kAnimPct = kParamAnimatable | kParamPercent;
        p.add_float("completion", "Conclusão", 0.0f, 0.0f, 100.0f, kAnimPct, "%");
        p.add_angle("direction", "Direção", 0.0f);
        p.add_float("particle_size", "Tamanho da partícula", 6.0f, 1.0f, 64.0f, kAnim | kParamPixels, "px");
        p.typed_range(1.0f, 400.0f);
        p.add_float("randomness", "Aleatoriedade", 40.0f, 0.0f, 100.0f, kAnimPct, "%");
        p.add_angle("spread", "Espalhamento", 35.0f, 0.0f, 180.0f);
        p.add_float("speed", "Velocidade", 260.0f, 0.0f, 2000.0f, kAnim | kParamPixels, "px");
        p.typed_range(0.0f, 20000.0f);
        p.add_float("turbulence", "Turbulência", 40.0f, 0.0f, 500.0f, kAnim | kParamPixels, "px");
        p.typed_range(0.0f, 5000.0f);
        p.add_float("gravity", "Gravidade", 0.0f, -1000.0f, 1000.0f, kAnim | kParamPixels, "px");
        p.typed_range(-20000.0f, 20000.0f);
        p.add_float("fade", "Esmaecer", 70.0f, 0.0f, 100.0f, kAnimPct, "%");
        p.add_int("seed", "Semente", 0, 0, 9999, kAnim);
        p.add_color("glow_color", "Cor do brilho", Vec4{1.0f, 0.55f, 0.15f, 1.0f});
        p.add_float("glow_intensity", "Brilho da borda", 0.0f, 0.0f, 400.0f, kAnimPct, "%");
        p.add_float("mix", "Mistura", 100.0f, 0.0f, 100.0f, kAnimPct, "%");
    }
    bool is_identity(const EffectEval& e) const noexcept override {
        return !(finite_or(e.f(kCompletion), 0.0f) > 0.0f) || !(finite_or(e.f(kMix), 100.0f) > 0.0f);
    }
    // Fragmentos de fora do quadro visível podem voar para dentro dele.
    bool needs_full_input() const noexcept override { return true; }
    void pipelines(std::vector<PipelineKey>& out, SurfaceFormat work) const override {
        out.push_back(PipelineKey::graphics(ShaderId::effects_disintegrate_vert, ShaderId::effects_disintegrate_frag,
                                            work, true, BlendMode::Normal));
        out.push_back(PipelineKey::fullscreen(ShaderId::effects_disintegrate_compose_frag, work));
    }
    bool demo_values(EffectInstance&, std::vector<ParamValue>& v) const noexcept override {
        v[kCompletion] = ParamValue::scalar(45.0f);
        v[kParticleSize] = ParamValue::scalar(4.0f);
        v[kSpeed] = ParamValue::scalar(70.0f);
        v[kTurbulence] = ParamValue::scalar(12.0f);
        v[kGlowIntensity] = ParamValue::scalar(60.0f);
        return true;
    }

    Status build(EffectBuildContext& ctx, const EffectEval& e, const LayerImage& input, f32 margin,
                 LayerImage& out) const override {
        struct Uniforms {
            Vec4 src, dst, grid, front, timing, motion, glow, texel, uvMap, mode;
        } u{};
        static_assert(sizeof(Uniforms) == 160);
        const Rect src = input.region;
        if (!(src.w > 0.0f) || !(src.h > 0.0f)) { out = input; return OkStatus; }

        f32 side = std::clamp(finite_or(e.f(kParticleSize), 6.0f), 1.0f, 400.0f);
        while (std::ceil(src.w / side) * std::ceil(src.h / side) > kMaxCells) side *= 1.25f;
        const f32 cols = std::max(1.0f, std::ceil(src.w / side));
        const f32 rows = std::max(1.0f, std::ceil(src.h / side));

        constexpr f32 kDeg = 0.01745329252f;
        const f32 angle = finite_or(e.f(kDirection), 0.0f) * kDeg;
        const Vec2 dir{std::cos(angle), std::sin(angle)};
        // A varredura vai da projeção mínima à máxima dos cantos da camada.
        f32 lo = 1e30f, hi = -1e30f;
        for (const Vec2 c : {Vec2{src.x, src.y}, Vec2{src.x + src.w, src.y}, Vec2{src.x, src.y + src.h},
                             Vec2{src.x + src.w, src.y + src.h}}) {
            const f32 d = c.x * dir.x + c.y * dir.y;
            lo = std::min(lo, d); hi = std::max(hi, d);
        }
        const f32 completion = std::clamp(finite_or(e.f(kCompletion), 0.0f) / 100.0f, 0.0f, 1.0f);
        const f32 speed = std::clamp(finite_or(e.f(kSpeed), 260.0f), 0.0f, 20000.0f);
        const f32 turbulence = std::clamp(finite_or(e.f(kTurbulence), 40.0f), 0.0f, 5000.0f);
        const f32 gravity = std::clamp(finite_or(e.f(kGravity), 0.0f), -20000.0f, 20000.0f);
        const f32 spread = std::clamp(finite_or(e.f(kSpread), 35.0f), 0.0f, 180.0f) * kDeg;

        // Alcance do voo (px): velocidade × 1,45 (o maior sorteio), a gravidade,
        // o fluxo (1,5 × 0,6 × turbulência) e o fragmento girado.
        const f32 reach = speed * 1.45f + std::fabs(gravity) + turbulence * 0.9f + side + 2.0f;
        const bool clipped = e.placement && !e.placement->inScene3d && !e.placement->preserveFullExtent
                          && e.placement->compWidth && e.placement->compHeight;
        // Na cena 3D (sem recorte) o crescimento tem teto, como nos raios.
        const f32 grow = clipped ? reach : std::min(reach, std::max(0.25f * std::max(src.w, src.h), 512.0f));
        const Rect region = spread_region(src, grow, grow, e.placement, margin);
        u32 w = 0, h = 0;
        ctx.region_size(region, input.texel_scale_x(), w, h);
        if (!w || !h) { out = input; return OkStatus; }

        const Vec4 glow = e.color(kGlowColor);
        u.src = Vec4{src.x, src.y, src.w, src.h};
        u.dst = Vec4{region.x, region.y, region.w, region.h};
        u.grid = Vec4{cols, rows, side, static_cast<f32>(std::clamp(e.value(kSeed).as_int(), 0, 9999))};
        u.front = Vec4{dir.x, dir.y, lo, 1.0f / std::max(hi - lo, 1e-3f)};
        u.timing = Vec4{completion, std::clamp(finite_or(e.f(kRandomness), 40.0f) / 100.0f, 0.0f, 1.0f), kLife,
                        std::clamp(finite_or(e.f(kFade), 70.0f) / 100.0f, 0.0f, 1.0f)};
        u.motion = Vec4{speed, spread, turbulence, gravity};
        u.glow = Vec4{Color::srgb_to_linear(std::clamp(glow.x, 0.0f, 1.0f)),
                      Color::srgb_to_linear(std::clamp(glow.y, 0.0f, 1.0f)),
                      Color::srgb_to_linear(std::clamp(glow.z, 0.0f, 1.0f)),
                      std::clamp(finite_or(e.f(kGlowIntensity), 0.0f) / 100.0f, 0.0f, 4.0f)};
        u.texel = Vec4{1.0f / std::max(input.texel_scale_x(), 1e-4f), 0.0f, 0.0f, 0.0f};
        u.uvMap = EffectBuildContext::uv_map(region, src);
        u.mode = Vec4{std::clamp(finite_or(e.f(kMix), 100.0f) / 100.0f, 0.0f, 1.0f), 1.0f, 0.0f, 0.0f};

        const FGTexture parts = ctx.texture("desintegrar-fragmentos", w, h);
        const u32 vertices = static_cast<u32>(cols * rows) * 6u;
        if (ctx.geometry_pass("desintegrar-fragmentos", PassStage::Effects, parts, ShaderId::effects_disintegrate_vert,
                              ShaderId::effects_disintegrate_frag,
                              {PassTexture{input.texture, {}, CommonSampler::LinearBorder}}, &u, sizeof(u), vertices,
                              false, true, BlendMode::Normal) == kInvalidIndex) {
            return Errc::PipelineCompileFailed;
        }
        out = LayerImage{ctx.texture("desintegrar", w, h), region, w, h};
        if (ctx.fullscreen_pass("desintegrar", PassStage::Effects, out.texture, ShaderId::effects_disintegrate_compose_frag,
                                {PassTexture{parts, {}, CommonSampler::LinearClamp},
                                 PassTexture{input.texture, {}, CommonSampler::LinearBorder}},
                                &u, sizeof(u)) == kInvalidIndex) {
            return Errc::PipelineCompileFailed;
        }
        return OkStatus;
    }
};

} // namespace

void register_disintegrate_effect(EffectRegistry& r) {
    (void)r.add(std::make_unique<Disintegrate>());
}

} // namespace aurea::builtin
