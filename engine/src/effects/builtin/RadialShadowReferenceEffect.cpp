// Sombra radial (referência do AE): reconstrução independente do
// comportamento observado no Radial Shadow do After Effects
// (ADBE Radial Shadow). Evidência em
// build/reference/effects-20261115/batch-a/radial-shadow.md.
//
//  - cada pixel P da saída lê a camada em S = P + d·(L − P) (d = distância/100,
//    L = origem da luz), ou seja a sombra é a camada ampliada 1/(1 − d) em
//    volta da luz;
//  - o efeito nativo trabalha em 8 bits: o alfa amostrado é arredondado para
//    1/255 e o da sombra é trunc(alfa · opacidade);
//  - Normal: a sombra tem a cor do controle. Borda de vidro: onde o alfa é
//    parcial, t = (255 − alfa) · influência / 127,5 mistura a cor da camada
//    com peso 1 − (1 − t)²; com influência 0 o modo volta a Normal;
//  - Suavidade: a camada é desfocada (alfa no Normal, tudo no vidro) com
//    raio suavidade/2 antes da projeção;
//  - sem "Somente sombra", a camada fica na frente da sombra;
//  - "Redimensionar camada" aumenta a saída até a sombra projetada.
#include "BuiltinEffects.hpp"
#include "aurea/effects/FastBlurReference.hpp"

#include <algorithm>
#include <cmath>
#include <memory>

namespace aurea::builtin {
namespace {

constexpr const char* kRadialShadowReference = "aurea.stylize.radial_shadow_reference";

f64 finite_value(f32 value, f64 fallback) noexcept {
    return std::isfinite(value) ? static_cast<f64>(value) : fallback;
}

struct RadialShadowUniforms {
    Vec4 light{};        ///< L.x, L.y (px da camada), d, opacidade 0..1
    Vec4 mode{};         ///< vidro (0/1), influência (1 = 100%), só a sombra, 0
    Vec4 color{};        ///< cor da sombra (linear, sem alfa pré-multiplicado)
    Vec4 outRegion{};    ///< região da saída (px da camada)
    Vec4 sourceRegion{}; ///< região da camada original
    Vec4 shadowRegion{}; ///< região da imagem que projeta (desfocada ou não)
    Vec4 target{};       ///< texels da saída
};

class RadialShadowReference final : public Effect {
public:
    enum : u32 { kColor = 0, kOpacity, kLight, kDistance, kSoftness, kRender, kInfluence, kShadowOnly, kResize };

    const EffectInfo& info() const noexcept override {
        static const EffectInfo info{kRadialShadowReference, "Sombra radial", "Estilizar", EffectClass::Neighborhood};
        return info;
    }

    void declare_parameters(ParameterRegistry& p) const override {
        p.add_color("shadow_color", "Cor da sombra", Vec4{0, 0, 0, 1});
        p.add_float("opacity", "Opacidade", 50, 0, 100, kParamAnimatable | kParamPercent, "%");
        p.add_point2("light_source", "Fonte de luz", Vec2{.25f, .25f}, -1, 2, kParamAnimatable | kParamRelative);
        p.typed_range(-10, 10);
        p.add_float("projection_distance", "Distância da projeção", 10, 0, 50, kParamAnimatable);
        p.typed_range(0, 100);
        p.add_float("softness", "Suavidade", 0, 0, 50, kParamAnimatable | kParamPixels, "px");
        p.typed_range(0, 100);
        static const char* const renders[] = {"Normal", "Borda de vidro"};
        p.add_enum("render", "Renderizar", renders, 2, 0);
        p.add_float("color_influence", "Influência da cor", 100, 0, 100, kParamAnimatable | kParamPercent, "%");
        p.typed_range(0, 200);
        p.add_bool("shadow_only", "Só a sombra", false);
        p.add_bool("resize_layer", "Redimensionar camada", false);
    }

    bool demo_values(EffectInstance&, std::vector<ParamValue>& values) const noexcept override {
        values[kOpacity] = ParamValue::scalar(75);
        values[kResize] = ParamValue::boolean(true);
        return true;
    }

    bool is_identity(const EffectEval& e) const noexcept override {
        return !e.b(kShadowOnly) && !(finite_value(e.f(kOpacity), 50) > 0);
    }

    f32 input_margin(const EffectEval&) const noexcept override { return 0; }
    bool needs_full_input() const noexcept override { return true; }

    void pipelines(std::vector<PipelineKey>& out, SurfaceFormat format) const override {
        out.push_back(PipelineKey::fullscreen(ShaderId::effects_radial_shadow_reference_frag, format));
        fast_blur_reference::pipelines(out, format);
    }

    Status build(EffectBuildContext& ctx, const EffectEval& e, const LayerImage& input,
                 f32 margin, LayerImage& out) const override {
        const f64 width = e.placement && e.placement->layerWidth ? e.placement->layerWidth : input.region.w;
        const f64 height = e.placement && e.placement->layerHeight ? e.placement->layerHeight : input.region.h;
        const Vec2 light = e.p2(kLight);
        const f64 lx = std::clamp(finite_value(light.x, .25), -10., 10.) * width;
        const f64 ly = std::clamp(finite_value(light.y, .25), -10., 10.) * height;
        const f64 distance = std::clamp(finite_value(e.f(kDistance), 10), 0., 100.) * .01;
        const f64 opacity = std::clamp(finite_value(e.f(kOpacity), 50), 0., 100.) * .01;
        const f64 softness = std::clamp(finite_value(e.f(kSoftness), 0), 0., 100.) * .5;
        const f64 influence = std::clamp(finite_value(e.f(kInfluence), 100), 0., 200.) * .01;
        const bool glass = e.e(kRender) == 1 && influence > 0;
        const bool shadowOnly = e.b(kShadowOnly);

        // Saída: a caixa da camada, ou (Redimensionar) a caixa da sombra
        // projetada com a margem da suavidade, como na preparação do quadro.
        Rect region = input.region;
        const f64 pad = softness > 0 ? std::floor(softness) + 1 : 0;
        if (e.b(kResize) && opacity > 0 && (distance > 0 || softness > 0)) {
            const f64 k = 1 / (1 - std::min(distance, .75));
            const auto project = [&](f64 v, f64 l) { return (v - l) * k + l; };
            f64 left = std::min(project(-pad, lx), project(width + 2 * pad, lx)) - 1;
            f64 right = std::max(project(-pad, lx), project(width + 2 * pad, lx)) + 2;
            f64 top = std::min(project(-pad, ly), project(height + 2 * pad, ly)) - 1;
            f64 bottom = std::max(project(-pad, ly), project(height + 2 * pad, ly)) + 2;
            if (!shadowOnly) {
                left = std::min(left, -pad); right = std::max(right, width + 2 * pad);
                top = std::min(top, -pad); bottom = std::max(bottom, height + 2 * pad);
            }
            left = std::floor(left); top = std::floor(top);
            const f64 w = std::min(std::floor(right) - left, 30000.), h = std::min(std::floor(bottom) - top, 30000.);
            if (w >= width && h >= height) {
                const Rect grown{static_cast<f32>(left), static_cast<f32>(top), static_cast<f32>(w), static_cast<f32>(h)};
                region = spread_region(grown, 0, 0, e.placement, margin);
            }
        }
        u32 outW = input.width, outH = input.height;
        if (region.x != input.region.x || region.y != input.region.y || region.w != input.region.w ||
            region.h != input.region.h)
            ctx.region_size(region, input.texel_scale_x(), outW, outH);

        LayerImage caster = input;
        if (softness > 0) {
            fast_blur_reference::Request blur;
            blur.radiusX = blur.radiusY = static_cast<f32>(softness * input.texel_scale_x());
            blur.repeatEdges = false;
            blur.outRegion = Rect{input.region.x - static_cast<f32>(pad + softness), input.region.y - static_cast<f32>(pad + softness),
                                  input.region.w + static_cast<f32>(2 * (pad + softness)),
                                  input.region.h + static_cast<f32>(2 * (pad + softness))};
            if (const auto status = fast_blur_reference::build(ctx, input, blur, caster); !status.ok()) return status;
        }

        RadialShadowUniforms u;
        u.light = {static_cast<f32>(lx), static_cast<f32>(ly), static_cast<f32>(distance), static_cast<f32>(opacity)};
        u.mode = {glass ? 1.f : 0.f, static_cast<f32>(influence), shadowOnly ? 1.f : 0.f, 0};
        u.color = e.color(kColor);
        u.outRegion = {region.x, region.y, region.w, region.h};
        u.sourceRegion = {input.region.x, input.region.y, input.region.w, input.region.h};
        u.shadowRegion = {caster.region.x, caster.region.y, caster.region.w, caster.region.h};
        u.target = {static_cast<f32>(outW), static_cast<f32>(outH), 0, 0};
        out = LayerImage{ctx.texture("radial-shadow-reference", outW, outH), region, outW, outH};
        if (ctx.fullscreen_pass("radial-shadow-reference", PassStage::Effects, out.texture,
                ShaderId::effects_radial_shadow_reference_frag,
                {{input.texture, {}, CommonSampler::LinearBorder}, {caster.texture, {}, CommonSampler::LinearBorder}},
                &u, sizeof(u)) == kInvalidIndex)
            return Errc::PipelineCompileFailed;
        return OkStatus;
    }
};

} // namespace

void register_radial_shadow_reference(EffectRegistry& registry) {
    (void)registry.add(std::make_unique<RadialShadowReference>());
}

} // namespace aurea::builtin
