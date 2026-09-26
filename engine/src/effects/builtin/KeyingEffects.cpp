// =============================================================================
//  Keying — Chave de luma e Chave de croma.
//
//  Os dois são POR PIXEL: entram no passe de cor fundido (`color_stack.frag`)
//  como operações que mexem no ALFA. Uma chave seguida de correção de cor sai
//  numa leitura e numa escrita.
//
//  Chave de croma: distância no plano de crominância (Cb, Cr do BT.709 sobre o
//  valor codificado) entre o pixel e a cor-chave. Abaixo da tolerância o pixel
//  some; na faixa de suavidade o alfa sobe em curva suave. A luma não entra na
//  distância — sombra e luz no fundo verde continuam "verde". Supressão de
//  derramamento: tira do pixel a componente de crominância na direção da
//  chave (o verde que respingou no cabelo/pele), mantendo a luma.
//
//  Chave de luma: o alfa pela luminância codificada, com limiar e suavidade;
//  tira os escuros (padrão) ou os claros.
// =============================================================================
#include "BuiltinEffects.hpp"

#include <algorithm>
#include <cmath>

namespace aurea::builtin {
namespace {

f32 encode(f32 c) noexcept {
    c = std::max(0.0f, c);
    return c <= 0.0031308f ? c * 12.92f : 1.055f * std::pow(c, 1.0f / 2.4f) - 0.055f;
}

class LumaKey final : public Effect {
public:
    enum : u32 { kType = 0, kThreshold, kSoftness };

    const EffectInfo& info() const noexcept override {
        static const EffectInfo i{effect_keys::kLumaKey, "Chave de luma", "Recorte", EffectClass::PerPixel};
        return i;
    }
    void declare_parameters(ParameterRegistry& p) const override {
        static const char* const kTypes[2] = {"Tirar escuros", "Tirar claros"};
        p.add_enum("key_type", "Tipo", kTypes, 2, 0);
        p.add_float("threshold", "Limiar", 20.0f, 0.0f, 100.0f, kParamAnimatable | kParamPercent, "%");
        p.add_float("softness", "Suavidade", 10.0f, 0.0f, 100.0f, kParamAnimatable | kParamPercent, "%");
    }
    bool demo_values(EffectInstance&, std::vector<ParamValue>& v) const noexcept override {
        v[0] = ParamValue::scalar(0.0f);      // tirar os escuros
        v[1] = ParamValue::scalar(34.0f);     // limiar
        v[2] = ParamValue::scalar(14.0f);     // suavidade
        return true;
    }
    bool color_op(const EffectEval& e, ColorOp& op) const noexcept override {
        op.code = ColorOpCode::LumaKey;
        op.p[1] = std::clamp(e.f(kThreshold), 0.0f, 100.0f) / 100.0f;
        op.p[2] = std::clamp(e.f(kSoftness), 0.0f, 100.0f) / 100.0f;
        op.p[3] = e.e(kType) == 1 ? 1.0f : 0.0f;
        return true;
    }
};

class ChromaKey final : public Effect {
public:
    enum : u32 { kColor = 0, kTolerance, kSoftness, kSpill, kClipBlack, kClipWhite, kGamma, kView, kSpillBalance };

    const EffectInfo& info() const noexcept override {
        static const EffectInfo i{effect_keys::kChromaKey, "Chave de croma", "Recorte", EffectClass::PerPixel};
        return i;
    }
    void declare_parameters(ParameterRegistry& p) const override {
        // Verde de fundo típico (sRGB 0,10 / 0,72 / 0,22), guardado em linear
        // como todo parâmetro de cor.
        p.add_color("key_color", "Cor-chave", Vec4{0.0100f, 0.4793f, 0.0395f, 1.0f});
        p.add_float("tolerance", "Tolerância", 25.0f, 0.0f, 100.0f, kParamAnimatable | kParamPercent, "%");
        p.add_float("softness", "Suavidade", 10.0f, 0.0f, 100.0f, kParamAnimatable | kParamPercent, "%");
        p.add_float("spill", "Supressão de derramamento", 50.0f, 0.0f, 100.0f, kParamAnimatable | kParamPercent, "%");
        p.add_float("clip_black", "Limpar fundo", 0.f, 0.f, 100.f, kParamAnimatable | kParamPercent, "%");
        p.add_float("clip_white", "Preencher primeiro plano", 100.f, 0.f, 100.f, kParamAnimatable | kParamPercent, "%");
        p.add_float("matte_gamma", "Detalhes de cabelo e borda", 1.f, .1f, 4.f);
        static const char* const views[] = {"Composição", "Máscara", "Supressão de cor"};
        p.add_enum("view", "Visualização", views, 3, 0);
        p.add_float("spill_balance", "Proteger cores do primeiro plano", 0.f, 0.f, 100.f, kParamAnimatable | kParamPercent, "%");
    }
    bool color_op(const EffectEval& e, ColorOp& op) const noexcept override {
        const Vec4 k = e.color(kColor);
        op.code = ColorOpCode::ChromaKey;
        op.p[1] = encode(k.x);
        op.p[2] = encode(k.y);
        op.p[3] = encode(k.z);
        // 100% de tolerância = meia unidade de crominância (verde puro fica a
        // ~0,45 do cinza): cobre tudo o que é "da cor", nada do oposto.
        op.p[4] = std::clamp(e.f(kTolerance), 0.0f, 100.0f) / 100.0f * 0.5f;
        op.p[5] = std::clamp(e.f(kSoftness), 0.0f, 100.0f) / 100.0f * 0.5f;
        op.p[6] = std::clamp(e.f(kSpill), 0.0f, 100.0f) / 100.0f;
        op.p[7] = std::clamp(e.f(kClipBlack), 0.f, 100.f) / 100.f;
        op.p[8] = std::max(op.p[7] + .0001f, std::clamp(e.f(kClipWhite), 0.f, 100.f) / 100.f);
        op.p[9] = std::clamp(e.f(kGamma), .1f, 4.f);
        op.p[10] = static_cast<f32>(e.e(kView));
        op.p[11] = std::clamp(e.f(kSpillBalance), 0.f, 100.f) / 100.f;
        return true;
    }
};

} // namespace

void register_keying_effects(EffectRegistry& r) {
    (void)r.add(std::make_unique<LumaKey>());
    (void)r.add(std::make_unique<ChromaKey>());
}

} // namespace aurea::builtin
