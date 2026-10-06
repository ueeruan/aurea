// =============================================================================
//  Efeitos de acabamento do pacote de paridade.
//
//  Desfoque radial · Espelho · Cortar bordas · Vinheta · Mosaico/LED ·
//  Detectar bordas · Matiz e saturação
//
//  Cada um é um passe de tela cheia com o bloco de uniforms comum
//  (EffectUniforms). Toda posição é medida no plano da camada, em px da
//  resolução cheia: a prévia reduzida e o export cortam/centram no mesmo lugar.
// =============================================================================
#include "BuiltinEffects.hpp"

#include <algorithm>
#include <cmath>

namespace aurea::builtin {
namespace {

f32 finite_or(f32 v, f32 fallback) noexcept { return std::isfinite(v) ? v : fallback; }

Vec2 layer_size(const EffectEval& e, const LayerImage& input) noexcept {
    if (e.placement && e.placement->layerWidth && e.placement->layerHeight) {
        return Vec2{static_cast<f32>(e.placement->layerWidth), static_cast<f32>(e.placement->layerHeight)};
    }
    return Vec2{input.region.w, input.region.h};
}

Vec4 region_vec(const Rect& r) noexcept { return Vec4{r.x, r.y, r.w, r.h}; }

// -----------------------------------------------------------------------------
// Desfoque radial (giro ou zoom)
// -----------------------------------------------------------------------------
class RadialBlur final : public Effect {
public:
    enum : u32 { kType = 0, kAmount, kCenter, kQuality, kMix };

    const EffectInfo& info() const noexcept override {
        static const EffectInfo i{effect_keys::kRadialBlur, "Desfoque radial", "Desfoque", EffectClass::Neighborhood};
        return i;
    }
    void declare_parameters(ParameterRegistry& p) const override {
        static const char* const kTypes[] = {"Giro", "Zoom"};
        p.add_enum("type", "Tipo", kTypes, 2, 0);
        // Giro: graus de arco; zoom: % da distância ao centro. O custo é fixo
        // (as amostras), então a faixa digitada vai longe.
        p.add_float("amount", "Intensidade", 10.0f, 0.0f, 100.0f);
        p.typed_range(0.0f, 360.0f);
        p.add_point2("center", "Centro", Vec2{0.5f, 0.5f}, -1.0f, 2.0f, kParamAnimatable | kParamRelative);
        p.typed_range(-10.0f, 10.0f);
        static const char* const kQualities[] = {"Rascunho", "Boa", "Alta"};
        p.add_enum("quality", "Qualidade", kQualities, 3, 1);
        p.add_float("mix", "Mistura", 100.0f, 0.0f, 100.0f, kParamAnimatable | kParamPercent, "%");
    }
    bool demo_values(EffectInstance&, std::vector<ParamValue>& v) const noexcept override {
        v[kType] = ParamValue::scalar(1.0f);
        v[kAmount] = ParamValue::scalar(30.0f);
        return true;
    }
    bool is_identity(const EffectEval& e) const noexcept override {
        return !(finite_or(e.f(kAmount), 0.0f) > 0.01f) || !(finite_or(e.f(kMix), 0.0f) > 0.01f);
    }
    void pipelines(std::vector<PipelineKey>& out, SurfaceFormat work) const override {
        out.push_back(PipelineKey::fullscreen(ShaderId::effects_radial_blur_frag, work));
    }
    /// Quanto um ponto pode andar (px): o rastro passa da caixa por isso.
    static f32 reach(const EffectEval& e, Vec2 size, Vec2 center) noexcept {
        const f32 dx = std::max(std::fabs(center.x), std::fabs(size.x - center.x));
        const f32 dy = std::max(std::fabs(center.y), std::fabs(size.y - center.y));
        const f32 far = std::sqrt(dx * dx + dy * dy);
        const f32 amount = std::clamp(finite_or(e.f(kAmount), 0.0f), 0.0f, 360.0f);
        if (e.e(kType) == 1) return far * std::min(amount / 100.0f, 2.0f) * 0.5f;
        const f32 halfArc = std::min(amount * 0.5f * kDeg2Rad, kPi);   // corda do meio arco
        return 2.0f * far * std::sin(halfArc * 0.5f);
    }
    f32 input_margin(const EffectEval& e) const noexcept override {
        const Vec2 size = e.placement ? Vec2{static_cast<f32>(e.placement->layerWidth),
                                             static_cast<f32>(e.placement->layerHeight)} : Vec2{0, 0};
        const Vec2 c = e.p2(kCenter);
        return reach(e, size, Vec2{c.x * size.x, c.y * size.y});
    }
    Status build(EffectBuildContext& ctx, const EffectEval& e, const LayerImage& input, f32 margin,
                 LayerImage& out) const override {
        const Vec2 size = layer_size(e, input);
        const Vec2 rel = e.p2(kCenter);
        const Vec2 center{finite_or(rel.x, 0.5f) * size.x, finite_or(rel.y, 0.5f) * size.y};
        const f32 r = reach(e, size, center);
        const Rect region = spread_region(input.region, r, r, e.placement, margin);
        u32 w = 0, h = 0;
        ctx.region_size(region, input.texel_scale_x(), w, h);

        EffectUniforms u = base_uniforms(input);
        const bool zoom = e.e(kType) == 1;
        const f32 amount = std::clamp(finite_or(e.f(kAmount), 0.0f), 0.0f, 360.0f);
        static constexpr f32 kSamples[] = {12.0f, 24.0f, 48.0f};
        const f32 quality = std::clamp(ctx.resources().effect_quality(), 0.25f, 1.0f);
        const f32 samples = std::max(8.0f, std::round(kSamples[std::min<u32>(e.e(kQuality), 2u)] * quality));
        u.p0 = Vec4{zoom ? 1.0f : 0.0f, zoom ? amount / 100.0f : amount * kDeg2Rad, samples,
                    std::clamp(finite_or(e.f(kMix), 100.0f) / 100.0f, 0.0f, 1.0f)};
        u.p1 = Vec4{center.x, center.y, 0.0f, 0.0f};
        u.p2 = region_vec(region);
        u.p3 = region_vec(input.region);
        out = LayerImage{ctx.texture("desfoque-radial", w, h), region, w, h};
        if (ctx.fullscreen_pass("desfoque-radial", PassStage::Effects, out.texture, ShaderId::effects_radial_blur_frag,
                                {PassTexture{input.texture, {}, CommonSampler::LinearBorder}},
                                &u, sizeof(u)) == kInvalidIndex) {
            return Errc::PipelineCompileFailed;
        }
        return OkStatus;
    }
};

// -----------------------------------------------------------------------------
// Espelho
// -----------------------------------------------------------------------------
class Mirror final : public Effect {
public:
    enum : u32 { kCenter = 0, kAngle, kFlip };

    const EffectInfo& info() const noexcept override {
        static const EffectInfo i{effect_keys::kMirror, "Espelho", "Distorcer", EffectClass::Domain};
        return i;
    }
    void declare_parameters(ParameterRegistry& p) const override {
        p.add_point2("center", "Centro", Vec2{0.5f, 0.5f}, -1.0f, 2.0f, kParamAnimatable | kParamRelative);
        p.typed_range(-10.0f, 10.0f);
        // 0° = reta vertical, a metade da esquerda se repete à direita.
        p.add_angle("angle", "Ângulo", 0.0f);
        p.add_bool("flip_side", "Trocar o lado", false);
    }
    void pipelines(std::vector<PipelineKey>& out, SurfaceFormat work) const override {
        out.push_back(PipelineKey::fullscreen(ShaderId::effects_mirror_frag, work));
    }
    Status build(EffectBuildContext& ctx, const EffectEval& e, const LayerImage& input, f32,
                 LayerImage& out) const override {
        const Vec2 size = layer_size(e, input);
        const Vec2 rel = e.p2(kCenter);
        const f32 a = finite_or(e.f(kAngle), 0.0f) * kDeg2Rad;
        const f32 side = e.b(kFlip) ? -1.0f : 1.0f;
        EffectUniforms u = base_uniforms(input);
        u.p0 = Vec4{finite_or(rel.x, 0.5f) * size.x, finite_or(rel.y, 0.5f) * size.y,
                    std::cos(a) * side, std::sin(a) * side};
        u.p2 = region_vec(input.region);
        return single_pass(ctx, ShaderId::effects_mirror_frag, input, u, "espelho", out);
    }
};

// -----------------------------------------------------------------------------
// Cortar bordas
// -----------------------------------------------------------------------------
class CropEdges final : public Effect {
public:
    enum : u32 { kLeft = 0, kTop, kRight, kBottom, kFeather };

    const EffectInfo& info() const noexcept override {
        static const EffectInfo i{effect_keys::kCrop, "Cortar bordas", "Recorte", EffectClass::Neighborhood};
        return i;
    }
    void declare_parameters(ParameterRegistry& p) const override {
        p.add_float("left", "Esquerda", 0.0f, 0.0f, 100.0f, kParamAnimatable | kParamPercent, "%");
        p.add_float("top", "Topo", 0.0f, 0.0f, 100.0f, kParamAnimatable | kParamPercent, "%");
        p.add_float("right", "Direita", 0.0f, 0.0f, 100.0f, kParamAnimatable | kParamPercent, "%");
        p.add_float("bottom", "Base", 0.0f, 0.0f, 100.0f, kParamAnimatable | kParamPercent, "%");
        p.add_float("feather", "Suavidade da borda", 0.0f, 0.0f, 200.0f, kParamAnimatable | kParamPixels, "px");
        p.typed_range(0.0f, 5000.0f);
    }
    bool demo_values(EffectInstance&, std::vector<ParamValue>& v) const noexcept override {
        v[kLeft] = ParamValue::scalar(15.0f);
        v[kRight] = ParamValue::scalar(15.0f);
        v[kTop] = ParamValue::scalar(10.0f);
        return true;
    }
    bool is_identity(const EffectEval& e) const noexcept override {
        return e.f(kLeft) <= 0.0f && e.f(kTop) <= 0.0f && e.f(kRight) <= 0.0f && e.f(kBottom) <= 0.0f;
    }
    void pipelines(std::vector<PipelineKey>& out, SurfaceFormat work) const override {
        out.push_back(PipelineKey::fullscreen(ShaderId::effects_crop_edges_frag, work));
    }
    Status build(EffectBuildContext& ctx, const EffectEval& e, const LayerImage& input, f32,
                 LayerImage& out) const override {
        const Vec2 size = layer_size(e, input);
        auto frac = [&](u32 i) { return std::clamp(finite_or(e.f(i), 0.0f), 0.0f, 100.0f) / 100.0f; };
        EffectUniforms u = base_uniforms(input);
        u.p0 = Vec4{frac(kLeft) * size.x, frac(kTop) * size.y, (1.0f - frac(kRight)) * size.x,
                    (1.0f - frac(kBottom)) * size.y};
        u.p1 = Vec4{std::clamp(finite_or(e.f(kFeather), 0.0f), 0.0f, 5000.0f), 0.0f, 0.0f, 0.0f};
        u.p2 = region_vec(input.region);
        return single_pass(ctx, ShaderId::effects_crop_edges_frag, input, u, "cortar-bordas", out);
    }
};

// -----------------------------------------------------------------------------
// Vinheta
// -----------------------------------------------------------------------------
class Vignette final : public Effect {
public:
    enum : u32 { kAmount = 0, kSize, kSoftness, kRoundness, kCenter, kColor };

    const EffectInfo& info() const noexcept override {
        static const EffectInfo i{effect_keys::kVignette, "Vinheta", "Estilizar", EffectClass::Neighborhood};
        return i;
    }
    void declare_parameters(ParameterRegistry& p) const override {
        p.add_float("amount", "Intensidade", 50.0f, -100.0f, 100.0f, kParamAnimatable | kParamPercent, "%");
        p.add_float("size", "Tamanho", 50.0f, 0.0f, 100.0f, kParamAnimatable | kParamPercent, "%");
        p.add_float("softness", "Suavidade", 50.0f, 0.0f, 100.0f, kParamAnimatable | kParamPercent, "%");
        p.add_float("roundness", "Arredondamento", 0.0f, -100.0f, 100.0f, kParamAnimatable | kParamPercent, "%");
        p.add_point2("center", "Centro", Vec2{0.5f, 0.5f}, -1.0f, 2.0f, kParamAnimatable | kParamRelative);
        p.typed_range(-10.0f, 10.0f);
        p.add_color("color", "Cor", Vec4{0.0f, 0.0f, 0.0f, 1.0f});
    }
    bool is_identity(const EffectEval& e) const noexcept override {
        return std::fabs(finite_or(e.f(kAmount), 0.0f)) < 0.01f;
    }
    void pipelines(std::vector<PipelineKey>& out, SurfaceFormat work) const override {
        out.push_back(PipelineKey::fullscreen(ShaderId::effects_vignette_frag, work));
    }
    Status build(EffectBuildContext& ctx, const EffectEval& e, const LayerImage& input, f32,
                 LayerImage& out) const override {
        const Vec2 size = layer_size(e, input);
        const Vec2 rel = e.p2(kCenter);
        EffectUniforms u = base_uniforms(input);
        u.p0 = Vec4{std::clamp(finite_or(e.f(kAmount), 0.0f) / 100.0f, -1.0f, 1.0f),
                    std::clamp(finite_or(e.f(kSize), 50.0f) / 100.0f, 0.0f, 1.0f),
                    std::clamp(finite_or(e.f(kSoftness), 50.0f) / 100.0f, 0.0f, 1.0f),
                    std::clamp(finite_or(e.f(kRoundness), 0.0f) / 100.0f, -1.0f, 1.0f)};
        u.p1 = Vec4{finite_or(rel.x, 0.5f) * size.x, finite_or(rel.y, 0.5f) * size.y, size.x, size.y};
        u.p2 = region_vec(input.region);
        u.color = e.color(kColor);
        return single_pass(ctx, ShaderId::effects_vignette_frag, input, u, "vinheta", out);
    }
};

// -----------------------------------------------------------------------------
// Mosaico / painel de LED
// -----------------------------------------------------------------------------
class Mosaic final : public Effect {
public:
    enum : u32 { kCell = 0, kGap, kRound, kShade, kCellVignette, kBackground, kStyle, kVignette };

    const EffectInfo& info() const noexcept override {
        static const EffectInfo i{effect_keys::kMosaic, "Mosaico / LED", "Estilizar", EffectClass::Neighborhood};
        return i;
    }
    void declare_parameters(ParameterRegistry& p) const override {
        // 9 amostras por pixel qualquer que seja a célula: a faixa digitada
        // vai longe sem custo.
        p.add_float("cell_size", "Tamanho da célula", 12.0f, 2.0f, 200.0f, kParamAnimatable | kParamPixels, "px");
        p.typed_range(1.0f, 4000.0f);
        p.add_float("gap", "Vão entre células", 0.0f, 0.0f, 90.0f, kParamAnimatable | kParamPercent, "%");
        p.add_bool("round", "Células redondas", false);
        p.add_float("shade", "Sombreado", 0.0f, 0.0f, 100.0f, kParamAnimatable | kParamPercent, "%");
        p.add_float("cell_vignette", "Vinheta da célula", 0.0f, 0.0f, 100.0f, kParamAnimatable | kParamPercent, "%");
        p.add_color("background", "Cor do fundo", Vec4{0.0f, 0.0f, 0.0f, 0.0f});
        // Os três looks do Pixelar / LED do app antigo. "Livre" é o de sempre
        // (os controles acima valem); os outros fixam vão, forma, sombreado e
        // fundo do look e deixam o tamanho da célula, a cor do fundo (a placa
        // da Parede de LED) e a vinheta com o usuário. No fim da lista:
        // projetos antigos abrem em "Livre", idênticos.
        static const char* const kStyles[] = {"Livre", "Mosaico de blocos", "Parede de LED", "Matriz de pontos"};
        p.add_enum("style", "Estilo", kStyles, 4, 0);
        // Vinheta sobre a caixa da camada (só a cor, nunca o alfa).
        p.add_float("vignette", "Vinheta", 0.0f, 0.0f, 100.0f, kParamAnimatable | kParamPercent, "%");
    }
    bool demo_values(EffectInstance&, std::vector<ParamValue>& v) const noexcept override {
        v[kCell] = ParamValue::scalar(8.0f);
        v[kGap] = ParamValue::scalar(20.0f);
        v[kRound] = ParamValue::boolean(true);
        v[kShade] = ParamValue::scalar(60.0f);
        v[kBackground] = ParamValue::color(0.0f, 0.0f, 0.0f, 1.0f);
        return true;
    }
    f32 input_margin(const EffectEval& e) const noexcept override {
        return std::clamp(finite_or(e.f(kCell), 12.0f), 1.0f, 4000.0f);
    }
    void pipelines(std::vector<PipelineKey>& out, SurfaceFormat work) const override {
        out.push_back(PipelineKey::fullscreen(ShaderId::effects_mosaic_cells_frag, work));
    }
    Status build(EffectBuildContext& ctx, const EffectEval& e, const LayerImage& input, f32,
                 LayerImage& out) const override {
        EffectUniforms u = base_uniforms(input);
        const f32 k = std::max(input.texel_scale_x(), 1e-4f);
        u.p0 = Vec4{std::clamp(finite_or(e.f(kCell), 12.0f), 1.0f, 4000.0f),
                    std::clamp(finite_or(e.f(kGap), 0.0f) / 100.0f, 0.0f, 0.9f),
                    e.b(kRound) ? 1.0f : 0.0f,
                    std::clamp(finite_or(e.f(kShade), 0.0f) / 100.0f, 0.0f, 1.0f)};
        u.p1 = Vec4{std::clamp(finite_or(e.f(kCellVignette), 0.0f) / 100.0f, 0.0f, 1.0f), 1.0f / k, 0.0f, 0.0f};
        u.p2 = region_vec(input.region);
        const Vec2 box = layer_size(e, input);
        u.p3 = Vec4{static_cast<f32>(std::min(e.e(kStyle), 3u)),
                    std::clamp(finite_or(e.f(kVignette), 0.0f) / 100.0f, 0.0f, 1.0f), box.x, box.y};
        u.color = e.color(kBackground);
        return single_pass(ctx, ShaderId::effects_mosaic_cells_frag, input, u, "mosaico", out);
    }
};

// -----------------------------------------------------------------------------
// Detectar bordas
// -----------------------------------------------------------------------------
class FindEdges final : public Effect {
public:
    enum : u32 { kIntensity = 0, kWidth, kInvert, kMix };

    const EffectInfo& info() const noexcept override {
        static const EffectInfo i{effect_keys::kFindEdges, "Detectar bordas", "Estilizar", EffectClass::Neighborhood};
        return i;
    }
    void declare_parameters(ParameterRegistry& p) const override {
        p.add_float("intensity", "Intensidade", 100.0f, 0.0f, 400.0f, kParamAnimatable | kParamPercent, "%");
        p.typed_range(0.0f, 4000.0f);
        p.add_float("width", "Largura", 1.0f, 0.5f, 10.0f, kParamAnimatable | kParamPixels, "px");
        p.typed_range(0.25f, 100.0f);
        p.add_bool("invert", "Inverter", true);
        p.add_float("mix", "Mistura", 100.0f, 0.0f, 100.0f, kParamAnimatable | kParamPercent, "%");
    }
    bool is_identity(const EffectEval& e) const noexcept override {
        return !(finite_or(e.f(kMix), 0.0f) > 0.01f);
    }
    f32 input_margin(const EffectEval& e) const noexcept override {
        return std::clamp(finite_or(e.f(kWidth), 1.0f), 0.25f, 100.0f);
    }
    void pipelines(std::vector<PipelineKey>& out, SurfaceFormat work) const override {
        out.push_back(PipelineKey::fullscreen(ShaderId::effects_find_edges_frag, work));
    }
    Status build(EffectBuildContext& ctx, const EffectEval& e, const LayerImage& input, f32,
                 LayerImage& out) const override {
        EffectUniforms u = base_uniforms(input);
        // A largura é em px da camada: vira texels pela densidade da entrada.
        u.p0 = Vec4{std::clamp(finite_or(e.f(kIntensity), 100.0f) / 100.0f, 0.0f, 40.0f),
                    std::clamp(finite_or(e.f(kWidth), 1.0f), 0.25f, 100.0f) * input.texel_scale_x(),
                    e.b(kInvert) ? 1.0f : 0.0f,
                    std::clamp(finite_or(e.f(kMix), 100.0f) / 100.0f, 0.0f, 1.0f)};
        return single_pass(ctx, ShaderId::effects_find_edges_frag, input, u, "detectar-bordas", out);
    }
};

// -----------------------------------------------------------------------------
// Matiz e saturação
// -----------------------------------------------------------------------------
class HueSaturation final : public Effect {
public:
    enum : u32 { kHue = 0, kSaturation, kLightness, kColorize, kColorizeHue, kColorizeSaturation, kMix };

    const EffectInfo& info() const noexcept override {
        // Passe próprio (HSL na cor codificada não é uma matriz linear), como o
        // Colorama: por isso a classe de vizinhança, não PerPixel.
        static const EffectInfo i{effect_keys::kHueSaturation, "Matiz e saturação", "Cor", EffectClass::Neighborhood};
        return i;
    }
    void declare_parameters(ParameterRegistry& p) const override {
        p.add_angle("hue", "Matiz", 0.0f);
        p.add_float("saturation", "Saturação", 0.0f, -100.0f, 100.0f, kParamAnimatable | kParamPercent, "%");
        p.add_float("lightness", "Luminosidade", 0.0f, -100.0f, 100.0f, kParamAnimatable | kParamPercent, "%");
        p.add_bool("colorize", "Colorir", false);
        p.add_angle("colorize_hue", "Matiz ao colorir", 0.0f);
        p.add_float("colorize_saturation", "Saturação ao colorir", 25.0f, 0.0f, 100.0f, kParamAnimatable | kParamPercent, "%");
        p.add_float("mix", "Mistura", 100.0f, 0.0f, 100.0f, kParamAnimatable | kParamPercent, "%");
    }
    bool demo_values(EffectInstance&, std::vector<ParamValue>& v) const noexcept override {
        v[kHue] = ParamValue::scalar(150.0f);
        v[kSaturation] = ParamValue::scalar(20.0f);
        return true;
    }
    bool is_identity(const EffectEval& e) const noexcept override {
        if (!(finite_or(e.f(kMix), 0.0f) > 0.01f)) return true;
        if (e.b(kColorize)) return false;
        const f32 hue = std::fmod(std::fabs(finite_or(e.f(kHue), 0.0f)), 360.0f);
        return (hue < 0.01f || hue > 359.99f) && std::fabs(finite_or(e.f(kSaturation), 0.0f)) < 0.01f
            && std::fabs(finite_or(e.f(kLightness), 0.0f)) < 0.01f;
    }
    void pipelines(std::vector<PipelineKey>& out, SurfaceFormat work) const override {
        out.push_back(PipelineKey::fullscreen(ShaderId::effects_hue_saturation_frag, work));
    }
    Status build(EffectBuildContext& ctx, const EffectEval& e, const LayerImage& input, f32,
                 LayerImage& out) const override {
        EffectUniforms u = base_uniforms(input);
        u.p0 = Vec4{finite_or(e.f(kHue), 0.0f) / 360.0f,
                    std::clamp(finite_or(e.f(kSaturation), 0.0f) / 100.0f, -1.0f, 1.0f),
                    std::clamp(finite_or(e.f(kLightness), 0.0f) / 100.0f, -1.0f, 1.0f),
                    e.b(kColorize) ? 1.0f : 0.0f};
        u.p1 = Vec4{finite_or(e.f(kColorizeHue), 0.0f) / 360.0f,
                    std::clamp(finite_or(e.f(kColorizeSaturation), 25.0f) / 100.0f, 0.0f, 1.0f),
                    std::clamp(finite_or(e.f(kMix), 100.0f) / 100.0f, 0.0f, 1.0f), 0.0f};
        return single_pass(ctx, ShaderId::effects_hue_saturation_frag, input, u, "matiz-saturacao", out);
    }
};

// -----------------------------------------------------------------------------
// Sombra projetada — a silhueta da camada deslocada, borrada e pintada ATRÁS
// dela. É o efeito mais usado de qualquer editor e o único que faltava para
// separar texto e forma do fundo.
//
// Dois passes separáveis: o primeiro desloca e borra pela DIREÇÃO, o segundo
// fecha o borrão na PERPENDICULAR e compõe. O eixo é convertido em uv aqui
// (direção em graus × distância em px ÷ tamanho da região em cada eixo), o que
// é o que faz uma sombra na diagonal ter o mesmo comprimento de uma na
// vertical.
//
// A região cresce por distância + suavidade e é recortada ao quadro: uma
// camada deslocada para fora continua com a sombra desenhada, sem alocar uma
// textura do tamanho da soma.
// -----------------------------------------------------------------------------
class DropShadow final : public Effect {
public:
    enum : u32 { kShadowColor = 0, kOpacity, kDirection, kDistance, kSoftness, kShadowOnly };

    const EffectInfo& info() const noexcept override {
        static const EffectInfo i{effect_keys::kDropShadow, "Sombra projetada", "Estilizar",
                                  EffectClass::Neighborhood};
        return i;
    }
    void declare_parameters(ParameterRegistry& p) const override {
        p.add_color("shadow_color", "Cor da sombra", Vec4{0.0f, 0.0f, 0.0f, 1.0f});
        p.add_float("opacity", "Opacidade", 70.0f, 0.0f, 100.0f, kParamAnimatable | kParamPercent, "%");
        p.add_angle("direction", "Direção", 135.0f);
        p.add_float("distance", "Distância", 18.0f, 0.0f, 500.0f, kParamAnimatable | kParamPixels, "px");
        p.typed_range(0.0f, 5000.0f);
        p.add_float("softness", "Suavidade", 12.0f, 0.0f, 500.0f, kParamAnimatable | kParamPixels, "px");
        p.typed_range(0.0f, 5000.0f);
        // Append only: saved parameters and their keyframe addresses stay stable.
        p.add_bool("shadow_only", "Só a sombra", false);
    }
    bool is_identity(const EffectEval& e) const noexcept override {
        // A zero-opacity isolated shadow is transparent, not the original clip.
        if (e.count > kShadowOnly && e.b(kShadowOnly)) return false;
        return !(e.color(kShadowColor).w > 0.0f) || !(e.f(kOpacity) > 0.01f);
    }
    bool demo_values(EffectInstance&, std::vector<ParamValue>& v) const noexcept override {
        v[kShadowColor] = ParamValue::color(0.0f, 0.0f, 0.0f, 1.0f);
        v[kOpacity] = ParamValue::scalar(85.0f);
        v[kDistance] = ParamValue::scalar(34.0f);
        v[kSoftness] = ParamValue::scalar(22.0f);
        return true;
    }
    f32 input_margin(const EffectEval& e) const noexcept override {
        return std::clamp(finite_or(e.f(kDistance), 0.0f), 0.0f, 5000.0f) +
               std::clamp(finite_or(e.f(kSoftness), 0.0f), 0.0f, 5000.0f);
    }
    void pipelines(std::vector<PipelineKey>& out, SurfaceFormat work) const override {
        out.push_back(PipelineKey::fullscreen(ShaderId::effects_drop_shadow_blur_frag, work));
        out.push_back(PipelineKey::fullscreen(ShaderId::effects_drop_shadow_combine_frag, work));
    }
    Status build(EffectBuildContext& ctx, const EffectEval& e, const LayerImage& input, f32 margin,
                 LayerImage& out) const override {
        const f32 distance = std::clamp(finite_or(e.f(kDistance), 0.0f), 0.0f, 5000.0f);
        const f32 softness = std::clamp(finite_or(e.f(kSoftness), 0.0f), 0.0f, 5000.0f);
        const f32 reach = distance + softness;
        const Rect region = spread_region(input.region, reach, reach, e.placement, margin);
        if (region.w <= 0.0f || region.h <= 0.0f) return Errc::InvalidArgument;

        u32 w = 0, h = 0;
        ctx.region_size(region, input.texel_scale_x(), w, h);

        // Direção em graus. Os passos vão em PIXELS DA CAMADA e o shader os
        // converte para uv com a densidade do que ele amostra — é o que faz a
        // mesma distância deslocar o mesmo tanto na horizontal e na vertical.
        const f32 ang = finite_or(e.f(kDirection), 135.0f) * kDeg2Rad;
        const f32 dx = std::cos(ang), dy = std::sin(ang);
        const f32 perStep = softness / (2.0f * 8.0f);

        const Vec4 toInput = EffectBuildContext::uv_map(region, input.region);
        struct {
            Vec4 uvMap; Vec4 texel; Vec4 p0; Vec4 p1; Vec4 p2; Vec4 p3; Vec4 color;
        } u{};
        u.uvMap = toInput;
        u.texel = Vec4{region.w > 0.0f ? 1.0f / region.w : 0.0f, region.h > 0.0f ? 1.0f / region.h : 0.0f,
                       input.texel_scale_x(), input.texel_scale_y()};

        // 1. Silhueta deslocada e borrada pela direção. A amostra é a textura
        // da entrada, então a densidade é a da região DELA.
        u.p0 = Vec4{dx * perStep, dy * perStep, 0.0f, 0.0f};
        u.p1 = Vec4{dx * distance, dy * distance, 0.0f, 0.0f};
        u.p3 = Vec4{std::max(input.region.w, 1e-4f), std::max(input.region.h, 1e-4f), 0.0f, 0.0f};
        const FGTexture alpha = ctx.texture("sombra-alfa", w, h);
        if (ctx.fullscreen_pass("sombra-alfa", PassStage::Effects, alpha,
                                ShaderId::effects_drop_shadow_blur_frag,
                                {PassTexture{input.texture, {}, CommonSampler::LinearBorder}},
                                &u, sizeof(u)) == kInvalidIndex) {
            return Errc::PipelineCompileFailed;
        }

        // 2. Fecha o borrão na perpendicular e compõe atrás da camada.
        // A perpendicular é a direção girada 90°: (-dy, dx). Agora a amostra é
        // a silhueta, que já está na região do efeito — a densidade é a dela e
        // o mapa é identidade.
        u.p0 = Vec4{-dy * perStep, dx * perStep, e.count > kShadowOnly && e.b(kShadowOnly) ? 1.0f : 0.0f,
                    std::clamp(finite_or(e.f(kOpacity), 0.0f) / 100.0f, 0.0f, 1.0f)};
        u.p1 = Vec4{0.0f, 0.0f, 0.0f, 0.0f};
        u.p2 = toInput;                 // só a camada usa este mapa
        u.p3 = Vec4{std::max(region.w, 1e-4f), std::max(region.h, 1e-4f), 0.0f, 0.0f};
        u.color = e.color(kShadowColor);
        u.uvMap = Vec4{1.0f, 1.0f, 0.0f, 0.0f};

        const FGTexture result = ctx.texture("sombra-projetada", w, h);
        if (ctx.fullscreen_pass("sombra-projetada", PassStage::Effects, result,
                                ShaderId::effects_drop_shadow_combine_frag,
                                {PassTexture{alpha, {}, CommonSampler::LinearClamp},
                                 PassTexture{input.texture, {}, CommonSampler::LinearBorder}},
                                &u, sizeof(u)) == kInvalidIndex) {
            return Errc::PipelineCompileFailed;
        }
        out = LayerImage{result, region, w, h};
        return OkStatus;
    }
};

// -----------------------------------------------------------------------------
// Borda — o contorno sólido em volta da silhueta da camada. Dilatação por
// máximo (não por média: a borda não pode desbotar) em dois passes separáveis,
// com o traço em pixels da camada.
//
// O anel é o que a dilatação ganhou MENOS o que a camada já cobria — é o que
// impede o traço de invadir a imagem e escurecê-la por dentro. Ele é composto
// por baixo da camada, então uma franja semitransparente ganha a cor sem
// apagar o que estava lá.
// -----------------------------------------------------------------------------
class Border final : public Effect {
public:
    enum : u32 { kColor = 0, kWidth, kOpacity };

    const EffectInfo& info() const noexcept override {
        static const EffectInfo i{effect_keys::kBorder, "Borda", "Estilizar", EffectClass::Neighborhood};
        return i;
    }
    void declare_parameters(ParameterRegistry& p) const override {
        p.add_color("color", "Cor", Vec4{1.0f, 1.0f, 1.0f, 1.0f});
        p.add_float("width", "Largura", 8.0f, 0.5f, 200.0f, kParamAnimatable | kParamPixels, "px");
        p.typed_range(0.1f, 2000.0f);
        p.add_float("opacity", "Opacidade", 100.0f, 0.0f, 100.0f, kParamAnimatable | kParamPercent, "%");
    }
    bool is_identity(const EffectEval& e) const noexcept override {
        return !(e.color(kColor).w > 0.0f) || !(e.f(kOpacity) > 0.01f) || !(e.f(kWidth) > 0.05f);
    }
    bool demo_values(EffectInstance&, std::vector<ParamValue>& v) const noexcept override {
        v[kColor] = ParamValue::color(0.10f, 0.65f, 0.95f, 1.0f);
        v[kWidth] = ParamValue::scalar(14.0f);
        return true;
    }
    f32 input_margin(const EffectEval& e) const noexcept override {
        return std::clamp(finite_or(e.f(kWidth), 0.0f), 0.0f, 2000.0f);
    }
    void pipelines(std::vector<PipelineKey>& out, SurfaceFormat work) const override {
        out.push_back(PipelineKey::fullscreen(ShaderId::effects_border_dilate_frag, work));
        out.push_back(PipelineKey::fullscreen(ShaderId::effects_border_combine_frag, work));
    }
    Status build(EffectBuildContext& ctx, const EffectEval& e, const LayerImage& input, f32 margin,
                 LayerImage& out) const override {
        const f32 width = std::clamp(finite_or(e.f(kWidth), 8.0f), 0.1f, 2000.0f);
        const Rect region = spread_region(input.region, width, width, e.placement, margin);
        if (region.w <= 0.0f || region.h <= 0.0f) return Errc::InvalidArgument;

        u32 w = 0, h = 0;
        ctx.region_size(region, input.texel_scale_x(), w, h);

        const Vec4 toInput = EffectBuildContext::uv_map(region, input.region);
        struct {
            Vec4 uvMap; Vec4 texel; Vec4 p0; Vec4 p1; Vec4 p2; Vec4 p3; Vec4 color;
        } u{};
        u.texel = Vec4{region.w > 0.0f ? 1.0f / region.w : 0.0f, region.h > 0.0f ? 1.0f / region.h : 0.0f,
                       input.texel_scale_x(), input.texel_scale_y()};

        // 1. Dilata na horizontal. O passo é a meia-largura dividida pelas
        // amostras: a caixa cobre exatamente `width` px. A amostra é a textura
        // da entrada, então a densidade é a da região dela.
        const f32 perStep = width / 8.0f;
        u.uvMap = toInput;
        u.p0 = Vec4{perStep, 0.0f, 0.0f, 0.0f};
        u.p3 = Vec4{std::max(input.region.w, 1e-4f), std::max(input.region.h, 1e-4f), 0.0f, 0.0f};
        const FGTexture dilated = ctx.texture("borda-dilata", w, h);
        if (ctx.fullscreen_pass("borda-dilata", PassStage::Effects, dilated,
                                ShaderId::effects_border_dilate_frag,
                                {PassTexture{input.texture, {}, CommonSampler::LinearBorder}},
                                &u, sizeof(u)) == kInvalidIndex) {
            return Errc::PipelineCompileFailed;
        }

        // 2. Dilata na vertical e compõe o anel por baixo da camada. Agora a
        // amostra é a silhueta dilatada, na região do efeito.
        u.uvMap = Vec4{1.0f, 1.0f, 0.0f, 0.0f};
        u.p0 = Vec4{0.0f, perStep, 0.0f,
                    std::clamp(finite_or(e.f(kOpacity), 0.0f) / 100.0f, 0.0f, 1.0f)};
        u.p2 = toInput;
        u.p3 = Vec4{std::max(region.w, 1e-4f), std::max(region.h, 1e-4f), 0.0f, 0.0f};
        u.color = e.color(kColor);

        const FGTexture result = ctx.texture("borda", w, h);
        if (ctx.fullscreen_pass("borda", PassStage::Effects, result, ShaderId::effects_border_combine_frag,
                                {PassTexture{dilated, {}, CommonSampler::LinearClamp},
                                 PassTexture{input.texture, {}, CommonSampler::LinearClamp}},
                                &u, sizeof(u)) == kInvalidIndex) {
            return Errc::PipelineCompileFailed;
        }
        out = LayerImage{result, region, w, h};
        return OkStatus;
    }
};

} // namespace

void register_finishing_effects(EffectRegistry& r) {
    (void)r.add(std::make_unique<RadialBlur>());
    (void)r.add(std::make_unique<Mirror>());
    (void)r.add(std::make_unique<CropEdges>());
    (void)r.add(std::make_unique<Vignette>());
    (void)r.add(std::make_unique<Mosaic>());
    (void)r.add(std::make_unique<FindEdges>());
    (void)r.add(std::make_unique<HueSaturation>());
    (void)r.add(std::make_unique<DropShadow>());
    (void)r.add(std::make_unique<Border>());
}

} // namespace aurea::builtin
