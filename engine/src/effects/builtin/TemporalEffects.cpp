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

#include <algorithm>
#include <cmath>

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
// Low FPS: o Posterizar com cara de stop-motion. O RENDERER trava o tempo da
// camada (o mesmo gancho do Posterizar, com o ritmo irregular); aqui só o
// TREMIDO: um deslocamento e um giro sorteados por quadro segurado, como a
// câmera na mão de uma animação quadro a quadro.
// -----------------------------------------------------------------------------
f32 low_fps_hash(i64 k, f32 seed) noexcept {
    const f64 x = std::sin(static_cast<f64>(k) * 12.9898 + static_cast<f64>(seed) * 78.233) * 43758.5453;
    return static_cast<f32>(x - std::floor(x));
}
class LowFps final : public Effect {
public:
    enum : u32 { kFrameRate = 0, kJitter, kJitterRotation, kIrregular, kSeed };
    const EffectInfo& info() const noexcept override {
        static const EffectInfo i{effect_keys::kLowFps, "Low FPS", "Tempo", EffectClass::Neighborhood};
        return i;
    }
    void declare_parameters(ParameterRegistry& p) const override {
        p.add_float("frame_rate", "Quadros por segundo", 12.0f, 1.0f, 60.0f, kParamAnimatable, "fps");
        p.add_float("jitter", "Tremido", 3.0f, 0.0f, 200.0f, kParamAnimatable | kParamPixels, "px");
        p.add_float("jitter_rotation", "Tremido do giro", 0.6f, 0.0f, 45.0f, kParamAnimatable, "°");
        p.add_float("irregular", "Ritmo irregular", 25.0f, 0.0f, 100.0f, kParamAnimatable | kParamPercent, "%");
        p.add_float("seed", "Semente", 0.0f, 0.0f, 9999.0f, kParamAnimatable);
    }
    bool is_identity(const EffectEval& e) const noexcept override {
        return e.f(kJitter) <= 0.001f && e.f(kJitterRotation) <= 0.001f;
    }
    f32 input_margin(const EffectEval& e) const noexcept override { return std::max(0.f, e.f(kJitter)) + 2.f; }
    void pipelines(std::vector<PipelineKey>& out, SurfaceFormat work) const override {
        out.push_back(PipelineKey::fullscreen(ShaderId::effects_affine_resample_frag, work));
    }
    /// O quadro segurado em que o instante (já travado pelo renderer) está.
    static i64 held_index(f64 localFrame, f64 compFps, f32 targetFps) noexcept {
        const f64 step = compFps / std::max<f64>(targetFps, 0.01);
        return step > 1.0 ? static_cast<i64>(std::floor(localFrame / step + 1e-6)) : static_cast<i64>(std::floor(localFrame));
    }
    Status build(EffectBuildContext& ctx, const EffectEval& e, const LayerImage& input, f32 margin, LayerImage& out) const override {
        const f64 fps = e.framesPerSecond > 0 ? e.framesPerSecond : 30.0;
        const i64 k = held_index(e.time_frames(), fps, e.f(kFrameRate));
        const f32 seed = e.f(kSeed);
        const f32 jitter = std::max(0.f, e.f(kJitter));
        const Vec2 t{(low_fps_hash(k, seed) * 2 - 1) * jitter, (low_fps_hash(k + 7919, seed) * 2 - 1) * jitter};
        const f32 angle = (low_fps_hash(k + 104729, seed) * 2 - 1) * e.f(kJitterRotation) * kDeg2Rad;
        const f32 w = e.placement && e.placement->layerWidth ? static_cast<f32>(e.placement->layerWidth) : input.region.w;
        const f32 h = e.placement && e.placement->layerHeight ? static_cast<f32>(e.placement->layerHeight) : input.region.h;
        const Vec2 c{w * .5f, h * .5f};
        const Rect o = spread_region(input.region, jitter + 2, jitter + 2, e.placement, margin);
        u32 ow = 0, oh = 0;
        ctx.region_size(o, input.texel_scale_x(), ow, oh);
        if (!ow || !oh) { out = input; return OkStatus; }
        // uv de entrada = Rinv · (px da saída − centro − tremido) + centro, em uv da entrada.
        const f32 cs = std::cos(angle), sn = std::sin(angle);
        const Rect& i = input.region;
        const f32 bx = o.x - c.x - t.x, by = o.y - c.y - t.y;
        struct { Vec4 row0, row1, params; } u{};
        u.row0 = {cs * o.w / i.w, sn * o.h / i.w, (cs * bx + sn * by + c.x - i.x) / i.w, 0};
        u.row1 = {-sn * o.w / i.h, cs * o.h / i.h, (-sn * bx + cs * by + c.y - i.y) / i.h, 0};
        u.params = {1, 0, 0, 0};
        out = LayerImage{ctx.texture("low-fps", ow, oh), o, ow, oh};
        if (ctx.fullscreen_pass("low-fps", PassStage::Effects, out.texture, ShaderId::effects_affine_resample_frag,
                {PassTexture{input.texture, {}, CommonSampler::LinearBorder}}, &u, sizeof(u)) == kInvalidIndex)
            return Errc::PipelineCompileFailed;
        return OkStatus;
    }
};

// -----------------------------------------------------------------------------
// RGB no tempo
// -----------------------------------------------------------------------------
class TimeWarpRgb final : public Effect {
public:
    enum : u32 { kRedOffset = 0, kGreenOffset, kBlueOffset, kUnits, kBlend, kClamp };

    void pipelines(std::vector<PipelineKey>& out, SurfaceFormat work) const override {
        out.push_back(PipelineKey::fullscreen(ShaderId::effects_rgb_merge_frag, work));
    }

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

// -----------------------------------------------------------------------------
// TimeSlice (fatias no tempo)
//
// A imagem vira N faixas paralelas; cada faixa mostra a camada num instante
// diferente. A faixa i (0..N−1, na direção escolhida) fica em
//     t + deslocamento + (−N/2 + i·N/(N−1))
// quadros — com 12 faixas, de t−6 a t+6, como a referência descreve. Sem
// interpolar, o instante é arredondado para o quadro mais próximo; ligado, os
// dois quadros vizinhos são misturados pelo peso da fração.
//
// Como o RGB no tempo, ele é DECLARATIVO: quem busca os instantes e desenha
// cada faixa é o renderer (o mesmo acúmulo temporal do Eco, com o recorte da
// faixa no shader composite/time_slice.frag).
// -----------------------------------------------------------------------------
class TimeSlice final : public Effect {
public:
    enum : u32 { kDirection = 0, kSlices, kOffset, kInterpolate };
    static constexpr u32 kMaxSlices = 32;

    const EffectInfo& info() const noexcept override {
        static const EffectInfo i{effect_keys::kTimeSlice, "TimeSlice", "Tempo", EffectClass::Temporal};
        return i;
    }
    void declare_parameters(ParameterRegistry& p) const override {
        p.add_angle("direction", "Direção das faixas", 90.0f);
        p.add_int("slices", "Número de faixas", 12, 1, static_cast<i32>(kMaxSlices));
        p.add_float("frame_offset", "Deslocamento de quadros", 0.0f, -600.0f, 600.0f, kParamAnimatable | kParamFrames);
        p.add_bool("interpolate", "Interpolar quadros", false);
    }
    bool is_identity(const EffectEval&) const noexcept override { return true; }
};

// -----------------------------------------------------------------------------
// Edições aleatórias (0.2.7)
//
// A camada vira trechos de "Duração do corte" quadros remontados numa ordem
// embaralhada, repetível pela semente (effects/RandomEdits.hpp). Declarativo
// como o Posterizar tempo: quem troca o tempo da camada é o renderer.
// -----------------------------------------------------------------------------
class RandomEdits final : public Effect {
public:
    enum : u32 { kLength = 0, kSeed };

    const EffectInfo& info() const noexcept override {
        static const EffectInfo i{effect_keys::kRandomEdits, "Edições aleatórias", "Tempo", EffectClass::Temporal};
        return i;
    }
    void declare_parameters(ParameterRegistry& p) const override {
        p.add_int("edit_length", "Duração do corte", 2, 1, 120, kParamFrames);
        p.add_float("seed", "Semente", 0.123f, 0.0f, 100.0f, kParamNone);
        p.typed_range(0.0f, 1000000.0f);
    }
    bool is_identity(const EffectEval&) const noexcept override { return true; }
};

// -----------------------------------------------------------------------------
// Tempo amplo (0.3.0) — o CC Wide Time do AE
//
// A camada no quadro t vira a MÉDIA (pesos iguais, soma 1) das amostras em
//     t − Passos para trás … t + Passos para a frente
// de um em um quadro. Com "Desfoque de movimento nativo", o intervalo entre
// dois passos é preenchido por subamostras (as do desfoque de movimento da
// composição): o rastro vira um borrão contínuo em vez de cópias.
//
// O AE só vê o CONTEÚDO da camada em outros instantes (efeito não enxerga o
// próprio transform: por isso lá se pré-compõe). "Incluir movimento da camada"
// soma também a posição/escala/rotação de cada instante — o uso comum no
// celular, sem pré-compor. Desligado = o comportamento do AE.
//
// Declarativo como o TimeSlice: o renderer busca cada instante (decoder do
// vídeo, filha da pré-composição) e soma no acúmulo temporal.
// -----------------------------------------------------------------------------
class WideTime final : public Effect {
public:
    enum : u32 { kForward = 0, kBackward, kNativeBlur, kLayerMotion };
    static constexpr i32 kMaxSteps = 30;

    const EffectInfo& info() const noexcept override {
        static const EffectInfo i{effect_keys::kWideTime, "Tempo amplo", "Tempo", EffectClass::Temporal};
        return i;
    }
    void declare_parameters(ParameterRegistry& p) const override {
        p.add_int("forward_steps", "Passos para a frente", 3, 0, kMaxSteps);
        p.add_int("backward_steps", "Passos para trás", 3, 0, kMaxSteps);
        p.add_bool("native_motion_blur", "Desfoque de movimento nativo", false);
        p.add_bool("layer_motion", "Incluir movimento da camada", true);
    }
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
        // Facilidade da chave no cabeçote (o trecho que sai dela). Os três
        // primeiros ficam nos números de sempre — projetos antigos guardaram
        // 0 Linear, 1 Suave, 2 Segurar — e os dois novos entram no fim.
        static const char* const kInterp[] = {"Linear", "Suave", "Segurar", "Suave no início", "Suave no fim"};
        // O teto é o da fonte inteira (o motor prende no último quadro dela).
        p.add_float("time", "Tempo", 0.0f, 0.0f, 86400.0f, kParamAnimatable, "s");
        p.add_enum("interpolation", "Interpolação do tempo", kInterp, 5, 0);
    }
    bool is_identity(const EffectEval&) const noexcept override { return true; }
};

void register_temporal_effects(EffectRegistry& r) {
    (void)r.add(std::make_unique<PosterizeTime>());
    (void)r.add(std::make_unique<LowFps>());
    (void)r.add(std::make_unique<TimeWarpRgb>());
    (void)r.add(std::make_unique<TimeRemapEffect>());
    (void)r.add(std::make_unique<TimeSlice>());
    (void)r.add(std::make_unique<WideTime>());
}

void register_random_edits_effect(EffectRegistry& r) { (void)r.add(std::make_unique<RandomEdits>()); }


} // namespace aurea::builtin
