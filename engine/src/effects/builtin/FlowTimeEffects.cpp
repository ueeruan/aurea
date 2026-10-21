// =============================================================================
//  Tempo por optical flow (0.1.8): Flow Retime e Flow Motion Blur.
//
//  Pedido do dono: "Twixtor otimizado e muito bom" e "RSMB". Implementação
//  própria; a referência de comportamento são os plugins de mesmo propósito
//  (câmera lenta por vetores de movimento e borrão de movimento calculado da
//  filmagem). A busca acha "twixtor" e "rsmb" pelas palavras-chave.
//
//  Os dois são DECLARATIVOS (como o Remapear tempo): não desenham na cadeia
//  de pixels. O renderer lê os parâmetros onde escolhe os quadros da fonte
//  (`video_flow_fx` em Renderer.cpp) e usa o MESMO optical flow do Movimento
//  suave — nos dois sentidos, com a borda de oclusão tratada. Vale na prévia
//  e no export; o som da camada não muda (como nos plugins).
//
//  Os índices de parâmetro ficam fixos nos projetos salvos.
// =============================================================================
#include "BuiltinEffects.hpp"

namespace aurea::builtin {
namespace {

constexpr u16 kAnimPct = kParamAnimatable | kParamPercent;

// Câmera lenta (ou rápida) com quadros inventados pelo movimento de pixels.
// A velocidade anda DENTRO da camada desde a entrada (a barra não muda de
// tamanho): 50% mostra metade da fonte no mesmo tempo, com os quadros que
// faltam criados pelo optical flow.
class FlowRetime final : public Effect {
public:
    const EffectInfo& info() const noexcept override {
        static const EffectInfo i{effect_keys::kFlowRetime, "Flow Retime", "Tempo", EffectClass::Temporal};
        return i;
    }
    void declare_parameters(ParameterRegistry& p) const override {
        static const char* const frames[] = {"Mais próximo", "Mistura", "Movimento"};
        static const char* const quality[] = {"Rápida", "Boa", "Melhor"};
        p.add_float("speed", "Velocidade", 50.0f, 5.0f, 400.0f, kAnimPct, "%");
        p.typed_range(1.0f, 1000.0f);
        p.add_enum("frames", "Quadros", frames, 3, 2);
        p.add_enum("vectors", "Qualidade dos vetores", quality, 3, 1);
        p.add_float("motion_blur", "Desfoque de movimento", 0.0f, 0.0f, 200.0f, kAnimPct, "%");
        p.typed_range(0.0f, 400.0f);
        // 0.3.0 — o "substituir quadros duplicados" dos interpoladores de
        // vídeo: o quadro da fonte que repete o anterior (30 dentro de 60,
        // quadro perdido/congelado) é refeito pelo optical flow entre os
        // quadros DISTINTOS em volta. A detecção compara assinaturas de luma
        // 32×18 (uma por quadro da fonte, guardada) no renderer.
        const u32 dup = p.add_bool("replace_duplicates", "Substituir quadros duplicados", false, kParamNone);
        const u32 sens = p.add_float("duplicate_sensitivity", "Sensibilidade", 10.0f, 0.0f, 100.0f, kParamPercent, "%");
        p.set_tip(sens, "Mais alta: quadros só parecidos também contam como repetidos.");
        p.add_section("duplicates", "Substituir quadros duplicados", {sens}, dup, true);
    }
    bool is_identity(const EffectEval&) const noexcept override { return true; }
};

// Borrão de movimento calculado da própria filmagem: cada pixel borra ao
// longo do seu vetor de movimento, com o obturador escolhido.
class FlowMotionBlur final : public Effect {
public:
    const EffectInfo& info() const noexcept override {
        static const EffectInfo i{effect_keys::kFlowMotionBlur, "Flow Motion Blur", "Tempo", EffectClass::Temporal};
        return i;
    }
    void declare_parameters(ParameterRegistry& p) const override {
        static const char* const quality[] = {"Rápida", "Boa", "Melhor"};
        p.add_float("amount", "Quantidade", 100.0f, 0.0f, 200.0f, kAnimPct, "%");
        p.typed_range(0.0f, 400.0f);
        p.add_float("shutter", "Obturador", 180.0f, 0.0f, 360.0f, kParamAnimatable, "°");
        p.typed_range(0.0f, 720.0f);
        p.add_enum("vectors", "Qualidade dos vetores", quality, 3, 1);
    }
    bool is_identity(const EffectEval&) const noexcept override { return true; }
};

} // namespace

void register_flow_time_effects(EffectRegistry& r) {
    (void)r.add(std::make_unique<FlowRetime>());
    (void)r.add(std::make_unique<FlowMotionBlur>());
}

} // namespace aurea::builtin
