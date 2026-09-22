// =============================================================================
//  Controles de expressão — Slider, Ângulo, Caixa de seleção, Cor e Ponto.
//
//  São efeitos de verdade (entram na pilha, têm keyframes, salvam, desfazem,
//  aparecem no painel de efeitos) que NÃO DESENHAM NADA: `is_identity` é
//  sempre verdadeiro, então o EffectGraph os tira da cadeia e nenhum passe
//  existe. Servem de "botão" para expressões:
//
//      effect("Slider Control")("Slider")        // nome do After Effects
//      effect("Controle deslizante")(1)          // nome da interface + índice
//      thisComp.layer("Controle").effect("Slider Control")("Slider")
// =============================================================================
#include "BuiltinEffects.hpp"

namespace aurea::builtin {
namespace {

class ControlEffect final : public Effect {
public:
    enum class Kind : u8 { Slider, Angle, Checkbox, Color, Point };

    explicit ControlEffect(Kind k) : kind_(k) {}

    const EffectInfo& info() const noexcept override {
        static const EffectInfo kInfo[] = {
            {"aurea.control.slider", "Controle deslizante", "Controles de expressão", EffectClass::PerPixel},
            {"aurea.control.angle", "Controle de ângulo", "Controles de expressão", EffectClass::PerPixel},
            {"aurea.control.checkbox", "Caixa de seleção", "Controles de expressão", EffectClass::PerPixel},
            {"aurea.control.color", "Controle de cor", "Controles de expressão", EffectClass::PerPixel},
            {"aurea.control.point", "Controle de ponto", "Controles de expressão", EffectClass::PerPixel},
        };
        return kInfo[static_cast<u32>(kind_)];
    }

    void declare_parameters(ParameterRegistry& p) const override {
        // O id do parâmetro é o nome do After Effects ("Slider", "Angle"...):
        // `effect(...)("Slider")` acha pelo id, sem tabela de apelidos.
        switch (kind_) {
            case Kind::Slider: p.add_float("slider", "Valor", 0.0f, -100000.0f, 100000.0f); break;
            case Kind::Angle: p.add_angle("angle", "Ângulo", 0.0f); break;
            case Kind::Checkbox: {
                ParamSpec s;
                s.id = "checkbox";
                s.label = "Ligado";
                s.type = ParamType::Bool;
                s.flags = kParamAnimatable;   // caixa animável (keyframe de "segurar")
                s.defaultValue = ParamValue::boolean(false);
                s.minValue = 0.0f;
                s.maxValue = 1.0f;
                (void)p.add(s);
                break;
            }
            case Kind::Color: p.add_color("color", "Cor", Vec4{1.0f, 0.0f, 0.0f, 1.0f}); break;
            case Kind::Point: {
                ParamSpec s;
                s.id = "point";
                s.label = "Ponto";
                s.type = ParamType::Point2D;
                s.flags = kParamAnimatable | kParamPixels;
                s.defaultValue = ParamValue::vec2(0.0f, 0.0f);
                s.minValue = -100000.0f;
                s.maxValue = 100000.0f;
                s.unit = "px";
                (void)p.add(s);
                break;
            }
        }
    }

    bool is_identity(const EffectEval&) const noexcept override { return true; }

private:
    Kind kind_;
};

} // namespace

void register_expression_controls(EffectRegistry& r) {
    using K = ControlEffect::Kind;
    for (K k : {K::Slider, K::Angle, K::Checkbox, K::Color, K::Point}) (void)r.add(std::make_unique<ControlEffect>(k));
}

} // namespace aurea::builtin
