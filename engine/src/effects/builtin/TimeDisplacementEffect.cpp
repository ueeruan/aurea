// =============================================================================
//  Deslocamento no tempo (0.4.5, pedido de usuários: o Time Displacement do AE).
//
//  Cada pixel da camada vem de um INSTANTE diferente, escolhido pelo brilho de
//  um mapa: branco = o deslocamento máximo para a frente, preto = para trás,
//  cinza 50% = agora (a regra pública do AE: deslocamento = máx · 2 · (L − ½)).
//  O mapa é outra camada (no lugar dela na composição), a própria camada no
//  quadro de agora, ou um degradê pronto (horizontal, vertical, radial).
//
//  DECLARATIVO como o TimeSlice (TemporalEffects.cpp): não desenha na cadeia de
//  pixels. O renderer lê os parâmetros, pede a camada em cada NÍVEL de tempo
//  (de −máx a +máx no passo da "Resolução do tempo", no máximo 31 níveis) e o
//  shader composite/time_displace.frag dá a cada pixel o peso do nível dele.
//  Só vídeo e pré-composição mudam no tempo; numa foto ele é neutro.
//
//  Os índices de parâmetro ficam fixos nos projetos salvos (acrescentar só no
//  fim). O renderer lê os seis primeiros por índice.
// =============================================================================
#include "BuiltinEffects.hpp"

namespace aurea::builtin {
namespace {

class TimeDisplacement final : public Effect {
public:
    enum : u32 { kMap = 0, kMapLayer, kMaxTime, kResolution, kInvert, kSmooth };

    const EffectInfo& info() const noexcept override {
        static const EffectInfo i{"aurea.time.time_displacement", "Time Displacement", "Tempo", EffectClass::Temporal};
        return i;
    }
    void declare_parameters(ParameterRegistry& p) const override {
        static const char* const kMaps[] = {"Camada do mapa", "A própria camada", "Degradê horizontal",
                                            "Degradê vertical", "Degradê radial"};
        p.add_enum("map", "Mapa", kMaps, 5, 2);
        p.add_layer_ref("map_layer", "Camada do mapa");
        p.add_float("max_time", "Deslocamento máximo", 1.0f, 0.0f, 5.0f, kParamAnimatable, "s");
        p.typed_range(0.0f, 60.0f);
        // Quantos instantes por segundo de deslocamento: mais = degraus menores
        // e mais quadros da fonte por pixel desenhado (teto de 31 níveis).
        p.add_float("time_resolution", "Resolução do tempo", 15.0f, 1.0f, 60.0f, kParamNone, "fps");
        p.add_bool("invert", "Inverter o mapa", false);
        p.add_bool("smooth", "Suavizar entre os níveis", true);
        p.set_tip(kMapLayer, "Branco = adiantado, preto = atrasado, cinza 50% = agora.");
    }
    bool is_identity(const EffectEval&) const noexcept override { return true; }
    /// A camada do mapa entra no quadro mesmo escondida (só como entrada),
    /// como o mapa do Mapa de deslocamento (Renderer::prepare).
    i32 input_layer_param() const noexcept override { return static_cast<i32>(kMapLayer); }
};

} // namespace

void register_time_displacement_effect(EffectRegistry& r) {
    (void)r.add(std::make_unique<TimeDisplacement>());
}

} // namespace aurea::builtin
