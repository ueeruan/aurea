// =============================================================================
//  Efeitos de cor — todos POR PIXEL, todos fundíveis.
//
//  Nenhum deles escreve passe: cada um só descreve a sua operação (`ColorOp`)
//  e o EffectGraph junta as operações consecutivas num único passe do
//  `color_stack.frag`. A matemática de cada operação está no shader; aqui fica
//  a tradução dos parâmetros da interface para os números do shader.
// =============================================================================
#include "BuiltinEffects.hpp"

#include <cmath>

namespace aurea::builtin {
namespace {

bool near(f32 a, f32 b) noexcept { return std::fabs(a - b) < 1e-5f; }

// -----------------------------------------------------------------------------
// Exposure — em stops, no linear (é o único ajuste fisicamente "de luz").
// -----------------------------------------------------------------------------
class Exposure final : public Effect {
public:
    enum : u32 { kExposure = 0, kOffset, kGamma };

    const EffectInfo& info() const noexcept override {
        static const EffectInfo i{effect_keys::kExposure, "Exposição", "Cor", EffectClass::PerPixel};
        return i;
    }
    void declare_parameters(ParameterRegistry& p) const override {
        p.add_float("exposure", "Exposição", 0.0f, -10.0f, 10.0f, kParamAnimatable, "stops");
        p.add_float("offset", "Deslocamento", 0.0f, -0.5f, 0.5f);
        p.add_float("gamma", "Correção de gama", 1.0f, 0.1f, 10.0f);
    }
    bool is_identity(const EffectEval& e) const noexcept override {
        return near(e.f(kExposure), 0.0f) && near(e.f(kOffset), 0.0f) && near(e.f(kGamma), 1.0f);
    }
    bool color_op(const EffectEval& e, ColorOp& op) const noexcept override {
        op.code = ColorOpCode::Exposure;
        op.p[1] = e.f(kExposure);
        op.p[2] = e.f(kOffset);
        op.p[3] = e.f(kGamma) > 0.01f ? e.f(kGamma) : 0.01f;
        return true;
    }
};

// -----------------------------------------------------------------------------
// Brilho e contraste — no valor codificado (é como o olho julga).
// -----------------------------------------------------------------------------
class BrightnessContrast final : public Effect {
public:
    enum : u32 { kBrightness = 0, kContrast };

    const EffectInfo& info() const noexcept override {
        static const EffectInfo i{effect_keys::kBrightnessContrast, "Brilho e contraste", "Cor",
                                  EffectClass::PerPixel};
        return i;
    }
    void declare_parameters(ParameterRegistry& p) const override {
        p.add_float("brightness", "Brilho", 0.0f, -150.0f, 150.0f);
        p.add_float("contrast", "Contraste", 0.0f, -100.0f, 100.0f);
    }
    bool is_identity(const EffectEval& e) const noexcept override {
        return near(e.f(kBrightness), 0.0f) && near(e.f(kContrast), 0.0f);
    }
    /// Fator de contraste: -100 achata tudo no cinza médio, 0 não mexe, +100
    /// multiplica a distância ao cinza por 4. Linear dos dois lados do zero
    /// para o controle responder igual em toda a faixa.
    static f32 contrast_factor(f32 c) noexcept {
        return c >= 0.0f ? 1.0f + c / 100.0f * 3.0f : 1.0f + c / 100.0f;
    }
    bool color_op(const EffectEval& e, ColorOp& op) const noexcept override {
        op.code = ColorOpCode::BrightnessContrast;
        op.p[1] = e.f(kBrightness) / 255.0f;   // mesma escala de 0..255 da interface
        op.p[2] = contrast_factor(e.f(kContrast));
        return true;
    }
};

// -----------------------------------------------------------------------------
// Saturação — mistura com a luminância BT.709, em linear.
// -----------------------------------------------------------------------------
class Saturation final : public Effect {
public:
    enum : u32 { kSaturation = 0 };

    const EffectInfo& info() const noexcept override {
        static const EffectInfo i{effect_keys::kSaturation, "Saturação", "Cor", EffectClass::PerPixel};
        return i;
    }
    void declare_parameters(ParameterRegistry& p) const override {
        p.add_float("saturation", "Saturação", 0.0f, -100.0f, 100.0f);
    }
    bool is_identity(const EffectEval& e) const noexcept override {
        return near(e.f(kSaturation), 0.0f);
    }
    bool color_op(const EffectEval& e, ColorOp& op) const noexcept override {
        op.code = ColorOpCode::Saturation;
        op.p[1] = 1.0f + e.f(kSaturation) / 100.0f;
        return true;
    }
};

// -----------------------------------------------------------------------------
// Tint — o preto vira uma cor, o branco outra, pela luminância.
// -----------------------------------------------------------------------------
class Tint final : public Effect {
public:
    enum : u32 { kBlack = 0, kWhite, kAmount };

    const EffectInfo& info() const noexcept override {
        static const EffectInfo i{effect_keys::kTint, "Tingir", "Cor", EffectClass::PerPixel};
        return i;
    }
    void declare_parameters(ParameterRegistry& p) const override {
        p.add_color("map_black", "Mapear preto para", Vec4{0, 0, 0, 1});
        p.add_color("map_white", "Mapear branco para", Vec4{1, 1, 1, 1});
        p.add_float("amount", "Intensidade", 100.0f, 0.0f, 100.0f, kParamAnimatable | kParamPercent, "%");
    }
    bool is_identity(const EffectEval& e) const noexcept override {
        return near(e.f(kAmount), 0.0f);
    }
    bool color_op(const EffectEval& e, ColorOp& op) const noexcept override {
        const Vec4 b = e.color(kBlack);
        const Vec4 w = e.color(kWhite);
        op.code = ColorOpCode::Tint;
        op.p[1] = b.x; op.p[2] = b.y; op.p[3] = b.z;
        op.p[4] = w.x; op.p[5] = w.y; op.p[6] = w.z;
        op.p[7] = e.f(kAmount) / 100.0f;
        return true;
    }
};

// -----------------------------------------------------------------------------
// Matriz de cor 3x4 (RGB + deslocamento), em linear.
// -----------------------------------------------------------------------------
class ColorMatrix final : public Effect {
public:
    const EffectInfo& info() const noexcept override {
        static const EffectInfo i{effect_keys::kColorMatrix, "Matriz de cor", "Cor", EffectClass::PerPixel};
        return i;
    }
    void declare_parameters(ParameterRegistry& p) const override {
        static const char* const kIds[12] = {"rr", "rg", "rb", "ro", "gr", "gg", "gb", "go",
                                             "br", "bg", "bb", "bo"};
        static const char* const kLabels[12] = {
            "Vermelho ← R", "Vermelho ← G", "Vermelho ← B", "Vermelho +",
            "Verde ← R", "Verde ← G", "Verde ← B", "Verde +",
            "Azul ← R", "Azul ← G", "Azul ← B", "Azul +"};
        for (u32 i = 0; i < 12; ++i) {
            const bool diag = (i == 0 || i == 5 || i == 10);
            const bool offset = (i % 4 == 3);
            p.add_float(kIds[i], kLabels[i], diag ? 1.0f : 0.0f,
                        offset ? -1.0f : -4.0f, offset ? 1.0f : 4.0f);
        }
    }
    bool is_identity(const EffectEval& e) const noexcept override {
        for (u32 i = 0; i < 12; ++i) {
            const f32 want = (i == 0 || i == 5 || i == 10) ? 1.0f : 0.0f;
            if (!near(e.f(i), want)) return false;
        }
        return true;
    }
    bool color_op(const EffectEval& e, ColorOp& op) const noexcept override {
        // Layout do shader: [a.yzw b.x] [b.yzw d.x] [d.yzw e4.x] =
        // p[1..3] p[4] | p[5..7] p[8] | p[9..11] p[12].
        op.code = ColorOpCode::ColorMatrix;
        op.p[1] = e.f(0);  op.p[2] = e.f(1);  op.p[3] = e.f(2);  op.p[4] = e.f(3);
        op.p[5] = e.f(4);  op.p[6] = e.f(5);  op.p[7] = e.f(6);  op.p[8] = e.f(7);
        op.p[9] = e.f(8);  op.p[10] = e.f(9); op.p[11] = e.f(10); op.p[12] = e.f(11);
        return true;
    }
};

// -----------------------------------------------------------------------------
// Níveis — entrada preto/branco, gama, saída preto/branco (codificado).
// -----------------------------------------------------------------------------
class Levels final : public Effect {
public:
    enum : u32 { kInBlack = 0, kInWhite, kGamma, kOutBlack, kOutWhite };

    const EffectInfo& info() const noexcept override {
        static const EffectInfo i{effect_keys::kLevels, "Níveis", "Cor", EffectClass::PerPixel};
        return i;
    }
    void declare_parameters(ParameterRegistry& p) const override {
        p.add_float("input_black", "Preto de entrada", 0.0f, 0.0f, 255.0f);
        p.add_float("input_white", "Branco de entrada", 255.0f, 0.0f, 255.0f);
        p.add_float("gamma", "Gama", 1.0f, 0.1f, 10.0f);
        p.add_float("output_black", "Preto de saída", 0.0f, 0.0f, 255.0f);
        p.add_float("output_white", "Branco de saída", 255.0f, 0.0f, 255.0f);
    }
    bool is_identity(const EffectEval& e) const noexcept override {
        return near(e.f(kInBlack), 0.0f) && near(e.f(kInWhite), 255.0f) && near(e.f(kGamma), 1.0f)
            && near(e.f(kOutBlack), 0.0f) && near(e.f(kOutWhite), 255.0f);
    }
    bool color_op(const EffectEval& e, ColorOp& op) const noexcept override {
        op.code = ColorOpCode::Levels;
        op.p[1] = e.f(kInBlack) / 255.0f;
        op.p[2] = e.f(kInWhite) / 255.0f;
        op.p[3] = e.f(kGamma);
        op.p[4] = e.f(kOutBlack) / 255.0f;
        op.p[5] = e.f(kOutWhite) / 255.0f;
        return true;
    }
};

// -----------------------------------------------------------------------------
// Curvas — fundação: mestra + R, G, B, avaliadas por spline monotônica e
// amostradas numa LUT de 256 entradas que só é refeita quando a curva muda.
// -----------------------------------------------------------------------------
class Curves final : public Effect {
public:
    enum : u32 { kCurve = 0 };

    const EffectInfo& info() const noexcept override {
        static const EffectInfo i{effect_keys::kCurves, "Curvas", "Cor", EffectClass::PerPixel};
        return i;
    }
    void declare_parameters(ParameterRegistry& p) const override {
        p.add_curve("curve", "Curva");
    }
    bool is_identity(const EffectEval& e) const noexcept override {
        const CurveData* c = e.curve(kCurve);
        return !c || c->is_identity();
    }
    bool color_op(const EffectEval& e, ColorOp& op) const noexcept override {
        const CurveData* c = e.curve(kCurve);
        if (!c || !e.resources) return false;
        op.code = ColorOpCode::Curves;
        op.lut = e.resources->curve_lut(*c);
        return op.lut.valid();
    }
};

} // namespace

void register_color_effects(EffectRegistry& r) {
    (void)r.add(std::make_unique<Exposure>());
    (void)r.add(std::make_unique<BrightnessContrast>());
    (void)r.add(std::make_unique<Saturation>());
    (void)r.add(std::make_unique<Tint>());
    (void)r.add(std::make_unique<ColorMatrix>());
    (void)r.add(std::make_unique<Levels>());
    (void)r.add(std::make_unique<Curves>());
}

} // namespace aurea::builtin
