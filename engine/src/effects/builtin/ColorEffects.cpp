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
    enum : u32 { kExposure = 0, kOffset, kGamma, kSpace };

    const EffectInfo& info() const noexcept override {
        static const EffectInfo i{effect_keys::kExposure, "Exposição", "Cor", EffectClass::PerPixel};
        return i;
    }
    void declare_parameters(ParameterRegistry& p) const override {
        p.add_float("exposure", "Exposição", 0.0f, -10.0f, 10.0f, kParamAnimatable, "stops");
        // Digitado até ±14 stops: 2^14 x o branco ainda é finito na textura de
        // trabalho em meia precisão (teto 65504); acima disso o branco vira inf.
        p.typed_range(-14.0f, 14.0f);
        p.add_float("offset", "Deslocamento", 0.0f, -0.5f, 0.5f);
        p.typed_range(-2.0f, 2.0f);
        // O mínimo 0.1 fica: gama menor eleva luz HDR a potências que estouram.
        p.add_float("gamma", "Correção de gama", 1.0f, 0.1f, 10.0f);
        p.typed_range(0.1f, 100.0f);
        // Onde a conta roda. "Valor codificado" é o Gamma e exposição do app
        // antigo: sobre o valor que se vê (0..1), com o resultado preso em 0..1.
        // No fim da lista: projetos antigos abrem com o padrão, luz linear.
        static const char* const kSpaces[] = {"Luz linear", "Valor codificado"};
        p.add_enum("space", "Aplicar em", kSpaces, 2, 0);
    }
    bool demo_values(EffectInstance&, std::vector<ParamValue>& v) const noexcept override {
        v[kExposure] = ParamValue::scalar(1.0f);      // um ponto de luz já se vê
        return true;
    }
    bool is_identity(const EffectEval& e) const noexcept override {
        return near(e.f(kExposure), 0.0f) && near(e.f(kOffset), 0.0f) && near(e.f(kGamma), 1.0f);
    }
    bool color_op(const EffectEval& e, ColorOp& op) const noexcept override {
        op.code = ColorOpCode::Exposure;
        op.p[1] = e.f(kExposure);
        op.p[2] = e.f(kOffset);
        op.p[3] = e.f(kGamma) > 0.01f ? e.f(kGamma) : 0.01f;
        op.p[4] = e.e(kSpace) == 1u ? 1.0f : 0.0f;
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
        p.typed_range(-1000.0f, 1000.0f);
        // Abaixo de -100 o fator fica negativo: o contraste INVERTE em torno do
        // cinza médio (-200 = negativo inteiro). Custo fixo, só aritmética.
        p.add_float("contrast", "Contraste", 0.0f, -100.0f, 100.0f);
        p.typed_range(-300.0f, 1000.0f);
    }
    bool demo_values(EffectInstance&, std::vector<ParamValue>& v) const noexcept override {
        v[kBrightness] = ParamValue::scalar(25.0f);
        v[kContrast] = ParamValue::scalar(40.0f);
        return true;
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
        // Abaixo de -100 as cores viram as complementares; acima, supersaturam.
        p.typed_range(-300.0f, 1000.0f);
    }
    bool demo_values(EffectInstance&, std::vector<ParamValue>& v) const noexcept override {
        v[kSaturation] = ParamValue::scalar(80.0f);
        return true;
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
    bool demo_values(EffectInstance&, std::vector<ParamValue>& v) const noexcept override {
        // Sombras no azul profundo, luzes no âmbar: o duotone clássico.
        v[0] = ParamValue::color(0.02f, 0.10f, 0.30f, 1.0f);
        v[1] = ParamValue::color(1.00f, 0.72f, 0.32f, 1.0f);
        return true;
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
    bool demo_values(EffectInstance&, std::vector<ParamValue>& v) const noexcept override {
        // Roda os canais: vermelho vem do verde, verde do azul, azul do
        // vermelho. Uma matriz identidade não mostraria nada.
        const f32 m[12] = {0, 1, 0, 0,  0, 0, 1, 0,  1, 0, 0, 0};
        for (u32 i = 0; i < 12; ++i) v[i] = ParamValue::scalar(m[i]);
        return true;
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
    bool demo_values(EffectInstance&, std::vector<ParamValue>& v) const noexcept override {
        v[0] = ParamValue::scalar(32.0f);    // preto de entrada
        v[1] = ParamValue::scalar(214.0f);   // branco de entrada
        v[2] = ParamValue::scalar(1.4f);     // gama
        return true;
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
    bool demo_values(EffectInstance& inst, std::vector<ParamValue>& v) const noexcept override {
        // Uma S suave na mestra: escurece as sombras, clareia as luzes e
        // deixa o cinza médio no lugar. É a curva que todo mundo reconhece.
        if (inst.curves.empty()) return false;
        inst.curves[0].channel[0] = {{0.00f, 0.00f}, {0.25f, 0.16f}, {0.50f, 0.50f}, {0.75f, 0.84f}, {1.00f, 1.00f}};
        v[kCurve].ref = 0;
        return true;
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

// -----------------------------------------------------------------------------
// Preencher — tinta a camada inteira com uma cor chapada. A opacidade é o
// quanto a cor cobre a imagem: 0 não faz nada, 100 troca tudo pela cor. O alfa
// da camada não muda — quem era transparente continua transparente.
// -----------------------------------------------------------------------------
class Fill final : public Effect {
public:
    enum : u32 { kColor = 0, kOpacity };

    const EffectInfo& info() const noexcept override {
        static const EffectInfo i{effect_keys::kFill, "Preencher", "Cor", EffectClass::PerPixel};
        return i;
    }
    void declare_parameters(ParameterRegistry& p) const override {
        p.add_color("color", "Cor", Vec4{1.0f, 0.85f, 0.20f, 1.0f});
        p.add_float("opacity", "Opacidade", 100.0f, 0.0f, 100.0f, kParamAnimatable | kParamPercent, "%");
    }
    bool is_identity(const EffectEval& e) const noexcept override {
        // Alfa 0 na cor = "sem tinta": o efeito não desenha, mesmo com
        // opacidade em 100%. É o que deixa a cor guardar o próprio "ligado".
        return e.f(kOpacity) <= 0.0f || e.color(kColor).w <= 0.0f;
    }
    bool demo_values(EffectInstance&, std::vector<ParamValue>& v) const noexcept override {
        v[kColor] = ParamValue::color(0.95f, 0.35f, 0.10f, 1.0f);
        return true;
    }
    bool color_op(const EffectEval& e, ColorOp& op) const noexcept override {
        const Vec4 c = e.color(kColor);
        op.code = ColorOpCode::Fill;
        op.p[1] = c.x;
        op.p[2] = c.y;
        op.p[3] = c.z;
        op.p[4] = std::clamp(e.f(kOpacity) * 0.01f, 0.0f, 1.0f) * std::clamp(c.w, 0.0f, 1.0f);
        return true;
    }
};

// -----------------------------------------------------------------------------
// Equilíbrio de cor (HLS) — matiz, luz e saturação num só controle, como o
// painel do editor antigo: girar o matiz, clarear até o branco ou escurecer
// até o preto, e saturar sem lavar a luz.
// -----------------------------------------------------------------------------
class ColorBalanceHls final : public Effect {
public:
    enum : u32 { kHue = 0, kLightness, kSaturation };

    const EffectInfo& info() const noexcept override {
        static const EffectInfo i{effect_keys::kColorBalanceHls, "Equilíbrio de cor (HLS)", "Cor",
                                  EffectClass::PerPixel};
        return i;
    }
    void declare_parameters(ParameterRegistry& p) const override {
        // O matiz em graus (-180..180) é o que a pessoa vê; o shader recebe
        // voltas (-0.5..0.5), que é como o HSL conta.
        p.add_angle("hue", "Matiz", 0.0f, -180.0f, 180.0f);
        p.add_float("lightness", "Luminosidade", 0.0f, -100.0f, 100.0f,
                    kParamAnimatable | kParamPercent, "%");
        p.add_float("saturation", "Saturação", 0.0f, -100.0f, 100.0f,
                    kParamAnimatable | kParamPercent, "%");
    }
    bool is_identity(const EffectEval& e) const noexcept override {
        return std::fabs(e.f(kHue)) < 1e-4f && std::fabs(e.f(kLightness)) < 1e-4f &&
               std::fabs(e.f(kSaturation)) < 1e-4f;
    }
    bool demo_values(EffectInstance&, std::vector<ParamValue>& v) const noexcept override {
        v[kHue] = ParamValue::scalar(-40.0f);      // esfria o quadro para o azul
        v[kSaturation] = ParamValue::scalar(35.0f);
        return true;
    }
    bool color_op(const EffectEval& e, ColorOp& op) const noexcept override {
        op.code = ColorOpCode::BalanceHls;
        op.p[1] = e.f(kHue) / 360.0f;
        op.p[2] = std::clamp(e.f(kLightness) * 0.01f, -1.0f, 1.0f);
        op.p[3] = std::clamp(e.f(kSaturation) * 0.01f, -1.0f, 1.0f);
        return true;
    }
};

class ImportedCubeLut final : public Effect {
public:
    const EffectInfo& info() const noexcept override {
        static const EffectInfo info{effect_keys::kCubeLut, "LUT (.cube)", "Cor", EffectClass::Neighborhood};
        return info;
    }
    void declare_parameters(ParameterRegistry& p) const override {
        p.add_texture_ref("file", "Arquivo .cube");
        p.add_float("mix", "Intensidade", 100, 0, 100, kParamAnimatable | kParamPercent, "%");
        p.add_float("size", "Size", 2, 2, 65536, kParamHidden);
        p.add_float("dimensions", "Dimensions", 3, 1, 3, kParamHidden);
        ParamSpec domain;
        domain.type = ParamType::Point3D; domain.flags = kParamHidden;
        domain.minValue = -65504; domain.maxValue = 65504;
        domain.id = "domain_min"; domain.label = "Domain min"; domain.defaultValue = ParamValue::vec3(0,0,0); p.add(domain);
        domain.id = "domain_max"; domain.label = "Domain max"; domain.defaultValue = ParamValue::vec3(1,1,1); p.add(domain);
    }
    bool is_identity(const EffectEval& e) const noexcept override { return e.f(1) <= 0 || !e.value(0).ref; }
    void resolve_resources(EffectEval& e) const noexcept override {
        if (e.resources && e.value(0).ref) e.aux = e.resources->cube_lut(AssetId::unpack(e.value(0).ref));
    }
    void pipelines(std::vector<PipelineKey>& out, SurfaceFormat work) const override {
        out.push_back(PipelineKey::fullscreen(ShaderId::effects_cube_lut_frag, work));
    }
    Status build(EffectBuildContext& ctx, const EffectEval& e, const LayerImage& input, f32, LayerImage& out) const override {
        if (!e.aux.valid()) return Status{Errc::MediaSourceMissing, "arquivo LUT indisponivel"};
        auto u = base_uniforms(input);
        u.p0 = {e.f(1) * .01f, e.f(2), e.f(3), 0};
        const Vec3 lo = e.value(4).as_vec3(), hi = e.value(5).as_vec3();
        u.p1 = {lo.x, lo.y, lo.z, 0}; u.p2 = {hi.x, hi.y, hi.z, 0};
        out = input; out.texture = ctx.texture("LUT", input.width, input.height);
        if (ctx.fullscreen_pass("LUT", PassStage::Effects, out.texture, ShaderId::effects_cube_lut_frag,
            {PassTexture{input.texture}, PassTexture{{}, e.aux, CommonSampler::LinearClamp}}, &u, sizeof(u)) == kInvalidIndex)
            return Errc::PipelineCompileFailed;
        return OkStatus;
    }
};

} // namespace

void register_color_effects(EffectRegistry& r) {
    (void)r.add(std::make_unique<ImportedCubeLut>());
    (void)r.add(std::make_unique<Exposure>());
    (void)r.add(std::make_unique<BrightnessContrast>());
    (void)r.add(std::make_unique<Saturation>());
    (void)r.add(std::make_unique<Tint>());
    (void)r.add(std::make_unique<ColorMatrix>());
    (void)r.add(std::make_unique<Levels>());
    (void)r.add(std::make_unique<Curves>());
    // Preencher e Equilíbrio de cor: os dois por pixel entram na MESMA pilha
    // fundida dos outros de cor — sem passe a mais.
    (void)r.add(std::make_unique<Fill>());
    (void)r.add(std::make_unique<ColorBalanceHls>());
}

} // namespace aurea::builtin
