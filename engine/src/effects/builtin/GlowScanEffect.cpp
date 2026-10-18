// Glow Scan (Luz): uma faixa de luz atravessa a camada, acende o conteúdo
// (ou só o contorno) e solta um brilho para fora das bordas. Posição por
// keyframe ou varredura automática (duração + intervalo), determinística:
// o mesmo quadro dá a mesma faixa na prévia e no export.
#include "BuiltinEffects.hpp"

#include <algorithm>
#include <cmath>

namespace aurea::builtin {
namespace {
class GlowScan final : public Effect {
public:
    enum : u32 { kProgress = 0, kAuto, kDuration, kPause, kAngle, kWidth, kSoftness, kColor, kIntensity,
                 kGlowRadius, kGlow, kEdges };
    const EffectInfo& info() const noexcept override {
        static const EffectInfo i{"aurea.light.glow_scan", "Glow Scan", "Luz", EffectClass::Neighborhood};
        return i;
    }
    void declare_parameters(ParameterRegistry& p) const override {
        p.add_float("progress", "Posição da varredura", 50, 0, 100, kParamAnimatable | kParamPercent, "%");
        p.add_bool("auto", "Varrer sozinho", true);
        p.add_float("duration", "Duração da passada", 1.6f, 0.1f, 30, kParamAnimatable, "s");
        p.add_float("pause", "Intervalo", 0.8f, 0, 30, kParamAnimatable, "s");
        p.add_angle("angle", "Ângulo", 30);
        p.add_float("width", "Largura", 140, 1, 4000, kParamAnimatable | kParamPixels, "px");
        p.add_float("softness", "Suavidade", 70, 0, 100, kParamAnimatable | kParamPercent, "%");
        p.add_color("color", "Cor do brilho", Vec4{.55f, .85f, 1, 1});
        p.add_float("intensity", "Intensidade", 150, 0, 1000, kParamAnimatable | kParamPercent, "%");
        p.add_float("glow_radius", "Raio do brilho", 24, 0, 300, kParamAnimatable | kParamPixels, "px");
        p.add_float("glow", "Brilho em volta", 100, 0, 1000, kParamAnimatable | kParamPercent, "%");
        p.add_bool("edges", "Só nas bordas", false);
    }
    bool demo_values(EffectInstance&, std::vector<ParamValue>& v) const noexcept override {
        v[kAuto] = ParamValue::boolean(false);   // o catálogo mostra a faixa no meio
        v[kProgress] = ParamValue::scalar(45);
        return true;
    }
    bool is_identity(const EffectEval& e) const noexcept override {
        return e.f(kWidth) <= 0 || (e.f(kIntensity) <= 0 && e.f(kGlow) <= 0);
    }
    f32 input_margin(const EffectEval& e) const noexcept override { return std::max(0.f, e.f(kGlowRadius)) + 2; }
    void pipelines(std::vector<PipelineKey>& out, SurfaceFormat work) const override {
        out.push_back(PipelineKey::fullscreen(ShaderId::effects_glow_scan_frag, work));
    }
    Status build(EffectBuildContext& ctx, const EffectEval& e, const LayerImage& input, f32 margin, LayerImage& out) const override {
        const f32 w = e.placement && e.placement->layerWidth ? static_cast<f32>(e.placement->layerWidth) : input.region.w;
        const f32 h = e.placement && e.placement->layerHeight ? static_cast<f32>(e.placement->layerHeight) : input.region.h;
        const f32 angle = e.f(kAngle) * kDeg2Rad;
        const Vec2 axis{std::cos(angle), std::sin(angle)};
        const f32 halfWidth = std::max(e.f(kWidth), 1.f) * .5f;
        // A faixa entra e sai inteira: do canto mais longe de um lado ao do outro.
        const f32 range = (std::abs(w * axis.x) + std::abs(h * axis.y)) * .5f + halfWidth;
        f32 t = std::clamp(e.f(kProgress) * .01f, 0.f, 1.f);
        if (e.b(kAuto)) {
            const f64 fps = e.framesPerSecond > 0 ? e.framesPerSecond : 30.0;
            const f64 seconds = e.time_frames() / fps;
            const f64 duration = std::max<f64>(e.f(kDuration), .1), cycle = duration + std::max<f64>(e.f(kPause), 0);
            const f64 local = seconds - std::floor(seconds / cycle) * cycle;
            t = local < duration ? static_cast<f32>(local / duration) : 1.f;
        }
        EffectUniforms u = base_uniforms(input);
        u.p0 = {w * .5f, h * .5f, axis.x, axis.y};
        u.p1 = {-range + 2 * range * t, halfWidth, std::clamp(e.f(kSoftness) * .01f, 0.f, 1.f), std::max(e.f(kIntensity), 0.f) * .01f};
        u.p3 = {std::max(e.f(kGlowRadius), 0.f), e.b(kEdges) ? 1.f : 0.f, std::max(e.f(kGlow), 0.f) * .01f, 0};
        u.color = e.color(kColor);
        // O brilho sai da caixa da camada (texto: a caixa é justa nos glifos).
        const f32 extent = input_margin(e);
        const Rect region = spread_region(input.region, extent, extent, e.placement, margin);
        u32 ow = 0, oh = 0;
        ctx.region_size(region, input.texel_scale_x(), ow, oh);
        if (!ow || !oh) { out = input; return OkStatus; }
        u.uvMap = EffectBuildContext::uv_map(region, input.region);
        u.p2 = {region.x, region.y, region.w, region.h};
        out = LayerImage{ctx.texture("glow-scan", ow, oh), region, ow, oh};
        if (ctx.fullscreen_pass("glow-scan", PassStage::Effects, out.texture, ShaderId::effects_glow_scan_frag,
                {PassTexture{input.texture, {}, CommonSampler::LinearBorder}}, &u, sizeof(u)) == kInvalidIndex)
            return Errc::PipelineCompileFailed;
        return OkStatus;
    }
};
} // namespace

void register_glow_scan_effect(EffectRegistry& r) { (void)r.add(std::make_unique<GlowScan>()); }
} // namespace aurea::builtin
