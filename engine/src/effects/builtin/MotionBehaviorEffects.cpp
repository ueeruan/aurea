// =============================================================================
//  Comportamentos de movimento — Oscilar, Balançar e Agitar.
//
//  Os três movem a camada no PLANO DELA por uma função pura do tempo: nenhum
//  estado, nenhum sorteio guardado. O mesmo instante dá a mesma pose no
//  preview, no scrubbing de trás para a frente e no export — e em 24, 30 ou
//  60 qps, porque o relógio é em SEGUNDOS, não em quadros.
//
//  Todo número é parâmetro animável (keyframe ou expressão): dá para levar a
//  amplitude a zero exatamente onde o editor quiser. O Decaimento faz isso
//  sozinho: a amplitude cai por exp(-decaimento · t), com t em segundos desde
//  o início da camada — 1/s = cai a ~37% em um segundo.
//
//  Como o Transformar: quando o efeito é o ÚLTIMO da pilha a pose vira matriz
//  da composição (nenhum passe, nenhuma reamostragem extra, a camada pode sair
//  da própria caixa); no meio da pilha, vira um passe afim com a região da
//  caixa transformada, recortada ao quadro visível.
// =============================================================================
#include "BuiltinEffects.hpp"
#include "aurea/effects/ShakeMotion.hpp"

#include <algorithm>
#include <cmath>

namespace aurea::builtin {
namespace {

constexpr f32 kMaxTravel = 20000.0f;     // px: faixa digitada das amplitudes
constexpr f32 kMaxFrequency = 240.0f;    // Hz: acima de qps/2 já é quadro a quadro
constexpr f32 kMaxDecay = 100.0f;        // 1/s: 100 some em ~50 ms
constexpr f32 kMaxScalePulse = 1000.0f;  // %

f32 finite_or(f32 v, f32 fallback) noexcept { return std::isfinite(v) ? v : fallback; }

/// A pose que o comportamento pede neste instante, no plano da camada.
struct Pose {
    Vec2 offset{0.0f, 0.0f};    ///< px da camada
    f32  rotation = 0.0f;       ///< graus
    f32  scale = 1.0f;          ///< fator uniforme
    Vec2 pivot{0.0f, 0.0f};     ///< px da camada
};

/// Queda exponencial da amplitude. Antes do início da camada (o obturador do
/// motion blur lê um pouco para trás) o relógio não anda para trás.
f32 falloff(f32 decay, f64 seconds) noexcept {
    const f64 t = std::max(0.0, seconds);
    const f64 d = std::clamp(static_cast<f64>(finite_or(decay, 0.0f)), 0.0, static_cast<f64>(kMaxDecay));
    return static_cast<f32>(std::exp(-d * t));
}

/// Base comum: pose → matriz, identidade, dobra na composição e passe afim.
class MotionBehavior : public Effect {
public:
    void pipelines(std::vector<PipelineKey>& out, SurfaceFormat work) const override {
        out.push_back(PipelineKey::fullscreen(ShaderId::effects_affine_resample_frag, work));
    }

    bool is_identity(const EffectEval& e) const noexcept override {
        const Mat4 m = matrix(e);
        const Mat4 id = Mat4::identity();
        for (int c = 0; c < 4; ++c) {
            const Vec4 d = m.col[c] - id.col[c];
            if (std::fabs(d.x) > 1e-4f || std::fabs(d.y) > 1e-4f || std::fabs(d.z) > 1e-4f
                || std::fabs(d.w) > 1e-4f) {
                return false;
            }
        }
        return true;
    }

    bool fold_into_composite(const EffectEval& e, Mat4& layerMatrix, f32& opacity) const noexcept override {
        layerMatrix = matrix(e);
        opacity = 1.0f;
        return true;
    }

    Status build(EffectBuildContext& ctx, const EffectEval& e, const LayerImage& input, f32 margin,
                 LayerImage& out) const override {
        // A chave (ASCII) nomeia o passe: o nome exibido tem acento.
        return affine_pass(ctx, input, matrix(e), e.placement, 1.0f, info().key, margin, out);
    }

    /// T(pivô + deslocamento) · R · S · T(-pivô), em px da camada.
    [[nodiscard]] Mat4 matrix(const EffectEval& e) const noexcept {
        const Pose p = pose(e, seconds(e));
        const f32 s = std::max(1e-3f, finite_or(p.scale, 1.0f));
        const f32 r = finite_or(p.rotation, 0.0f) * kDeg2Rad;
        const Mat4 t = Mat4::translation(Vec3{p.pivot.x + finite_or(p.offset.x, 0.0f),
                                              p.pivot.y + finite_or(p.offset.y, 0.0f), 0.0f});
        const Mat4 rot = Mat4::from_quat(Quat::from_axis_angle(Vec3{0, 0, 1}, r));
        const Mat4 sc = Mat4::scale(Vec3{s, s, 1.0f});
        const Mat4 back = Mat4::translation(Vec3{-p.pivot.x, -p.pivot.y, 0.0f});
        return t * rot * sc * back;
    }

protected:
    [[nodiscard]] virtual Pose pose(const EffectEval& e, f64 seconds) const noexcept = 0;

    [[nodiscard]] static f64 seconds(const EffectEval& e) noexcept {
        const f64 fps = e.framesPerSecond > 0.0 ? e.framesPerSecond : 30.0;
        return static_cast<f64>(e.localTime.value) / fps;
    }
    /// Ponto relativo (0..1 da caixa natural) → px da camada.
    [[nodiscard]] static Vec2 layer_point(const EffectEval& e, Vec2 rel) noexcept {
        const f32 w = e.placement ? static_cast<f32>(e.placement->layerWidth) : 1.0f;
        const f32 h = e.placement ? static_cast<f32>(e.placement->layerHeight) : 1.0f;
        return Vec2{finite_or(rel.x, 0.5f) * w, finite_or(rel.y, 0.5f) * h};
    }
};

// -----------------------------------------------------------------------------
// Oscilar — vai e vem numa direção, com giro e pulso de escala em sincronia.
// -----------------------------------------------------------------------------
class Oscillate final : public MotionBehavior {
public:
    enum : u32 { kDirection = 0, kMagnitude, kFrequency, kPhase, kRotation, kScale, kWaveform,
                 kSeed, kDecay, kPivot };

    const EffectInfo& info() const noexcept override {
        static const EffectInfo i{effect_keys::kOscillate, "Oscilar", "Distorcer", EffectClass::Domain};
        return i;
    }
    void declare_parameters(ParameterRegistry& p) const override {
        // 0° = para a direita, 90° = para baixo (o eixo y da camada desce).
        p.add_angle("direction", "Direção", 0.0f);
        p.add_float("magnitude", "Amplitude", 40.0f, 0.0f, 1000.0f, kParamAnimatable | kParamPixels, "px");
        p.typed_range(0.0f, kMaxTravel);
        p.add_float("frequency", "Frequência", 1.0f, 0.0f, 30.0f, kParamAnimatable, "Hz");
        p.typed_range(0.0f, kMaxFrequency);
        // Ângulo sem teto prático: keyframear a fase É a "evolução" do ciclo.
        p.add_angle("phase", "Fase", 0.0f);
        p.add_angle("rotation", "Rotação", 0.0f);
        p.add_float("scale", "Pulso de escala", 0.0f, 0.0f, 100.0f, kParamAnimatable | kParamPercent, "%");
        p.typed_range(0.0f, kMaxScalePulse);
        static const char* const kWaves[] = {"Seno", "Triângulo", "Quadrada", "Dente de serra", "Aleatória"};
        p.add_enum("waveform", "Forma da onda", kWaves, 5, 0);
        p.add_int("seed", "Semente", 1, 0, 9999);
        p.add_float("decay", "Decaimento", 0.0f, 0.0f, 10.0f, kParamAnimatable, "1/s");
        p.typed_range(0.0f, kMaxDecay);
        p.add_point2("pivot", "Pivô", Vec2{0.5f, 0.5f}, -1.0f, 2.0f, kParamAnimatable | kParamRelative);
        p.typed_range(-10.0f, 10.0f);
    }
    bool demo_values(EffectInstance&, std::vector<ParamValue>& v) const noexcept override {
        v[kPhase] = ParamValue::scalar(90.0f);      // no pico no quadro 0: a prévia é um instante só
        v[kMagnitude] = ParamValue::scalar(18.0f);
        v[kRotation] = ParamValue::scalar(6.0f);
        return true;
    }

    /// Valor da onda em [-1, 1] com `turns` voltas do ciclo. Seno, triângulo e
    /// dente de serra passam por 0 na fase 0; a quadrada começa em +1.
    static f32 wave(u32 shape, f64 turns, u32 seed) noexcept {
        if (!std::isfinite(turns)) return 0.0f;
        const f64 u = turns - std::floor(turns);
        switch (shape) {
            case 1: return static_cast<f32>(u < 0.25 ? 4.0 * u : (u < 0.75 ? 2.0 - 4.0 * u : 4.0 * u - 4.0));
            case 2: return u < 0.5 ? 1.0f : -1.0f;
            case 3: return static_cast<f32>(u < 0.5 ? 2.0 * u : 2.0 * u - 2.0);
            case 4: return shake::noise(turns * 2.0, seed, 7u, 1.0f);   // um alvo novo a cada meia volta
            default: return static_cast<f32>(std::sin(u * 6.283185307179586));
        }
    }

protected:
    Pose pose(const EffectEval& e, f64 seconds) const noexcept override {
        const f64 freq = std::clamp(static_cast<f64>(finite_or(e.f(kFrequency), 0.0f)), 0.0,
                                    static_cast<f64>(kMaxFrequency));
        const f64 turns = std::max(0.0, seconds) * freq + static_cast<f64>(finite_or(e.f(kPhase), 0.0f)) / 360.0;
        const u32 seed = static_cast<u32>(std::max(0, e.value(kSeed).as_int()));
        const f32 w = wave(e.e(kWaveform), turns, seed) * falloff(e.f(kDecay), seconds);
        const f32 dir = finite_or(e.f(kDirection), 0.0f) * kDeg2Rad;
        const f32 mag = std::clamp(finite_or(e.f(kMagnitude), 0.0f), 0.0f, kMaxTravel);
        Pose p;
        p.offset = Vec2{std::cos(dir) * mag * w, std::sin(dir) * mag * w};
        p.rotation = finite_or(e.f(kRotation), 0.0f) * w;
        p.scale = 1.0f + std::clamp(finite_or(e.f(kScale), 0.0f), 0.0f, kMaxScalePulse) / 100.0f * w;
        p.pivot = layer_point(e, e.p2(kPivot));
        return p;
    }
};

// -----------------------------------------------------------------------------
// Balançar — pêndulo em volta de um pivô (por padrão, o meio da borda de cima).
// -----------------------------------------------------------------------------
class Swing final : public MotionBehavior {
public:
    enum : u32 { kAngle = 0, kFrequency, kPivot, kPhase, kDecay };

    const EffectInfo& info() const noexcept override {
        static const EffectInfo i{effect_keys::kSwing, "Balançar", "Distorcer", EffectClass::Domain};
        return i;
    }
    void declare_parameters(ParameterRegistry& p) const override {
        p.add_angle("angle", "Ângulo", 15.0f);   // para cada lado
        p.add_float("frequency", "Frequência", 1.0f, 0.0f, 30.0f, kParamAnimatable, "Hz");
        p.typed_range(0.0f, kMaxFrequency);
        p.add_point2("pivot", "Pivô", Vec2{0.5f, 0.0f}, -1.0f, 2.0f, kParamAnimatable | kParamRelative);
        p.typed_range(-10.0f, 10.0f);
        p.add_angle("phase", "Fase", 0.0f);
        p.add_float("decay", "Decaimento", 0.0f, 0.0f, 10.0f, kParamAnimatable, "1/s");
        p.typed_range(0.0f, kMaxDecay);
    }
    bool demo_values(EffectInstance&, std::vector<ParamValue>& v) const noexcept override {
        v[kPhase] = ParamValue::scalar(90.0f);
        v[kAngle] = ParamValue::scalar(12.0f);
        return true;
    }

protected:
    Pose pose(const EffectEval& e, f64 seconds) const noexcept override {
        const f64 freq = std::clamp(static_cast<f64>(finite_or(e.f(kFrequency), 0.0f)), 0.0,
                                    static_cast<f64>(kMaxFrequency));
        const f64 turns = std::max(0.0, seconds) * freq + static_cast<f64>(finite_or(e.f(kPhase), 0.0f)) / 360.0;
        const f64 u = turns - std::floor(turns);
        Pose p;
        p.rotation = finite_or(e.f(kAngle), 0.0f) * static_cast<f32>(std::sin(u * 6.283185307179586))
                   * falloff(e.f(kDecay), seconds);
        p.pivot = layer_point(e, e.p2(kPivot));
        return p;
    }
};

// -----------------------------------------------------------------------------
// Agitar — o "wiggle" da transformação, canal por canal.
//
// Cada canal (X, Y, rotação, escala) tem o seu próprio fluxo de ruído da
// mesma semente: mexer na amplitude de um não muda o desenho dos outros.
// "Segurar entre saltos" troca a curva suave por degraus (um valor novo a
// cada ciclo). Oitavas somam detalhe fino por cima do movimento principal.
// -----------------------------------------------------------------------------
class Wiggle final : public MotionBehavior {
public:
    enum : u32 { kFrequency = 0, kPositionX, kPositionY, kRotation, kScale, kAmount, kOctaves, kHold,
                 kSeed, kPivot };

    const EffectInfo& info() const noexcept override {
        static const EffectInfo i{effect_keys::kWiggle, "Agitar", "Distorcer", EffectClass::Domain};
        return i;
    }
    void declare_parameters(ParameterRegistry& p) const override {
        p.add_float("frequency", "Frequência", 2.0f, 0.0f, 30.0f, kParamAnimatable, "Hz");
        p.typed_range(0.0f, kMaxFrequency);
        p.add_float("position_x", "Posição X", 30.0f, 0.0f, 1000.0f, kParamAnimatable | kParamPixels, "px");
        p.typed_range(0.0f, kMaxTravel);
        p.add_float("position_y", "Posição Y", 30.0f, 0.0f, 1000.0f, kParamAnimatable | kParamPixels, "px");
        p.typed_range(0.0f, kMaxTravel);
        p.add_angle("rotation", "Rotação", 0.0f);
        p.add_float("scale", "Escala", 0.0f, 0.0f, 100.0f, kParamAnimatable | kParamPercent, "%");
        p.typed_range(0.0f, kMaxScalePulse);
        p.add_float("amount", "Intensidade", 100.0f, 0.0f, 200.0f, kParamAnimatable | kParamPercent, "%");
        p.typed_range(0.0f, 2000.0f);
        p.add_int("octaves", "Oitavas", 1, 1, 6);
        p.add_bool("hold", "Segurar entre saltos", false);
        p.add_int("seed", "Semente", 1, 0, 9999);
        p.add_point2("pivot", "Pivô", Vec2{0.5f, 0.5f}, -1.0f, 2.0f, kParamAnimatable | kParamRelative);
        p.typed_range(-10.0f, 10.0f);
    }
    bool demo_values(EffectInstance&, std::vector<ParamValue>& v) const noexcept override {
        v[kPositionX] = ParamValue::scalar(16.0f);
        v[kPositionY] = ParamValue::scalar(16.0f);
        v[kRotation] = ParamValue::scalar(8.0f);
        return true;
    }

    /// Um canal do wiggle em [-1, 1] no instante `cycles` (tempo × frequência).
    static f32 channel(f64 cycles, u32 seed, u32 axis, u32 octaves, bool hold) noexcept {
        if (!std::isfinite(cycles)) return 0.0f;
        f32 sum = 0.0f, amp = 1.0f, norm = 0.0f;
        f64 f = 1.0;
        for (u32 o = 0; o < octaves; ++o) {
            const u32 stream = axis + 32u * o;
            const f64 t = cycles * f;
            const f32 v = hold ? shake::random(seed, static_cast<std::int64_t>(std::floor(t)), stream)
                               : shake::noise(t, seed, stream, 1.0f);
            sum += v * amp;
            norm += amp;
            amp *= 0.5f;
            f *= 2.0;
        }
        return norm > 0.0f ? sum / norm : 0.0f;
    }

protected:
    Pose pose(const EffectEval& e, f64 seconds) const noexcept override {
        const f64 freq = std::clamp(static_cast<f64>(finite_or(e.f(kFrequency), 0.0f)), 0.0,
                                    static_cast<f64>(kMaxFrequency));
        const f64 cycles = std::clamp(seconds * freq, -1e9, 1e9);
        const u32 seed = static_cast<u32>(std::max(0, e.value(kSeed).as_int()));
        const u32 octaves = static_cast<u32>(std::clamp(e.value(kOctaves).as_int(), 1, 6));
        const bool hold = e.b(kHold);
        const f32 amount = std::clamp(finite_or(e.f(kAmount), 100.0f), 0.0f, 2000.0f) / 100.0f;
        Pose p;
        p.offset = Vec2{channel(cycles, seed, 0u, octaves, hold) * std::clamp(finite_or(e.f(kPositionX), 0.0f), 0.0f, kMaxTravel) * amount,
                        channel(cycles, seed, 1u, octaves, hold) * std::clamp(finite_or(e.f(kPositionY), 0.0f), 0.0f, kMaxTravel) * amount};
        p.rotation = channel(cycles, seed, 2u, octaves, hold) * finite_or(e.f(kRotation), 0.0f) * amount;
        p.scale = 1.0f + channel(cycles, seed, 3u, octaves, hold)
                       * std::clamp(finite_or(e.f(kScale), 0.0f), 0.0f, kMaxScalePulse) / 100.0f * amount;
        p.pivot = layer_point(e, e.p2(kPivot));
        return p;
    }
};


// -----------------------------------------------------------------------------
// Tremor em trancos (Twitch) — o do app antigo, refeito nativo.
//
// Um valor novo a cada tranco (Frequência trancos por segundo), SEGURO até o
// próximo; Suavizar mistura com o seguinte numa curva suave. Quatro fluxos da
// mesma semente — X, Y, rotação e escala — bem separados para nunca andarem
// juntos. O sorteio é um hash aritmético em float (sem seno), o mesmo em toda
// GPU e na CPU: a prévia e o export tremem igual.
//
// Intensidade é fração da ALTURA DO QUADRO (como no app antigo): a mesma
// intensidade anda a mesma distância em qualquer resolução. Gira e aproxima em
// volta do meio da camada.
// -----------------------------------------------------------------------------
class Twitch final : public MotionBehavior {
public:
    enum : u32 { kFrequency = 0, kStrength, kRotation, kScale, kSoften, kDecay, kSeed };

    const EffectInfo& info() const noexcept override {
        static const EffectInfo i{effect_keys::kTwitch, "Tremor em trancos", "Distorcer", EffectClass::Domain};
        return i;
    }
    void declare_parameters(ParameterRegistry& p) const override {
        p.add_float("frequency", "Frequência", 14.0f, 1.0f, 40.0f, kParamAnimatable, "Hz");
        p.typed_range(0.0f, kMaxFrequency);
        p.add_float("strength", "Intensidade", 1.5f, 0.0f, 50.0f, kParamAnimatable | kParamPercent, "%");
        p.typed_range(0.0f, 1000.0f);
        p.add_angle("rotation", "Rotação", 2.0f);
        p.add_float("scale", "Escala", 2.0f, 0.0f, 50.0f, kParamAnimatable | kParamPercent, "%");
        p.typed_range(0.0f, 1000.0f);
        p.add_float("soften", "Suavizar", 0.0f, 0.0f, 100.0f, kParamAnimatable | kParamPercent, "%");
        p.add_float("decay", "Decaimento", 0.0f, 0.0f, 5.0f, kParamAnimatable, "1/s");
        p.typed_range(0.0f, kMaxDecay);
        p.add_int("seed", "Semente", 0, 0, 100);
    }
    bool demo_values(EffectInstance&, std::vector<ParamValue>& v) const noexcept override {
        // O "Impacto" do app antigo, sem o decaimento (a prévia é um instante só).
        v[kFrequency] = ParamValue::scalar(20.0f);
        v[kStrength] = ParamValue::scalar(5.0f);
        v[kRotation] = ParamValue::scalar(6.0f);
        v[kScale] = ParamValue::scalar(6.0f);
        return true;
    }

    /// Hash sem seno em [0, 1): aritmética de float igual à do shader antigo.
    static f32 hash(f32 x) noexcept {
        f32 q = x * 0.1031f;
        q -= std::floor(q);
        q *= q + 33.33f;
        q *= q + q;
        return q - std::floor(q);
    }
    /// Um fluxo em [-1, 1]: o valor do tranco, levado ao próximo por `ease`.
    static f32 jolt(f32 stream, f32 tick, f32 f, f32 soften) noexcept {
        const f32 a = hash(tick + stream);
        const f32 b = hash(tick + 1.0f + stream);
        const f32 fc = std::clamp(f, 0.0f, 1.0f);
        const f32 k = fc * fc * (3.0f - 2.0f * fc) * std::clamp(soften, 0.0f, 1.0f);
        return (a + (b - a) * k) * 2.0f - 1.0f;
    }

protected:
    Pose pose(const EffectEval& e, f64 seconds) const noexcept override {
        const f64 t = std::max(0.0, seconds);
        const f64 freq = std::clamp(static_cast<f64>(finite_or(e.f(kFrequency), 0.0f)), 0.0,
                                    static_cast<f64>(kMaxFrequency));
        const f64 ticks = std::min(t * freq, 1e7);
        const f64 tickD = std::floor(ticks);
        const f32 tick = static_cast<f32>(tickD);
        const f32 f = static_cast<f32>(ticks - tickD);
        const f32 soften = finite_or(e.f(kSoften), 0.0f) / 100.0f;
        const f32 seed = static_cast<f32>(std::clamp(e.value(kSeed).as_int(), 0, 100)) * 37.0f;
        const f32 fall = falloff(e.f(kDecay), seconds);
        const f32 strength = std::clamp(finite_or(e.f(kStrength), 0.0f), 0.0f, 1000.0f) / 100.0f * fall;

        // Altura do quadro em px da camada, eixo a eixo (a camada pode estar escalada).
        f32 frameH = e.placement ? static_cast<f32>(e.placement->layerHeight) : 1.0f;
        f32 sx = 1.0f, sy = 1.0f;
        if (e.placement && !e.placement->inScene3d && e.placement->compHeight > 0) {
            const Mat4& m = e.placement->compFromLayer;
            sx = std::hypot(m.col[0].x, m.col[0].y);
            sy = std::hypot(m.col[1].x, m.col[1].y);
            if (!(sx > 1e-6f) || !std::isfinite(sx)) sx = 1.0f;
            if (!(sy > 1e-6f) || !std::isfinite(sy)) sy = 1.0f;
            frameH = static_cast<f32>(e.placement->compHeight);
        }

        Pose p;
        // O y do app antigo sobe; o da camada desce. O giro dele é anti-horário.
        p.offset = Vec2{jolt(seed, tick, f, soften) * strength * frameH / sx,
                        -jolt(seed + 11.0f, tick, f, soften) * strength * frameH / sy};
        p.rotation = -jolt(seed + 23.0f, tick, f, soften) * finite_or(e.f(kRotation), 0.0f) * fall;
        p.scale = 1.0f + jolt(seed + 41.0f, tick, f, soften)
                       * std::clamp(finite_or(e.f(kScale), 0.0f), 0.0f, 1000.0f) / 100.0f * fall;
        p.pivot = layer_point(e, Vec2{0.5f, 0.5f});
        return p;
    }
};

} // namespace

void register_motion_behavior_effects(EffectRegistry& r) {
    (void)r.add(std::make_unique<Oscillate>());
    (void)r.add(std::make_unique<Swing>());
    (void)r.add(std::make_unique<Wiggle>());
}

void register_twitch_effect(EffectRegistry& r) { (void)r.add(std::make_unique<Twitch>()); }

} // namespace aurea::builtin
