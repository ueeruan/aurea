// =============================================================================
//  Efeitos temporais declarativos (Fase 7.3 §25, §26, §61).
//
//  Posterizar tempo · RGB no tempo
//
//  Os dois são DECLARATIVOS, como o Eco: eles não desenham nada na cadeia de
//  pixels (`is_identity` devolve true). Quem age é o RENDERER, que lê os
//  parâmetros no instante e monta o que precisa — no caso do RGB no tempo,
//  três amostras da camada em instantes diferentes; no do Posterizar, o tempo
//  local quantizado.
//
//  Isso é de propósito. O Temporal Engine já resolve "quero este quadro, nesta
//  layer, naquele instante" com o cache de quadros, o desfoque de movimento e
//  o eco; fazer um segundo caminho aqui dentro seria duplicar o difícil.
//
//  A consequência para quem usa: os parâmetros destes dois aceitam keyframe
//  como qualquer outro, e a prévia do catálogo cai na cartela genérica — um
//  quadro solto não representa um efeito que vive entre quadros.
// =============================================================================
#include "BuiltinEffects.hpp"

namespace aurea::builtin {
namespace {

// -----------------------------------------------------------------------------
// Posterizar tempo
// -----------------------------------------------------------------------------
class PosterizeTime final : public Effect {
public:
    enum : u32 { kFrameRate = 0, kMix };

    const EffectInfo& info() const noexcept override {
        static const EffectInfo i{effect_keys::kPosterizeTime, "Posterizar tempo", "Tempo", EffectClass::Temporal};
        return i;
    }
    void declare_parameters(ParameterRegistry& p) const override {
        p.add_float("frame_rate", "Quadros por segundo", 8.0f, 0.1f, 60.0f, kParamAnimatable, "fps");
        // "Segurar o quadro" (por padrão) mostra o quadro da taxa menor até o
        // próximo; desligado, mostra o quadro da taxa menor mais PRÓXIMO, que
        // pula alguns quadros em vez de segurá-los. Não existe "mistura" aqui
        // porque misturar duas taxas exigiria desenhar os dois quadros e
        // cruzar — e o resultado disso não é posterizar tempo, é outra coisa.
        p.add_bool("hold", "Segurar o quadro", true);
    }
    /// Ele não desenha: quem age é o renderer, que quantiza o tempo da camada.
    bool is_identity(const EffectEval&) const noexcept override { return true; }

    /// O instante local quantizado para `fps` quadros por segundo. O renderer
    /// chama isto no lugar do tempo normal da camada — é o efeito inteiro.
    [[nodiscard]] static i64 quantize(i64 localFrame, f32 layerFps, f32 targetFps, bool hold) noexcept {
        if (!(targetFps > 0.01f) || !(layerFps > 0.01f)) return localFrame;
        const f64 step = static_cast<f64>(layerFps) / static_cast<f64>(targetFps);
        if (step <= 1.0) return localFrame;   // já é mais lento que o pedido
        const f64 v = static_cast<f64>(localFrame) / step;
        return static_cast<i64>((hold ? std::floor(v) : std::round(v)) * step);
    }
};

// -----------------------------------------------------------------------------
// RGB no tempo
// -----------------------------------------------------------------------------
class TimeWarpRgb final : public Effect {
public:
    enum : u32 { kRedOffset = 0, kGreenOffset, kBlueOffset, kUnits, kBlend, kClamp };

    const EffectInfo& info() const noexcept override {
        static const EffectInfo i{effect_keys::kTimeWarpRgb, "RGB no tempo", "Tempo", EffectClass::Temporal};
        return i;
    }
    void declare_parameters(ParameterRegistry& p) const override {
        static const char* const kUnits[] = {"Quadros", "Segundos"};
        p.add_float("red_offset", "Deslocamento do vermelho", 3.0f, -120.0f, 120.0f, kParamAnimatable);
        p.add_float("green_offset", "Deslocamento do verde", 0.0f, -120.0f, 120.0f, kParamAnimatable);
        p.add_float("blue_offset", "Deslocamento do azul", -3.0f, -120.0f, 120.0f, kParamAnimatable);
        p.add_enum("units", "Unidade", kUnits, 2, 0);
        p.add_float("blend", "Intensidade", 100.0f, 0.0f, 100.0f, kParamAnimatable | kParamPercent, "%");
        p.add_bool("clamp", "Prender nas pontas", true);
    }
    /// Não desenha: o renderer monta as três amostras e o desfoque temporal as
    /// mistura com a máscara de canal (o mesmo caminho do RGB do Eco).
    bool is_identity(const EffectEval&) const noexcept override { return true; }
};

} // namespace

// -----------------------------------------------------------------------------
// Remapear tempo
//
// O MESMO remapeamento do painel de velocidade (a curva `Layer::timeRemap`),
// com o nome e a cara do After Effects: aparece no navegador de efeitos, ao
// lado do Posterizar tempo, com as linhas dele.
//
// O parâmetro "Tempo" **É** a curva — não uma cópia. Ler devolve o quadro da
// fonte que toca no cabeçote; gravar grava na chave daquele instante (criando-a
// se não houver). Assim o efeito aceita keyframe como qualquer outro, o gráfico
// do painel de velocidade edita o mesmo dado, e não existe um segundo
// remapeamento para dessincronizar do primeiro.
//
// Ele não desenha na cadeia de pixels (`is_identity`): quem age é o Temporal
// Engine, que já resolve "qual quadro da fonte toca neste instante" com o cache
// de quadros e o desfoque de movimento. Fazer um segundo caminho aqui dentro
// seria duplicar o difícil.
//
// NÃO tem "Manter o tom do áudio": o motor não faz time-stretch (o áudio
// acompanha a velocidade por reamostragem, o tom sobe junto). Um interruptor
// que não faz nada seria pior que a linha que falta.
// -----------------------------------------------------------------------------
class TimeRemapEffect final : public Effect {
public:
    enum : u32 { kTime = 0, kInterpolation };

    const EffectInfo& info() const noexcept override {
        static const EffectInfo i{effect_keys::kTimeRemap, "Remapear tempo", "Tempo", EffectClass::Temporal};
        return i;
    }
    void declare_parameters(ParameterRegistry& p) const override {
        // Os três modos do After Effects, na ordem dele.
        static const char* const kInterp[] = {"Linear", "Suave", "Segurar"};
        // O teto é o da fonte inteira (o motor prende no último quadro dela).
        p.add_float("time", "Tempo", 0.0f, 0.0f, 86400.0f, kParamAnimatable, "s");
        p.add_enum("interpolation", "Interpolação do tempo", kInterp, 3, 0);
    }
    bool is_identity(const EffectEval&) const noexcept override { return true; }

    /// O modo do AE (0 Linear, 1 Suave, 2 Segurar) na interpolação do motor.
    [[nodiscard]] static Interpolation interp_of(u32 mode) noexcept {
        return mode == 2 ? Interpolation::Hold : Interpolation::Linear;
    }
    [[nodiscard]] static u32 mode_of(Interpolation i) noexcept {
        return i == Interpolation::Hold ? 2u : 0u;
    }
};

void register_temporal_effects(EffectRegistry& r) {
    (void)r.add(std::make_unique<PosterizeTime>());
    (void)r.add(std::make_unique<TimeWarpRgb>());
    (void)r.add(std::make_unique<TimeRemapEffect>());
}


} // namespace aurea::builtin
