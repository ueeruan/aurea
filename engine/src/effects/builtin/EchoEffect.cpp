// =============================================================================
//  Eco e rastro — como EFEITO (entra no navegador de efeitos, tem keyframes,
//  salva, desfaz, vai em preset).
//
//  O efeito não desenha na cadeia de pixels (`is_identity`): eco é tempo, não
//  cor. O renderer lê os parâmetros dele no instante e monta as amostras
//  passadas da camada (o mesmo caminho do eco antigo, que morava na camada):
//  cópias atrasadas que vão sumindo e o atraso RGB (canais em instantes
//  diferentes).
// =============================================================================
#include "BuiltinEffects.hpp"

namespace aurea::builtin {
namespace {

class EchoEffect final : public Effect {
public:
    const EffectInfo& info() const noexcept override {
        static const EffectInfo kInfo{effect_keys::kEchoTrail, "Eco e rastro", "Tempo", EffectClass::Temporal};
        return kInfo;
    }

    void declare_parameters(ParameterRegistry& p) const override {
        p.add_int("copies", "Cópias", 4, 0, 16);
        p.add_float("delay", "Intervalo (quadros)", 2.0f, 0.25f, 120.0f);
        p.add_float("decay", "Decaimento", 0.6f, 0.0f, 1.0f);
        p.add_float("rgb", "Atraso RGB (quadros)", 0.0f, 0.0f, 60.0f);
    }

    bool is_identity(const EffectEval&) const noexcept override { return true; }
};

} // namespace

void register_echo_effect(EffectRegistry& r) { (void)r.add(std::make_unique<EchoEffect>()); }

} // namespace aurea::builtin
