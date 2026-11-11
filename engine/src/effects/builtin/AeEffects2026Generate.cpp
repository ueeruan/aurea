// =============================================================================
//  Aurea / effects / builtin / AeEffects2026Generate.cpp
//
//  Lote "AE 2026", gerar/transição/simulação/3D/áudio: Círculo, Elipse,
//  Fractal, Preencher com conta-gotas (ae_generate.frag); Limpar em grade,
//  Limpar com luz, Varredura em linhas (ae_wipe.frag); Chuva e Neve
//  (ae_weather.frag); Fosco por profundidade e Neblina 3D sobre o mapa de
//  profundidade da IA (ae_depth.frag); Graves e agudos (só áudio: o DSP mora
//  em audio/AudioFx.cpp).
//
//  Parâmetros: os do efeito equivalente do After Effects (documentação
//  pública), com rótulos em pt-BR; a ordem é contrato (só acrescentar no fim).
// =============================================================================
#include "AeEffects2026.hpp"

#include "aurea/audio/AudioEffects.hpp"

namespace aurea::builtin::ae26 {
namespace {

constexpr u16 kAnimPct = kParamAnimatable | kParamPercent;
constexpr u16 kAnimPx = kParamAnimatable | kParamPixels;
constexpr u16 kAnimRel = kParamAnimatable | kParamRelative;

/// Base dos efeitos de um shader com operação em p[11].w.
class AeOpEffect : public Effect {
public:
    void pipelines(std::vector<PipelineKey>& out, SurfaceFormat work) const override {
        out.push_back(PipelineKey::fullscreen(shader(), work));
    }
    Status build(EffectBuildContext& ctx, const EffectEval& e, const LayerImage& input, f32, LayerImage& out) const override {
        AeUniforms u = ae_uniforms(e, input);
        fill(e, input, u);
        u.p[11].w = static_cast<f32>(op());
        return ae_pass(ctx, input, u, shader(), "ae-gerar", CommonSampler::LinearClamp, out);
    }

protected:
    [[nodiscard]] virtual ShaderId shader() const noexcept { return ShaderId::effects_ae_generate_frag; }
    [[nodiscard]] virtual u32 op() const noexcept = 0;
    virtual void fill(const EffectEval& e, const LayerImage& input, AeUniforms& u) const noexcept = 0;
};

// =============================================================================
// ae_generate.frag
// =============================================================================
class Circle final : public AeOpEffect {
public:
    enum : u32 { kCenter = 0, kRadius, kEdge, kThickness, kFeatherOuter, kFeatherInner, kInvert, kColor, kOpacity, kBlend };
    const EffectInfo& info() const noexcept override {
        static const EffectInfo i{"aurea.generate.circle", "Círculo", "Gerar", EffectClass::Neighborhood};
        return i;
    }
    void declare_parameters(ParameterRegistry& p) const override {
        p.add_point2("center", "Centro", Vec2{0.5f, 0.5f}, -1.0f, 2.0f, kAnimRel);
        p.typed_range(-10.0f, 10.0f);
        p.add_float("radius", "Raio", 75.0f, 0.0f, 1000.0f, kAnimPx, "px");
        p.typed_range(0.0f, 20000.0f);
        static const char* const kEdges[] = {"Nenhuma", "Raio da borda", "Espessura", "Espessura × raio",
                                             "Espessura e difusão × raio"};
        p.add_enum("edge", "Borda", kEdges, 5, 0);
        p.add_float("thickness", "Espessura", 10.0f, 0.0f, 200.0f, kAnimPx, "px");
        p.typed_range(0.0f, 5000.0f);
        p.add_float("feather_outer", "Difusão da borda externa", 0.0f, 0.0f, 200.0f, kAnimPx, "px");
        p.typed_range(0.0f, 5000.0f);
        p.add_float("feather_inner", "Difusão da borda interna", 0.0f, 0.0f, 200.0f, kAnimPx, "px");
        p.typed_range(0.0f, 5000.0f);
        p.add_bool("invert", "Inverter círculo", false);
        p.add_color("color", "Cor", Vec4{1.0f, 1.0f, 1.0f, 1.0f});
        p.add_float("opacity", "Opacidade", 100.0f, 0.0f, 100.0f, kAnimPct, "%");
        static const char* const kModes[] = {"Nenhum", "Normal", "Somar", "Multiplicar", "Tela", "Sobrepor",
                                             "Estêncil alfa", "Silhueta alfa"};
        p.add_enum("blend", "Modo de mesclagem", kModes, 8, 1);
    }

protected:
    u32 op() const noexcept override { return 0; }
    void fill(const EffectEval& e, const LayerImage& input, AeUniforms& u) const noexcept override {
        const Vec2 c = layer_point(e, input, kCenter);
        u.p[0] = Vec4{c.x, c.y, clampf(e.f(kRadius), 0.0f, 20000.0f, 75.0f), en(e, kEdge, 5)};
        u.p[1] = Vec4{clampf(e.f(kThickness), 0.0f, 5000.0f, 10.0f), clampf(e.f(kFeatherOuter), 0.0f, 5000.0f, 0.0f),
                      clampf(e.f(kFeatherInner), 0.0f, 5000.0f, 0.0f), flag(e.b(kInvert))};
        u.p[2] = Vec4{pct(e, kOpacity), en(e, kBlend, 8), 0.0f, 0.0f};
        u.c[0] = e.color(kColor);
    }
};

class Ellipse final : public AeOpEffect {
public:
    enum : u32 { kCenter = 0, kWidth, kHeight, kThickness, kSoftness, kInside, kOutside, kComposite };
    const EffectInfo& info() const noexcept override {
        static const EffectInfo i{"aurea.generate.ellipse", "Elipse", "Gerar", EffectClass::Neighborhood};
        return i;
    }
    void declare_parameters(ParameterRegistry& p) const override {
        p.add_point2("center", "Centro", Vec2{0.5f, 0.5f}, -1.0f, 2.0f, kAnimRel);
        p.typed_range(-10.0f, 10.0f);
        p.add_float("width", "Largura", 200.0f, 0.0f, 2000.0f, kAnimPx, "px");
        p.typed_range(0.0f, 20000.0f);
        p.add_float("height", "Altura", 200.0f, 0.0f, 2000.0f, kAnimPx, "px");
        p.typed_range(0.0f, 20000.0f);
        p.add_float("thickness", "Espessura", 10.0f, 0.0f, 200.0f, kAnimPx, "px");
        p.typed_range(0.0f, 5000.0f);
        p.add_float("softness", "Suavidade", 50.0f, 0.0f, 100.0f, kAnimPct, "%");
        p.add_color("inside_color", "Cor de dentro", Vec4{1.0f, 1.0f, 1.0f, 1.0f});
        p.add_color("outside_color", "Cor de fora", Vec4{0.02f, 0.25f, 1.0f, 1.0f});
        p.add_bool("composite_on_original", "Compor sobre o original", false);
    }

protected:
    u32 op() const noexcept override { return 1; }
    void fill(const EffectEval& e, const LayerImage& input, AeUniforms& u) const noexcept override {
        const Vec2 c = layer_point(e, input, kCenter);
        u.p[0] = Vec4{c.x, c.y, clampf(e.f(kWidth), 0.0f, 20000.0f, 200.0f), clampf(e.f(kHeight), 0.0f, 20000.0f, 200.0f)};
        u.p[1] = Vec4{clampf(e.f(kThickness), 0.0f, 5000.0f, 10.0f), pct(e, kSoftness), flag(e.b(kComposite)), 0.0f};
        u.c[0] = e.color(kInside);
        u.c[1] = e.color(kOutside);
    }
};

class Fractal final : public AeOpEffect {
public:
    enum : u32 { kSet = 0, kEquation, kX, kY, kMagnification, kEscape, kJuliaX, kJuliaY, kPalette, kHue, kCycleSteps,
                 kCycleOffset, kTransparent, kMix };
    const EffectInfo& info() const noexcept override {
        static const EffectInfo i{"aurea.generate.fractal", "Fractal", "Gerar", EffectClass::Neighborhood};
        return i;
    }
    void declare_parameters(ParameterRegistry& p) const override {
        static const char* const kSets[] = {"Mandelbrot", "Julia"};
        p.add_enum("set", "Conjunto", kSets, 2, 0);
        static const char* const kEquations[] = {"z = z² + c", "z = z³ + c", "z = z⁵ + c", "z = z⁷ + c"};
        p.add_enum("equation", "Equação", kEquations, 4, 0);
        p.add_float("x_real", "X (real)", -0.75f, -2.5f, 2.5f, kParamAnimatable);
        p.typed_range(-10.0f, 10.0f);
        p.add_float("y_imaginary", "Y (imaginário)", 0.0f, -2.5f, 2.5f, kParamAnimatable);
        p.typed_range(-10.0f, 10.0f);
        p.add_float("magnification", "Ampliação", 0.0f, -2.0f, 20.0f, kParamAnimatable);
        p.add_int("escape_limit", "Limite de escape", 100, 10, 1000);
        p.add_float("julia_x", "Julia X (real)", -0.8f, -2.0f, 2.0f, kParamAnimatable);
        p.add_float("julia_y", "Julia Y (imaginário)", 0.156f, -2.0f, 2.0f, kParamAnimatable);
        static const char* const kPalettes[] = {"Arco-íris", "Tons de cinza", "Fogo", "Gelo"};
        p.add_enum("palette", "Paleta", kPalettes, 4, 0);
        p.add_angle("hue", "Matiz", 0.0f);
        p.add_int("cycle_steps", "Passos do ciclo", 32, 1, 512);
        p.add_int("cycle_offset", "Deslocamento do ciclo", 0, 0, 512);
        p.add_bool("transparent", "Interior transparente", false);
        p.add_float("mix", "Mistura", 100.0f, 0.0f, 100.0f, kAnimPct, "%");
    }
    bool is_identity(const EffectEval& e) const noexcept override { return pct(e, kMix) < 1e-4f; }

protected:
    u32 op() const noexcept override { return 2; }
    void fill(const EffectEval& e, const LayerImage&, AeUniforms& u) const noexcept override {
        static const f32 kPowers[] = {2.0f, 3.0f, 5.0f, 7.0f};
        u.p[0] = Vec4{en(e, kSet, 2), kPowers[std::min<u32>(e.e(kEquation), 3u)], clampf(e.f(kX), -10, 10, -0.75f),
                      clampf(e.f(kY), -10, 10, 0)};
        u.p[1] = Vec4{std::exp2(clampf(e.f(kMagnification), -2.0f, 20.0f, 0.0f)), clampf(e.f(kEscape), 10, 1000, 100),
                      clampf(e.f(kJuliaX), -2, 2, -0.8f), clampf(e.f(kJuliaY), -2, 2, 0.156f)};
        u.p[2] = Vec4{en(e, kPalette, 4), fin(e.f(kHue), 0.0f) / 360.0f, clampf(e.f(kCycleSteps), 1, 512, 32),
                      clampf(e.f(kCycleOffset), 0, 512, 0)};
        u.p[3] = Vec4{flag(e.b(kTransparent)), pct(e, kMix), 0.0f, 0.0f};
    }
};

class EyedropperFill final : public AeOpEffect {
public:
    enum : u32 { kPoint = 0, kRadius, kAverage, kMaintainAlpha, kBlend };
    const EffectInfo& info() const noexcept override {
        static const EffectInfo i{"aurea.generate.eyedropper_fill", "Preencher com conta-gotas", "Gerar", EffectClass::Neighborhood};
        return i;
    }
    void declare_parameters(ParameterRegistry& p) const override {
        p.add_point2("sample_point", "Ponto de amostra", Vec2{0.5f, 0.5f}, -1.0f, 2.0f, kAnimRel);
        p.typed_range(-10.0f, 10.0f);
        p.add_float("sample_radius", "Raio da amostra", 0.0f, 0.0f, 200.0f, kAnimPx, "px");
        p.typed_range(0.0f, 2000.0f);
        static const char* const kModes[] = {"Ignorar vazios", "Todos", "Todos pré-multiplicados", "Incluindo alfa"};
        p.add_enum("average", "Média das cores", kModes, 4, 0);
        p.add_bool("maintain_alpha", "Manter alfa original", true);
        p.add_float("blend", "Misturar com o original", 0.0f, 0.0f, 100.0f, kAnimPct, "%");
    }
    bool is_identity(const EffectEval& e) const noexcept override { return pct(e, kBlend) >= 0.999f; }
    bool needs_full_input() const noexcept override { return true; }

protected:
    u32 op() const noexcept override { return 3; }
    void fill(const EffectEval& e, const LayerImage& input, AeUniforms& u) const noexcept override {
        const Vec2 c = layer_point(e, input, kPoint);
        u.p[0] = Vec4{c.x, c.y, clampf(e.f(kRadius), 0.0f, 2000.0f, 0.0f), en(e, kAverage, 4)};
        u.p[1] = Vec4{flag(e.b(kMaintainAlpha)), pct(e, kBlend), 0.0f, 0.0f};
    }
};

// =============================================================================
// ae_wipe.frag — transições (Transição)
// =============================================================================
class AeWipeEffect : public AeOpEffect {
protected:
    ShaderId shader() const noexcept override { return ShaderId::effects_ae_wipe_frag; }
};

class GridWipe final : public AeWipeEffect {
public:
    enum : u32 { kCompletion = 0, kCenter, kRotation, kBorder, kTiles, kShape, kReverse };
    const EffectInfo& info() const noexcept override {
        static const EffectInfo i{"aurea.transition.grid_wipe", "Limpar em grade", "Transição", EffectClass::Neighborhood};
        return i;
    }
    void declare_parameters(ParameterRegistry& p) const override {
        p.add_float("completion", "Conclusão", 0.0f, 0.0f, 100.0f, kAnimPct, "%");
        p.add_point2("center", "Centro", Vec2{0.5f, 0.5f}, -1.0f, 2.0f, kAnimRel);
        p.typed_range(-10.0f, 10.0f);
        p.add_angle("rotation", "Rotação", 0.0f);
        p.add_float("border", "Borda", 0.0f, 0.0f, 100.0f, kAnimPx, "px");
        p.add_float("tiles", "Ladrilhos", 10.0f, 1.0f, 100.0f, kParamAnimatable);
        static const char* const kShapes[] = {"Portas", "Radial", "Retangular"};
        p.add_enum("shape", "Forma", kShapes, 3, 0);
        p.add_bool("reverse", "Inverter transição", false);
    }
    bool is_identity(const EffectEval& e) const noexcept override { return pct(e, kCompletion) < 1e-4f; }
    bool demo_values(EffectInstance&, std::vector<ParamValue>& v) const noexcept override {
        v[kCompletion] = ParamValue::scalar(50.0f);
        return true;
    }

protected:
    u32 op() const noexcept override { return 0; }
    void fill(const EffectEval& e, const LayerImage& input, AeUniforms& u) const noexcept override {
        const Vec2 c = layer_point(e, input, kCenter);
        u.p[0] = Vec4{pct(e, kCompletion), c.x, c.y, fin(e.f(kRotation), 0.0f) * kDeg2Rad};
        u.p[1] = Vec4{clampf(e.f(kBorder), 0.0f, 1000.0f, 0.0f), clampf(e.f(kTiles), 1.0f, 100.0f, 10.0f), en(e, kShape, 3),
                      flag(e.b(kReverse))};
    }
};

class LightWipe final : public AeWipeEffect {
public:
    enum : u32 { kCompletion = 0, kCenter, kIntensity, kShape, kDirection, kFromSource, kColor, kReverse };
    const EffectInfo& info() const noexcept override {
        static const EffectInfo i{"aurea.transition.light_wipe", "Limpar com luz", "Transição", EffectClass::Neighborhood};
        return i;
    }
    void declare_parameters(ParameterRegistry& p) const override {
        p.add_float("completion", "Conclusão", 0.0f, 0.0f, 100.0f, kAnimPct, "%");
        p.add_point2("center", "Centro", Vec2{0.5f, 0.5f}, -1.0f, 2.0f, kAnimRel);
        p.typed_range(-10.0f, 10.0f);
        p.add_float("intensity", "Intensidade", 100.0f, 0.0f, 400.0f, kParamAnimatable);
        static const char* const kShapes[] = {"Portas", "Redondo", "Quadrado"};
        p.add_enum("shape", "Forma", kShapes, 3, 1);
        p.add_angle("direction", "Direção", 0.0f);
        p.add_bool("color_from_source", "Cor da origem", false);
        p.add_color("color", "Cor", Vec4{1.0f, 1.0f, 1.0f, 1.0f});
        p.add_bool("reverse", "Inverter transição", false);
    }
    bool is_identity(const EffectEval& e) const noexcept override { return pct(e, kCompletion) < 1e-4f; }
    bool demo_values(EffectInstance&, std::vector<ParamValue>& v) const noexcept override {
        v[kCompletion] = ParamValue::scalar(40.0f);
        return true;
    }

protected:
    u32 op() const noexcept override { return 1; }
    void fill(const EffectEval& e, const LayerImage& input, AeUniforms& u) const noexcept override {
        const Vec2 c = layer_point(e, input, kCenter);
        u.p[0] = Vec4{pct(e, kCompletion), c.x, c.y, fin(e.f(kDirection), 0.0f) * kDeg2Rad};
        u.p[1] = Vec4{clampf(e.f(kIntensity), 0.0f, 400.0f, 100.0f) / 100.0f, en(e, kShape, 3), flag(e.b(kFromSource)),
                      flag(e.b(kReverse))};
        u.c[0] = e.color(kColor);
    }
};

class LineSweep final : public AeWipeEffect {
public:
    enum : u32 { kCompletion = 0, kDirection, kThickness, kSlant, kFlip };
    const EffectInfo& info() const noexcept override {
        static const EffectInfo i{"aurea.transition.line_sweep", "Varredura em linhas", "Transição", EffectClass::Neighborhood};
        return i;
    }
    void declare_parameters(ParameterRegistry& p) const override {
        p.add_float("completion", "Conclusão", 0.0f, 0.0f, 100.0f, kAnimPct, "%");
        p.add_angle("direction", "Direção", 45.0f);
        p.add_float("thickness", "Espessura", 20.0f, 1.0f, 200.0f, kAnimPx, "px");
        p.typed_range(0.5f, 5000.0f);
        p.add_float("slant", "Inclinação", 50.0f, 0.0f, 500.0f, kAnimPx, "px");
        p.typed_range(0.0f, 10000.0f);
        p.add_bool("flip_direction", "Inverter direção", false);
    }
    bool is_identity(const EffectEval& e) const noexcept override { return pct(e, kCompletion) < 1e-4f; }
    bool demo_values(EffectInstance&, std::vector<ParamValue>& v) const noexcept override {
        v[kCompletion] = ParamValue::scalar(50.0f);
        return true;
    }

protected:
    u32 op() const noexcept override { return 2; }
    void fill(const EffectEval& e, const LayerImage&, AeUniforms& u) const noexcept override {
        u.p[0] = Vec4{pct(e, kCompletion), fin(e.f(kDirection), 45.0f) * kDeg2Rad, clampf(e.f(kThickness), 0.5f, 5000.0f, 20.0f),
                      clampf(e.f(kSlant), 0.0f, 10000.0f, 50.0f)};
        u.p[1] = Vec4{flag(e.b(kFlip)), 0.0f, 0.0f, 0.0f};
    }
};

// =============================================================================
// ae_weather.frag — Chuva (0) e Neve (1): campos procedurais pela semente e
// pelo tempo (o mesmo quadro dá a mesma imagem no preview e no export).
// =============================================================================
class AeWeatherEffect : public AeOpEffect {
protected:
    ShaderId shader() const noexcept override { return ShaderId::effects_ae_weather_frag; }
};

class Rain final : public AeWeatherEffect {
public:
    enum : u32 { kAmount = 0, kSpeed, kAngle, kVariation, kDropSize, kDepth, kOpacity };
    const EffectInfo& info() const noexcept override {
        static const EffectInfo i{"aurea.generate.rain", "Chuva", "Gerar", EffectClass::Neighborhood};
        return i;
    }
    void declare_parameters(ParameterRegistry& p) const override {
        p.add_float("amount", "Quantidade de gotas", 500.0f, 0.0f, 2000.0f, kParamAnimatable);
        p.add_float("speed", "Velocidade", 1.0f, 0.0f, 5.0f, kParamAnimatable);
        p.add_angle("angle", "Ângulo", 10.0f);
        p.add_float("angle_variation", "Variação do ângulo", 5.0f, 0.0f, 45.0f, kParamAnimatable, "°");
        p.add_float("drop_size", "Tamanho da gota", 1.0f, 0.1f, 5.0f, kParamAnimatable);
        p.add_float("source_depth", "Profundidade da origem", 50.0f, 0.0f, 100.0f, kAnimPct, "%");
        p.add_float("opacity", "Opacidade", 60.0f, 0.0f, 100.0f, kAnimPct, "%");
    }
    bool is_identity(const EffectEval& e) const noexcept override {
        return !(fin(e.f(kAmount), 0.0f) > 0.5f) || pct(e, kOpacity) < 1e-4f;
    }

protected:
    u32 op() const noexcept override { return 0; }
    void fill(const EffectEval& e, const LayerImage&, AeUniforms& u) const noexcept override {
        u.p[0] = Vec4{clampf(e.f(kAmount), 0.0f, 2000.0f, 500.0f), clampf(e.f(kSpeed), 0.0f, 5.0f, 1.0f),
                      fin(e.f(kAngle), 10.0f) * kDeg2Rad, clampf(e.f(kVariation), 0.0f, 45.0f, 5.0f) * kDeg2Rad};
        u.p[1] = Vec4{clampf(e.f(kDropSize), 0.1f, 5.0f, 1.0f), pct(e, kDepth), pct(e, kOpacity), 0.0f};
    }
};

class Snow final : public AeWeatherEffect {
public:
    enum : u32 { kAmount = 0, kSpeed, kAmplitude, kFrequency, kFlakeSize, kDepth, kOpacity };
    const EffectInfo& info() const noexcept override {
        static const EffectInfo i{"aurea.generate.snow", "Neve", "Gerar", EffectClass::Neighborhood};
        return i;
    }
    void declare_parameters(ParameterRegistry& p) const override {
        p.add_float("amount", "Quantidade de flocos", 500.0f, 0.0f, 2000.0f, kParamAnimatable);
        p.add_float("speed", "Velocidade", 1.0f, 0.0f, 5.0f, kParamAnimatable);
        p.add_float("amplitude", "Amplitude", 1.0f, 0.0f, 5.0f, kParamAnimatable);
        p.add_float("frequency", "Frequência", 1.0f, 0.0f, 5.0f, kParamAnimatable);
        p.add_float("flake_size", "Tamanho do floco", 3.0f, 0.5f, 20.0f, kAnimPx, "px");
        p.add_float("source_depth", "Profundidade da origem", 50.0f, 0.0f, 100.0f, kAnimPct, "%");
        p.add_float("opacity", "Opacidade", 100.0f, 0.0f, 100.0f, kAnimPct, "%");
    }
    bool is_identity(const EffectEval& e) const noexcept override {
        return !(fin(e.f(kAmount), 0.0f) > 0.5f) || pct(e, kOpacity) < 1e-4f;
    }

protected:
    u32 op() const noexcept override { return 1; }
    void fill(const EffectEval& e, const LayerImage&, AeUniforms& u) const noexcept override {
        u.p[0] = Vec4{clampf(e.f(kAmount), 0.0f, 2000.0f, 500.0f), clampf(e.f(kSpeed), 0.0f, 5.0f, 1.0f),
                      clampf(e.f(kAmplitude), 0.0f, 5.0f, 1.0f), clampf(e.f(kFrequency), 0.0f, 5.0f, 1.0f)};
        u.p[1] = Vec4{clampf(e.f(kFlakeSize), 0.5f, 20.0f, 3.0f), pct(e, kDepth), pct(e, kOpacity), 0.0f};
    }
};

// =============================================================================
// ae_depth.frag — sobre o mapa de profundidade da IA (como DepthEffects.cpp)
// =============================================================================
class AeDepthEffect : public Effect {
public:
    void pipelines(std::vector<PipelineKey>& out, SurfaceFormat work) const override {
        out.push_back(PipelineKey::fullscreen(ShaderId::effects_ae_depth_frag, work));
    }
    void resolve_resources(EffectEval& e) const noexcept override {
        e.aux = TextureHandle{};
        e.auxInfo = Vec4{0.0f, 1.0f, 0.0f, 0.0f};
        if (!e.resources) return;
        DepthMapRequest req;
        req.host = e.layer;
        req.instance = e.instance;
        req.localTime = e.localTime;
        req.smoothing = pct(e, smoothing_param());
        const DepthMapResult r = e.resources->depth_map(req);
        if (!r.texture.valid()) return;
        e.aux = r.texture;
        e.auxInfo = Vec4{r.lo, r.hi, 1.0f, 0.0f};
        e.auxMapX = r.mapX;
        e.auxMapY = r.mapY;
    }
    Status build(EffectBuildContext& ctx, const EffectEval& e, const LayerImage& input, f32, LayerImage& out) const override {
        // Sem mapa (camada sem foto/vídeo, rede indisponível, ainda calculando): a camada como está.
        if (!e.aux.valid()) { out = input; return OkStatus; }
        AeUniforms u = ae_uniforms(e, input, false);   // o mapa cobre a caixa NATURAL da fonte
        fill(e, u);
        u.p[8] = Vec4{1.0f, 0.0f, e.auxInfo.x, e.auxInfo.y};
        if (e.auxMapX.w > 0.0f && e.auxMapY.w > 0.0f) {
            // Camada de ajuste: o mapa é da foto/vídeo abaixo (o mesmo contrato do Mapa de profundidade).
            u.p[8].y = 1.0f;
            u.p[9] = Vec4{e.auxMapX.x, e.auxMapX.y, e.auxMapY.x, e.auxMapY.y};
            u.p[10] = Vec4{e.auxMapX.w, e.auxMapY.w, e.auxMapX.z, e.auxMapY.z};
        }
        u.p[11].w = static_cast<f32>(op());
        const PassTexture depth{{}, e.aux, CommonSampler::LinearClamp};
        return ae_pass(ctx, input, u, ShaderId::effects_ae_depth_frag, "ae-profundidade", CommonSampler::LinearClamp, out, &depth, 1);
    }

protected:
    [[nodiscard]] virtual u32 op() const noexcept = 0;
    [[nodiscard]] virtual u32 smoothing_param() const noexcept = 0;
    virtual void fill(const EffectEval& e, AeUniforms& u) const noexcept = 0;
};

class DepthMatte final : public AeDepthEffect {
public:
    enum : u32 { kDepth = 0, kFeather, kInvert, kSmoothing };
    const EffectInfo& info() const noexcept override {
        static const EffectInfo i{"aurea.ai.depth_matte", "Fosco por profundidade (IA)", "3D", EffectClass::Neighborhood};
        return i;
    }
    void declare_parameters(ParameterRegistry& p) const override {
        p.add_float("depth", "Profundidade", 50.0f, 0.0f, 100.0f, kAnimPct, "%");
        p.add_float("feather", "Difusão", 10.0f, 0.0f, 100.0f, kAnimPct, "%");
        p.add_bool("invert", "Inverter", false);
        p.add_float("smoothing", "Suavização", 70.0f, 0.0f, 100.0f, kAnimPct, "%");
    }

protected:
    u32 op() const noexcept override { return 0; }
    u32 smoothing_param() const noexcept override { return kSmoothing; }
    void fill(const EffectEval& e, AeUniforms& u) const noexcept override {
        u.p[0] = Vec4{pct(e, kDepth), pct(e, kFeather), flag(e.b(kInvert)), 0.0f};
    }
};

class Fog3D final : public AeDepthEffect {
public:
    enum : u32 { kColor = 0, kStart, kEnd, kOpacity, kDensity, kFoggyBackground, kSmoothing };
    const EffectInfo& info() const noexcept override {
        static const EffectInfo i{"aurea.ai.fog_3d", "Neblina 3D (IA)", "3D", EffectClass::Neighborhood};
        return i;
    }
    void declare_parameters(ParameterRegistry& p) const override {
        p.add_color("fog_color", "Cor da neblina", Vec4{0.75f, 0.8f, 0.86f, 1.0f});
        p.add_float("start_depth", "Profundidade inicial", 0.0f, 0.0f, 100.0f, kAnimPct, "%");
        p.add_float("end_depth", "Profundidade final", 100.0f, 0.0f, 100.0f, kAnimPct, "%");
        p.add_float("opacity", "Opacidade da neblina", 100.0f, 0.0f, 100.0f, kAnimPct, "%");
        p.add_float("scattering_density", "Densidade de dispersão", 50.0f, 0.0f, 100.0f, kAnimPct, "%");
        p.add_bool("foggy_background", "Fundo com neblina", true);
        p.add_float("smoothing", "Suavização", 70.0f, 0.0f, 100.0f, kAnimPct, "%");
    }
    bool is_identity(const EffectEval& e) const noexcept override { return pct(e, kOpacity) < 1e-4f; }

protected:
    u32 op() const noexcept override { return 1; }
    u32 smoothing_param() const noexcept override { return kSmoothing; }
    void fill(const EffectEval& e, AeUniforms& u) const noexcept override {
        u.p[0] = Vec4{pct(e, kStart), pct(e, kEnd), pct(e, kOpacity), pct(e, kDensity)};
        u.p[1] = Vec4{flag(e.b(kFoggyBackground)), 0.0f, 0.0f, 0.0f};
        u.c[0] = e.color(kColor);
    }
};

// =============================================================================
// Graves e agudos — só áudio (no vídeo é identidade; o DSP lê os índices de
// audio::fxp::BassTreble).
// =============================================================================
class BassTreble final : public Effect {
public:
    const EffectInfo& info() const noexcept override {
        static const EffectInfo i{audio::fx_keys::kBassTreble, "Graves e agudos", "Áudio", EffectClass::PerPixel};
        return i;
    }
    void declare_parameters(ParameterRegistry& p) const override {
        p.add_float("bass", "Graves", 0.0f, -24.0f, 24.0f, kParamAnimatable, "dB");
        p.add_float("treble", "Agudos", 0.0f, -24.0f, 24.0f, kParamAnimatable, "dB");
    }
    bool is_identity(const EffectEval&) const noexcept override { return true; }
};

} // namespace

void register_generate(EffectRegistry& r) {
    (void)r.add(std::make_unique<Circle>());
    (void)r.add(std::make_unique<Ellipse>());
    (void)r.add(std::make_unique<Fractal>());
    (void)r.add(std::make_unique<EyedropperFill>());
    (void)r.add(std::make_unique<GridWipe>());
    (void)r.add(std::make_unique<LightWipe>());
    (void)r.add(std::make_unique<LineSweep>());
    (void)r.add(std::make_unique<Rain>());
    (void)r.add(std::make_unique<Snow>());
    (void)r.add(std::make_unique<DepthMatte>());
    (void)r.add(std::make_unique<Fog3D>());
    (void)r.add(std::make_unique<BassTreble>());
}

} // namespace aurea::builtin::ae26

namespace aurea::builtin {
void register_ae_effects_2026(EffectRegistry& r) {
    ae26::register_color(r);
    ae26::register_stylize(r);
    ae26::register_generate(r);
    // Segunda leva (AeEffects2026Extra.cpp): sempre no FIM.
    ae26::register_extra(r);
}
} // namespace aurea::builtin
