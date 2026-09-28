// =============================================================================
//  Aurea / audio / AudioFx.cpp — os efeitos de áudio da camada.
//
//  Cada clipe com cadeia vira um FLUXO (MixState::Stream): lê a fonte (com
//  velocidade, remapeamento e Reverso), passa pelos estágios em ordem e
//  guarda o resultado num anel de ~1,4 s. O mixer lê do anel; pedidos que se
//  sobrepõem (varispeed, repetição por bloco faltando, snapshot novo durante
//  o play) são servidos do que já foi processado. Salto além do anel ou
//  cadeia diferente = recomeço com pré-rolagem (ver AudioEffects.hpp).
//
//  DSP próprio, a partir do comportamento de cada efeito:
//    Atraso        linha com realimentação: eco em T, T·2, … com
//                  quantidade·realimentação^(k−1).
//    Flange/chorus N vozes, cada uma uma cópia atrasada k·separação +
//                  profundidade·separação·(1+sen)/2, fases defasadas por voz.
//    Passa-alta/   biquad de 2ª ordem (Butterworth, Q = 1/√2).
//    baixa
//    Mixer estéreo nível e pan de cada canal (lei linear: centro = metade
//                  para cada lado), inversão de fase.
//    Modulador     vibrato por atraso variável (a profundidade é o desvio de
//                  frequência de pico) + modulação de amplitude.
//    EQ            três filtros de pico (RBJ) em série.
//    Reverb        pré-atraso, 8 filtros-pente com amortecimento por canal
//                  (espalhamento estéreo) e 4 passa-tudos de difusão.
//    Tom           até 5 osciladores (seno, triângulo, dente de serra e
//                  quadrada com PolyBLEP) com fase = ∫ frequência.
// =============================================================================
#include "AudioInternal.hpp"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstring>

namespace aurea::audio {

namespace {
constexpr f64 kPi = 3.14159265358979323846;
constexpr f64 kRate = static_cast<f64>(kMixRate);
constexpr i64 kSub = 32;                  ///< sub-bloco de parâmetros (alinhado ao tempo absoluto)
constexpr u32 kRingFrames = 1u << 16;     ///< ~1,37 s de histórico processado
constexpr i64 kMaxRequest = 32768;        ///< maior trecho por passada (cabe no anel com folga)

i64 floor_div(i64 a, i64 b) noexcept { return a >= 0 ? a / b : -((-a + b - 1) / b); }
f64 frac(f64 x) noexcept { return x - std::floor(x); }
}

// =============================================================================
// Parâmetros no tempo
// =============================================================================
f32 FxStage::at(u32 i, i64 t) const noexcept {
    if (i >= params.size()) return 0.0f;
    const FxParam& p = params[i];
    if (p.byFrame.empty()) return p.value;
    const f64 fr = static_cast<f64>(t - envShift) * fps / kRate - static_cast<f64>(frame0);
    const i64 n = static_cast<i64>(p.byFrame.size());
    const i64 k = std::clamp<i64>(static_cast<i64>(std::floor(fr)), 0, n - 1);
    const i64 j = std::min(k + 1, n - 1);
    const f32 a = static_cast<f32>(std::clamp(fr - static_cast<f64>(k), 0.0, 1.0));
    return p.byFrame[static_cast<usize>(k)] + (p.byFrame[static_cast<usize>(j)] - p.byFrame[static_cast<usize>(k)]) * a;
}

f64 FxStage::turns(u32 i, i64 t) const noexcept {
    if (i >= params.size()) return 0.0;
    const FxParam& p = params[i];
    if (p.byFrame.empty() || p.integral.size() != p.byFrame.size()) {
        return static_cast<f64>(p.value) * static_cast<f64>(t - origin) / kRate;
    }
    // Quadros (fracionários) desde frame0; fora da faixa, o valor da ponta.
    const f64 fr = static_cast<f64>(t - envShift) * fps / kRate - static_cast<f64>(frame0);
    const i64 n = static_cast<i64>(p.byFrame.size());
    if (fr <= 0.0) return fr * static_cast<f64>(p.byFrame.front()) / fps;
    const i64 k = std::min<i64>(static_cast<i64>(std::floor(fr)), n - 1);
    const f64 x = fr - static_cast<f64>(k);
    const f64 v0 = p.byFrame[static_cast<usize>(k)];
    const f64 v1 = p.byFrame[static_cast<usize>(std::min(k + 1, n - 1))];
    const f64 slope = k + 1 < n ? v1 - v0 : 0.0;
    return p.integral[static_cast<usize>(k)] + (x * v0 + 0.5 * x * x * slope) / fps;
}

namespace {

f32 max_of(const FxStage& s, u32 i) noexcept {
    if (i >= s.params.size()) return 0.0f;
    const FxParam& p = s.params[i];
    if (p.byFrame.empty()) return p.value;
    return *std::max_element(p.byFrame.begin(), p.byFrame.end());
}

/// Tempo (amostras) para uma linha com realimentação `g` a cada `period`
/// amostras cair −100 dB.
i64 feedback_tail(f64 period, f64 g) noexcept {
    if (period <= 0.0) return 0;
    if (!(g > 1e-6)) return static_cast<i64>(std::ceil(period));
    if (g >= 0.9999) return kMaxPreroll;
    const f64 k = std::log(1e-5) / std::log(g);
    return static_cast<i64>(std::min<f64>(static_cast<f64>(kMaxPreroll), period * (1.0 + k)));
}

// Afinação dos pentes e passa-tudos (amostras a 48 kHz, na escala 1):
// comprimentos primos entre si, na faixa de 23–37 ms.
constexpr u32 kCombBase[8] = {1117, 1187, 1277, 1361, 1423, 1493, 1559, 1619};
constexpr u32 kAllpassBase[4] = {557, 443, 347, 227};
constexpr u32 kStereoSpread = 23;

f64 reverb_scale(f64 timeMs) noexcept { return std::clamp(timeMs / 60.0, 0.5, 3.0); }
f64 reverb_predelay(f64 timeMs) noexcept { return std::clamp(timeMs, 0.0, 2000.0) * 0.5 * kRate / 1000.0; }
f64 reverb_feedback(f64 decay) noexcept { return 0.70 + 0.28 * std::clamp(decay, 0.0, 1.0); }

} // namespace

i64 chain_preroll(const std::vector<FxStage>& chain) noexcept {
    i64 total = 0;
    for (const FxStage& s : chain) {
        i64 need = 0;
        switch (s.kind) {
            case FxKind::Delay: {
                const f64 period = std::max(1.0, static_cast<f64>(max_of(s, fxp::kDelayTime)) * kRate / 1000.0);
                need = feedback_tail(period, max_of(s, fxp::kDelayFeedback) / 100.0);
                break;
            }
            case FxKind::FlangeChorus:
            case FxKind::Modulator:
                need = static_cast<i64>(std::ceil(static_cast<f64>(s.maxDelayMs) * kRate / 1000.0)) + 64;
                break;
            case FxKind::HighLowPass:
            case FxKind::ParametricEq:
                need = static_cast<i64>(kRate / 4);
                break;
            case FxKind::Reverb: {
                const f64 scale = reverb_scale(max_of(s, fxp::kReverbTime));
                const f64 g = reverb_feedback(max_of(s, fxp::kDecay) / 100.0);
                need = static_cast<i64>(reverb_predelay(max_of(s, fxp::kReverbTime)))
                     + feedback_tail(kCombBase[7] * scale + kStereoSpread, g) + 4096;
                break;
            }
            default: break;
        }
        total = std::min(kMaxPreroll, total + need);
    }
    return total;
}

// =============================================================================
// Filtros
// =============================================================================
Biquad eq_band(f64 hz, f64 bandwidthPercent, f64 gainDb) noexcept {
    Biquad b;
    const f64 f = std::clamp(hz, 10.0, kRate * 0.49);
    const f64 q = 100.0 / std::clamp(bandwidthPercent, 0.5, 1000.0);
    const f64 A = std::pow(10.0, std::clamp(gainDb, -60.0, 60.0) / 40.0);
    const f64 w0 = 2.0 * kPi * f / kRate;
    const f64 alpha = std::sin(w0) / (2.0 * q);
    const f64 c = std::cos(w0);
    const f64 a0 = 1.0 + alpha / A;
    b.b0 = (1.0 + alpha * A) / a0;
    b.b1 = (-2.0 * c) / a0;
    b.b2 = (1.0 - alpha * A) / a0;
    b.a1 = (-2.0 * c) / a0;
    b.a2 = (1.0 - alpha / A) / a0;
    return b;
}

Biquad pass_filter(bool highPass, f64 hz) noexcept {
    Biquad b;
    const f64 f = std::clamp(hz, 10.0, kRate * 0.49);
    const f64 w0 = 2.0 * kPi * f / kRate;
    const f64 alpha = std::sin(w0) / (2.0 * 0.70710678118654752);
    const f64 c = std::cos(w0);
    const f64 a0 = 1.0 + alpha;
    if (highPass) {
        b.b0 = (1.0 + c) * 0.5 / a0;
        b.b1 = -(1.0 + c) / a0;
        b.b2 = b.b0;
    } else {
        b.b0 = (1.0 - c) * 0.5 / a0;
        b.b1 = (1.0 - c) / a0;
        b.b2 = b.b0;
    }
    b.a1 = -2.0 * c / a0;
    b.a2 = (1.0 - alpha) / a0;
    return b;
}

f64 biquad_response_db(const Biquad& b, f64 hz) noexcept {
    const f64 w = 2.0 * kPi * std::clamp(hz, 0.0, kRate * 0.5) / kRate;
    const std::complex<f64> z1 = std::polar(1.0, -w), z2 = std::polar(1.0, -2.0 * w);
    const std::complex<f64> num = b.b0 + b.b1 * z1 + b.b2 * z2;
    const std::complex<f64> den = 1.0 + b.a1 * z1 + b.a2 * z2;
    const f64 mag = std::abs(num) / std::max(std::abs(den), 1e-30);
    return 20.0 * std::log10(std::max(mag, 1e-12));
}

// =============================================================================
// Análise para os efeitos visuais
// =============================================================================
namespace {
void fft(std::complex<f32>* a, u32 n) noexcept {
    for (u32 i = 1, j = 0; i < n; ++i) {
        u32 bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(a[i], a[j]);
    }
    for (u32 len = 2; len <= n; len <<= 1) {
        const f64 ang = -2.0 * kPi / static_cast<f64>(len);
        const std::complex<f32> wl(static_cast<f32>(std::cos(ang)), static_cast<f32>(std::sin(ang)));
        for (u32 i = 0; i < n; i += len) {
            std::complex<f32> w(1.0f, 0.0f);
            for (u32 k = 0; k < len / 2; ++k) {
                const std::complex<f32> u = a[i + k], v = a[i + k + len / 2] * w;
                a[i + k] = u + v;
                a[i + k + len / 2] = u - v;
                w *= wl;
            }
        }
    }
}

/// Amplitudes por bin (seno em escala cheia ≈ 1) de `count` amostras com
/// janela de Hann, completadas com zero até `n`.
void window_magnitudes(const f32* x, u32 count, u32 n, std::vector<std::complex<f32>>& buf, std::vector<f32>& mag) {
    buf.assign(n, {0.0f, 0.0f});
    f64 wsum = 0.0;
    for (u32 i = 0; i < count && i < n; ++i) {
        const f64 w = count > 1 ? 0.5 - 0.5 * std::cos(2.0 * kPi * i / (count - 1)) : 1.0;
        wsum += w;
        buf[i] = {static_cast<f32>(x[i] * w), 0.0f};
    }
    fft(buf.data(), n);
    const f32 norm = wsum > 0.0 ? static_cast<f32>(2.0 / wsum) : 0.0f;
    mag.resize(n / 2 + 1);
    for (u32 k = 0; k <= n / 2; ++k) mag[k] = std::abs(buf[k]) * norm;
}
} // namespace

u32 analyze_linear_bands(const f32* mono, u32 count, f32 startHz, f32 endHz, u32 bands, bool averaging,
                         f32* out) noexcept {
    if (!out || bands == 0) return 0;
    std::fill(out, out + bands, 0.0f);
    if (!mono || count < 8) return 0;
    std::vector<std::complex<f32>> buf;
    std::vector<f32> mag, acc;
    u32 n = 0;
    if (averaging && count > 1024) {
        n = 1024;
        u32 windows = 0;
        for (u32 at = 0; at + 1024 <= count; at += 512) {
            window_magnitudes(mono + at, 1024, n, buf, mag);
            if (acc.empty()) acc.assign(mag.size(), 0.0f);
            for (usize k = 0; k < mag.size(); ++k) acc[k] += mag[k];
            ++windows;
        }
        for (f32& v : acc) v /= static_cast<f32>(std::max(1u, windows));
        mag.swap(acc);
    } else {
        n = 256;
        while (n < count && n < 65536) n <<= 1;
        window_magnitudes(mono, std::min(count, n), n, buf, mag);
    }
    const f64 binHz = kRate / n;
    f32 lo = std::clamp(std::min(startHz, endHz), 0.0f, static_cast<f32>(kRate * 0.5));
    f32 hi = std::clamp(std::max(startHz, endHz), 0.0f, static_cast<f32>(kRate * 0.5));
    if (hi - lo < 1.0f) hi = lo + 1.0f;
    const bool flip = startHz > endHz;
    u32 peak = 0;
    f32 best = -1.0f;
    for (u32 b = 0; b < bands; ++b) {
        const f64 f0 = lo + (hi - lo) * b / bands;
        const f64 f1 = lo + (hi - lo) * (b + 1) / bands;
        const f64 k0 = f0 / binHz, k1 = f1 / binHz;
        f32 v = 0.0f;
        const i64 a = static_cast<i64>(std::ceil(k0)), z = static_cast<i64>(std::floor(k1));
        if (z >= a) {
            for (i64 k = a; k <= z && k < static_cast<i64>(mag.size()); ++k) v = std::max(v, mag[static_cast<usize>(k)]);
        } else {
            // Faixa mais estreita que um bin: interpola no centro.
            const f64 c = 0.5 * (k0 + k1);
            const i64 i = std::clamp<i64>(static_cast<i64>(std::floor(c)), 0, static_cast<i64>(mag.size()) - 1);
            const i64 j = std::min<i64>(i + 1, static_cast<i64>(mag.size()) - 1);
            const f32 t = static_cast<f32>(c - static_cast<f64>(i));
            v = mag[static_cast<usize>(i)] + (mag[static_cast<usize>(j)] - mag[static_cast<usize>(i)]) * t;
        }
        const u32 slot = flip ? bands - 1 - b : b;
        out[slot] = v;
        if (v > best) { best = v; peak = slot; }
    }
    return peak;
}

// =============================================================================
// O fluxo
// =============================================================================
namespace {

/// Anel estéreo com leitura fracionária.
struct Ring {
    std::vector<f32> d;
    u32 mask = 0;
    u32 w = 0;
    void init(u32 minFrames) {
        u32 n = 16;
        while (n < minFrames + 4) n <<= 1;
        d.assign(static_cast<usize>(n) * 2, 0.0f);
        mask = n - 1;
        w = 0;
    }
    void push(f32 l, f32 r) noexcept {
        d[w * 2] = l;
        d[w * 2 + 1] = r;
        w = (w + 1) & mask;
    }
    /// O valor `back` amostras antes da próxima escrita (1 = o último).
    void read(f64 back, f32& l, f32& r) const noexcept {
        back = std::clamp(back, 1.0, static_cast<f64>(mask - 1));
        const f64 p = static_cast<f64>(w) - back;
        const f64 fl = std::floor(p);
        const f32 t = static_cast<f32>(p - fl);
        const u32 a = static_cast<u32>(static_cast<i64>(fl)) & mask;
        const u32 b = (a + 1) & mask;
        l = d[a * 2] + (d[b * 2] - d[a * 2]) * t;
        r = d[a * 2 + 1] + (d[b * 2 + 1] - d[a * 2 + 1]) * t;
    }
};

struct BiquadState {
    Biquad c;
    f64 z1[2]{0, 0}, z2[2]{0, 0};
    f32 key[3]{-1e30f, -1e30f, -1e30f};
    f32 run(u32 ch, f32 x) noexcept {
        const f64 y = c.b0 * x + z1[ch];
        z1[ch] = c.b1 * x - c.a1 * y + z2[ch];
        z2[ch] = c.b2 * x - c.a2 * y;
        return static_cast<f32>(y);
    }
};

struct StageState {
    Ring line;
    BiquadState bq[3];
    // Reverb
    std::vector<f32> comb[16];
    u32 combPos[16]{};
    f32 combLp[16]{};
    std::vector<f32> ap[8];
    u32 apPos[8]{};
};

f64 polyblep(f64 t, f64 dt) noexcept {
    if (dt <= 0.0) return 0.0;
    if (t < dt) { t /= dt; return t + t - t * t - 1.0; }
    if (t > 1.0 - dt) { t = (t - 1.0) / dt; return t * t + t + t + 1.0; }
    return 0.0;
}

f64 oscillator(u32 shape, f64 phase, f64 dt) noexcept {
    switch (shape) {
        case 1: return 1.0 - 4.0 * std::fabs(phase - 0.5);                       // triângulo
        case 2: return 2.0 * phase - 1.0 - polyblep(phase, dt);                 // dente de serra
        case 3: return (phase < 0.5 ? 1.0 : -1.0) + polyblep(phase, dt) - polyblep(frac(phase + 0.5), dt);   // quadrada
        default: return std::sin(2.0 * kPi * phase);                            // seno
    }
}

} // namespace

struct MixState::Stream {
    u64 fxHash = 0;
    u64 lastEpoch = 0;
    bool valid = false;
    i64 pos = 0;         ///< próxima amostra a processar
    i64 bufStart = 0;    ///< primeira amostra ainda no anel
    std::vector<f32> ring;
    std::vector<StageState> stages;
    // Envios antigos (reverb/flanger/eco sem estado) da mesma camada.
    struct SendTaps { u32 count = 0; std::array<f64, 24> delay{}; std::array<f32, 24> gain{}; };
    std::vector<SendTaps> sendTaps;
    // Leitura da fonte: dois blocos à mão.
    i64 blockIndex[2]{-1, -1};
    const AudioBlock* block[2]{nullptr, nullptr};
    u32 blockNext = 0;
};

MixState::MixState() = default;
MixState::~MixState() = default;
void MixState::clear() noexcept { streams_.clear(); }

void MixState::sweep(u64 epoch) noexcept {
    if (streams_.size() <= 32) return;
    for (auto it = streams_.begin(); it != streams_.end();) {
        if (!it->second || it->second->lastEpoch + 1000 < epoch) it = streams_.erase(it);
        else ++it;
    }
}

MixState::Stream& MixState::slot(u64 key, u64 epoch) {
    std::unique_ptr<Stream>& s = streams_[key];
    if (!s) s = std::make_unique<Stream>();
    s->lastEpoch = epoch;
    return *s;
}

namespace detail {
namespace {

/// Uma amostra da fonte (sem efeitos), estéreo; fora da mídia = silêncio.
bool source_frame(const AudioClip& c, MixState::Stream& st, BlockSource& blocks, i64 idx, f32& l, f32& r,
                  u32& missing) noexcept {
    if (idx < 0 || idx >= c.sourceLength) return false;
    const i64 b = idx / kBlockFrames;
    const AudioBlock* blk = nullptr;
    if (st.blockIndex[0] == b) blk = st.block[0];
    else if (st.blockIndex[1] == b) blk = st.block[1];
    else {
        blk = blocks.block(c.asset, b);
        if (!blk) ++missing;
        const u32 k = st.blockNext;
        st.blockIndex[k] = b;
        st.block[k] = blk;
        st.blockNext ^= 1u;
    }
    if (!blk) return false;
    const usize off = static_cast<usize>(idx - b * kBlockFrames) * 2;
    if (off + 1 >= blk->pcm.size()) return false;
    l = blk->pcm[off];
    r = blk->pcm[off + 1];
    return true;
}

/// A fonte num instante FRACIONÁRIO da raiz (os envios antigos leem entre
/// amostras), dentro do clipe; fora dele, silêncio.
void raw_at(const AudioClip& c, MixState::Stream& st, BlockSource& blocks, f64 t, f32& l, f32& r, u32& missing) noexcept {
    l = r = 0.0f;
    if (t < static_cast<f64>(std::min(c.start, c.revFrom)) || t >= static_cast<f64>(std::max(c.end, c.revTo))) return;
    const i64 ti = static_cast<i64>(std::floor(t));
    auto position = [&](i64 x) {
        return c.rate == 1.0 && c.srcByFrame.empty() ? static_cast<f64>(c.sourceAt0 + x - c.start) : clip_source_pos(c, x);
    };
    const f64 pos = position(ti) + (position(ti + 1) - position(ti)) * (t - static_cast<f64>(ti));
    const i64 at = static_cast<i64>(std::floor(pos));
    const f32 fr = static_cast<f32>(pos - static_cast<f64>(at));
    f32 l0 = 0, r0 = 0, l1 = 0, r1 = 0;
    const bool a = source_frame(c, st, blocks, at, l0, r0, missing);
    const bool b = source_frame(c, st, blocks, at + 1, l1, r1, missing);
    if (a) { l += l0 * (1 - fr); r += r0 * (1 - fr); }
    if (b) { l += l1 * fr; r += r1 * fr; }
}

/// O sinal seco do clipe na amostra `t` da raiz (Reverso já aplicado).
void dry_at(const AudioClip& c, MixState::Stream& st, BlockSource& blocks, i64 t, f32& l, f32& r, u32& missing) noexcept {
    l = r = 0.0f;
    if (c.asset == 0 || c.sourceLength <= 0) return;
    const i64 tt = c.reverse ? c.revFrom + c.revTo - 1 - t : t;
    if (!c.sends.empty()) {
        // Os envios antigos continuam valendo quando a camada ganha efeitos
        // novos: a mesma soma de ecos sem estado do caminho de sempre.
        raw_at(c, st, blocks, static_cast<f64>(tt), l, r, missing);
        for (usize j = 0; j < c.sends.size() && j < st.sendTaps.size(); ++j) {
            const AudioSend& fx = c.sends[j];
            if (fx.wet <= 0.0f) continue;
            f32 wl = 0.0f, wr = 0.0f;
            if (fx.kind == 1) {
                const f64 phase = 2.0 * kPi * fx.rate * static_cast<f64>(tt - c.start) / kRate;
                const f64 delay = (0.2 + 0.8 * (0.5 + 0.5 * std::sin(phase))) * fx.seconds * kRate;
                f32 gain = 1.0f, norm = 0.0f;
                for (u32 i = 1; i <= 4; ++i) {
                    f32 x, y;
                    raw_at(c, st, blocks, static_cast<f64>(tt) - delay * i, x, y, missing);
                    wl += x * gain; wr += y * gain; norm += gain; gain *= fx.decay;
                }
                wl /= norm; wr /= norm;
            } else {
                const MixState::Stream::SendTaps& taps = st.sendTaps[j];
                for (u32 i = 0; i < taps.count; ++i) {
                    f32 x, y;
                    raw_at(c, st, blocks, static_cast<f64>(tt) - taps.delay[i], x, y, missing);
                    wl += (fx.kind == 0 && i % 2 ? y : x) * taps.gain[i];
                    wr += (fx.kind == 0 && i % 2 ? x : y) * taps.gain[i];
                }
            }
            l += wl * fx.wet;
            r += wr * fx.wet;
        }
        return;
    }
    if (c.rate == 1.0 && c.srcByFrame.empty()) {
        (void)source_frame(c, st, blocks, c.sourceAt0 + (tt - c.start), l, r, missing);
        return;
    }
    const f64 pos = clip_source_pos(c, tt);
    if (!(pos >= 0.0) || pos >= static_cast<f64>(c.sourceLength)) return;
    const i64 i0 = static_cast<i64>(pos);
    const f32 fr = static_cast<f32>(pos - static_cast<f64>(i0));
    f32 l0 = 0, r0 = 0, l1 = 0, r1 = 0;
    if (!source_frame(c, st, blocks, i0, l0, r0, missing)) return;
    if (!source_frame(c, st, blocks, i0 + 1, l1, r1, missing)) { l1 = l0; r1 = r0; }
    l = l0 + (l1 - l0) * fr;
    r = r0 + (r1 - r0) * fr;
}

void reset_stream(const AudioClip& c, MixState::Stream& st) {
    st.valid = true;
    st.fxHash = c.fxHash;
    st.ring.assign(static_cast<usize>(kRingFrames) * 2, 0.0f);
    st.blockIndex[0] = st.blockIndex[1] = -1;
    st.block[0] = st.block[1] = nullptr;
    st.sendTaps.assign(c.sends.size(), {});
    for (usize j = 0; j < c.sends.size(); ++j) {
        const AudioSend& fx = c.sends[j];
        MixState::Stream::SendTaps& taps = st.sendTaps[j];
        if (fx.kind == 1) continue;
        taps.count = fx.kind == 0 ? 24 : 6;
        f32 norm = 0.0f;
        for (u32 i = 1; i <= taps.count; ++i) {
            const f32 d = fx.kind == 0 ? (.19f * i + .07f * std::sin(i * 2.39996f)) : static_cast<f32>(i);
            taps.delay[i - 1] = d * fx.seconds * kRate;
            taps.gain[i - 1] = std::pow(fx.decay, fx.kind == 0 ? (i - 1) * .22f : static_cast<f32>(i - 1));
            norm += taps.gain[i - 1];
        }
        for (u32 i = 0; i < taps.count; ++i) taps.gain[i] /= std::max(1.0f, norm);
    }
    st.stages.clear();
    st.stages.resize(c.chain.size());
    for (usize i = 0; i < c.chain.size(); ++i) {
        const FxStage& s = c.chain[i];
        StageState& ss = st.stages[i];
        switch (s.kind) {
            case FxKind::Delay:
                ss.line.init(static_cast<u32>(std::ceil(std::max(1.0f, s.maxDelayMs) * kRate / 1000.0)) + 4);
                break;
            case FxKind::FlangeChorus:
            case FxKind::Modulator:
                ss.line.init(static_cast<u32>(std::ceil(std::max(1.0f, s.maxDelayMs) * kRate / 1000.0)) + 8);
                break;
            case FxKind::Reverb: {
                const f64 scale = reverb_scale(s.maxDelayMs);
                ss.line.init(static_cast<u32>(reverb_predelay(s.maxDelayMs)) + 8);
                for (u32 k = 0; k < 16; ++k) {
                    const u32 len = static_cast<u32>(kCombBase[k % 8] * scale) + (k >= 8 ? kStereoSpread : 0u) + 2;
                    ss.comb[k].assign(len, 0.0f);
                }
                for (u32 k = 0; k < 8; ++k) {
                    ss.ap[k].assign(static_cast<usize>(kAllpassBase[k % 4] * std::max(1.0, scale * 0.5)) + (k >= 4 ? kStereoSpread : 0u) + 2, 0.0f);
                }
                break;
            }
            default: break;
        }
    }
}

/// Processa [a, b) (dentro de UM sub-bloco alinhado) no anel.
void process_block(const AudioClip& c, MixState::Stream& st, BlockSource& blocks, i64 a, i64 b, u32& missing) noexcept {
    f32 L[kSub], R[kSub];
    const i64 n = b - a;
    for (i64 i = 0; i < n; ++i) dry_at(c, st, blocks, a + i, L[i], R[i], missing);
    // Parâmetros lidos no início ALINHADO do sub-bloco: o mesmo valor qualquer
    // que seja o ponto onde o fluxo começou ou o tamanho do pedido.
    const i64 q = floor_div(a, kSub) * kSub;
    const f32 balL = c.pan > 0.0f ? 1.0f - c.pan : 1.0f;
    const f32 balR = c.pan < 0.0f ? 1.0f + c.pan : 1.0f;
    for (usize si = 0; si < c.chain.size(); ++si) {
        const FxStage& s = c.chain[si];
        StageState& ss = st.stages[si];
        switch (s.kind) {
            case FxKind::Envelope:
                for (i64 i = 0; i < n; ++i) {
                    const f32 g = clip_envelope(c, a + i);
                    L[i] *= g * balL;
                    R[i] *= g * balR;
                }
                break;
            case FxKind::Backwards:
                if (s.at(fxp::kSwapChannels, q) >= 0.5f) {
                    for (i64 i = 0; i < n; ++i) std::swap(L[i], R[i]);
                }
                break;
            case FxKind::Delay: {
                const f64 D = std::max(1.0, static_cast<f64>(s.at(fxp::kDelayTime, q)) * kRate / 1000.0);
                const f32 amount = s.at(fxp::kDelayAmount, q) / 100.0f;
                const f32 fb = std::clamp(s.at(fxp::kDelayFeedback, q) / 100.0f, 0.0f, 1.0f);
                const f32 dry = s.at(fxp::kDelayDry, q) / 100.0f, wet = s.at(fxp::kDelayWet, q) / 100.0f;
                for (i64 i = 0; i < n; ++i) {
                    f32 dl, dr;
                    ss.line.read(D, dl, dr);
                    ss.line.push(L[i] + fb * dl, R[i] + fb * dr);
                    L[i] = dry * L[i] + wet * amount * dl;
                    R[i] = dry * R[i] + wet * amount * dr;
                }
                break;
            }
            case FxKind::FlangeChorus: {
                const f64 sep = std::max(0.0, static_cast<f64>(s.at(fxp::kVoiceSeparation, q))) * kRate / 1000.0;
                const i32 voices = std::clamp(static_cast<i32>(std::lround(s.at(fxp::kVoices, q))), 1, 8);
                const f64 depth = std::clamp(static_cast<f64>(s.at(fxp::kFlangeDepth, q)) / 100.0, 0.0, 1.0);
                const f64 phaseStep = static_cast<f64>(s.at(fxp::kVoicePhase, q)) / 360.0;
                const f32 sign = s.at(fxp::kFlangeInvert, q) >= 0.5f ? -1.0f : 1.0f;
                const bool stereo = s.at(fxp::kStereoVoices, q) >= 0.5f;
                const f32 dry = s.at(fxp::kFlangeDry, q) / 100.0f, wet = s.at(fxp::kFlangeWet, q) / 100.0f;
                const i32 left = (voices + 1) / 2, right = voices / 2;
                for (i64 i = 0; i < n; ++i) {
                    ss.line.push(L[i], R[i]);
                    const f64 lfo = s.turns(fxp::kFlangeRate, a + i);
                    f32 wl = 0, wr = 0;
                    for (i32 k = 1; k <= voices; ++k) {
                        const f64 ph = 2.0 * kPi * (lfo + (k - 1) * phaseStep);
                        const f64 d = sep * k + sep * depth * 0.5 * (1.0 + std::sin(ph));
                        f32 vl, vr;
                        ss.line.read(d + 1.0, vl, vr);
                        if (!stereo) { wl += vl; wr += vr; }
                        else if (k % 2 == 1 || voices == 1) wl += 0.5f * (vl + vr);
                        else wr += 0.5f * (vl + vr);
                    }
                    if (!stereo) { wl /= voices; wr /= voices; }
                    else if (voices == 1) { wr = wl; }
                    else { wl /= std::max(1, left); wr /= std::max(1, right); }
                    L[i] = dry * L[i] + wet * sign * wl;
                    R[i] = dry * R[i] + wet * sign * wr;
                }
                break;
            }
            case FxKind::HighLowPass: {
                const bool high = s.at(fxp::kFilterType, q) < 0.5f;   // 0 = passa-alta
                const f32 hz = s.at(fxp::kCutoff, q);
                BiquadState& f = ss.bq[0];
                if (f.key[0] != hz || f.key[1] != (high ? 1.0f : 0.0f)) {
                    f.c = pass_filter(high, hz);
                    f.key[0] = hz;
                    f.key[1] = high ? 1.0f : 0.0f;
                }
                const f32 dry = s.at(fxp::kFilterDry, q) / 100.0f, wet = s.at(fxp::kFilterWet, q) / 100.0f;
                for (i64 i = 0; i < n; ++i) {
                    L[i] = dry * L[i] + wet * f.run(0, L[i]);
                    R[i] = dry * R[i] + wet * f.run(1, R[i]);
                }
                break;
            }
            case FxKind::StereoMixer: {
                const f32 ll = s.at(fxp::kLeftLevel, q) / 100.0f, rl = s.at(fxp::kRightLevel, q) / 100.0f;
                const f32 lp = std::clamp(s.at(fxp::kLeftPan, q) / 100.0f, -1.0f, 1.0f);
                const f32 rp = std::clamp(s.at(fxp::kRightPan, q) / 100.0f, -1.0f, 1.0f);
                const f32 sign = s.at(fxp::kMixerInvert, q) >= 0.5f ? -1.0f : 1.0f;
                for (i64 i = 0; i < n; ++i) {
                    const f32 x = L[i] * ll, y = R[i] * rl;
                    L[i] = sign * (x * (1.0f - lp) * 0.5f + y * (1.0f - rp) * 0.5f);
                    R[i] = sign * (x * (1.0f + lp) * 0.5f + y * (1.0f + rp) * 0.5f);
                }
                break;
            }
            case FxKind::Modulator: {
                const bool tri = s.at(fxp::kModType, q) >= 0.5f;
                const f64 rate = std::max(0.01, static_cast<f64>(s.at(fxp::kModRate, q)));
                const f64 dev = std::clamp(static_cast<f64>(s.at(fxp::kModDepth, q)) / 100.0, 0.0, 1.0);
                const f32 am = std::clamp(s.at(fxp::kModAmplitude, q) / 100.0f, 0.0f, 1.0f);
                // Desvio de frequência de pico = profundidade: A·2πf (seno) ou A·4f (triângulo).
                f64 A = tri ? dev / (4.0 * rate) : dev / (2.0 * kPi * rate);
                A = std::min(A * kRate, 0.5 * static_cast<f64>(ss.line.mask) - 4.0);
                for (i64 i = 0; i < n; ++i) {
                    ss.line.push(L[i], R[i]);
                    const f64 ph = frac(s.turns(fxp::kModRate, a + i));
                    const f64 w = tri ? 4.0 * std::fabs(ph - 0.5) - 1.0 : std::cos(2.0 * kPi * ph);
                    f32 vl = L[i], vr = R[i];
                    if (A > 0.0) ss.line.read(1.0 + A * (1.0 - w), vl, vr);
                    const f32 g = 1.0f - am * static_cast<f32>(1.0 - w) * 0.5f;
                    L[i] = vl * g;
                    R[i] = vr * g;
                }
                break;
            }
            case FxKind::ParametricEq: {
                for (u32 band = 0; band < 3; ++band) {
                    const u32 o = band * fxp::kEqBandStride;
                    if (s.at(o + fxp::kEqEnable, q) < 0.5f) continue;
                    const f32 hz = s.at(o + fxp::kEqFrequency, q), bw = s.at(o + fxp::kEqBandwidth, q),
                              db = s.at(o + fxp::kEqGain, q);
                    BiquadState& f = ss.bq[band];
                    if (f.key[0] != hz || f.key[1] != bw || f.key[2] != db) {
                        f.c = eq_band(hz, bw, db);
                        f.key[0] = hz;
                        f.key[1] = bw;
                        f.key[2] = db;
                    }
                    for (i64 i = 0; i < n; ++i) {
                        L[i] = f.run(0, L[i]);
                        R[i] = f.run(1, R[i]);
                    }
                }
                break;
            }
            case FxKind::Reverb: {
                const f64 timeMs = s.at(fxp::kReverbTime, q);
                const f64 scale = std::min(reverb_scale(timeMs), reverb_scale(s.maxDelayMs));
                const f64 pre = reverb_predelay(std::min<f64>(timeMs, s.maxDelayMs));
                const f32 diff = std::clamp(s.at(fxp::kDiffusion, q) / 100.0f, 0.0f, 1.0f);
                const f32 g = static_cast<f32>(reverb_feedback(s.at(fxp::kDecay, q) / 100.0f));
                const f32 damp = 0.05f + 0.9f * (1.0f - std::clamp(s.at(fxp::kBrightness, q) / 100.0f, 0.0f, 1.0f));
                const f32 apg = 0.2f + 0.5f * diff;
                const f32 dry = s.at(fxp::kReverbDry, q) / 100.0f, wet = s.at(fxp::kReverbWet, q) / 100.0f;
                u32 combLen[16], apLen[8];
                for (u32 k = 0; k < 16; ++k) {
                    combLen[k] = std::min<u32>(static_cast<u32>(ss.comb[k].size()),
                                               static_cast<u32>(kCombBase[k % 8] * scale) + (k >= 8 ? kStereoSpread : 0u) + 1);
                }
                for (u32 k = 0; k < 8; ++k) {
                    apLen[k] = std::min<u32>(static_cast<u32>(ss.ap[k].size()),
                                             static_cast<u32>(kAllpassBase[k % 4] * std::max(1.0, scale * 0.5)) + (k >= 4 ? kStereoSpread : 0u) + 1);
                }
                for (i64 i = 0; i < n; ++i) {
                    f32 pl = L[i], pr = R[i];
                    ss.line.push(L[i], R[i]);
                    if (pre >= 1.0) ss.line.read(pre, pl, pr);
                    const f32 in = (pl + pr) * 0.015f;
                    f32 out[2] = {0.0f, 0.0f};
                    for (u32 k = 0; k < 16; ++k) {
                        std::vector<f32>& buf = ss.comb[k];
                        u32& p = ss.combPos[k];
                        if (p >= combLen[k]) p = 0;
                        const f32 y = buf[p];
                        ss.combLp[k] = y * (1.0f - damp) + ss.combLp[k] * damp;
                        buf[p] = in + ss.combLp[k] * g;
                        if (++p >= combLen[k]) p = 0;
                        out[k >= 8 ? 1 : 0] += y;
                    }
                    for (u32 k = 0; k < 8; ++k) {
                        std::vector<f32>& buf = ss.ap[k];
                        u32& p = ss.apPos[k];
                        if (p >= apLen[k]) p = 0;
                        const f32 bo = buf[p];
                        f32& v = out[k >= 4 ? 1 : 0];
                        const f32 y = -v + bo;
                        buf[p] = v + bo * apg;
                        v = y;
                        if (++p >= apLen[k]) p = 0;
                    }
                    // Desnormais: o rabo que decai para zero não pode travar a CPU.
                    if (std::fabs(out[0]) < 1e-20f) out[0] = 0.0f;
                    if (std::fabs(out[1]) < 1e-20f) out[1] = 0.0f;
                    L[i] = dry * L[i] + wet * 3.0f * out[0];
                    R[i] = dry * R[i] + wet * 3.0f * out[1];
                }
                for (u32 k = 0; k < 16; ++k) if (std::fabs(ss.combLp[k]) < 1e-20f) ss.combLp[k] = 0.0f;
                break;
            }
            case FxKind::Tone: {
                const u32 shape = std::min<u32>(3u, static_cast<u32>(std::max(0.0f, s.at(fxp::kWaveform, q)) + 0.5f));
                const f32 level = std::max(0.0f, s.at(fxp::kToneLevel, q)) / 100.0f;
                f64 dt[5];
                for (u32 k = 0; k < 5; ++k) dt[k] = std::max(0.0, static_cast<f64>(s.at(fxp::kFreq1 + k, q))) / kRate;
                for (i64 i = 0; i < n; ++i) {
                    f64 y = 0.0;
                    for (u32 k = 0; k < 5; ++k) {
                        if (dt[k] <= 0.0 || dt[k] >= 0.5) continue;
                        y += oscillator(shape, frac(s.turns(fxp::kFreq1 + k, a + i)), dt[k]);
                    }
                    // O Tom GERA: o que vinha antes na cadeia é substituído.
                    L[i] = R[i] = static_cast<f32>(y) * level;
                }
                break;
            }
        }
    }
    for (i64 i = 0; i < n; ++i) {
        const u32 slot = static_cast<u32>((a + i) & (kRingFrames - 1));
        st.ring[slot * 2] = L[i];
        st.ring[slot * 2 + 1] = R[i];
    }
}

/// Blocos da fonte que o trecho [from, to) do fluxo lê e que ainda não estão
/// prontos. Conferido ANTES de processar: com bloco faltando (preview no
/// começo do play, logo depois de um salto) nada é processado — a repetição
/// do mixer não refaz a pré-rolagem inteira a cada bloco que chega.
u32 probe_blocks(const AudioClip& c, BlockSource& blocks, i64 from, i64 to) noexcept {
    if (c.asset == 0 || c.sourceLength <= 0 || from >= to) return 0;
    u32 lacking = 0;
    i64 last = -1;
    auto check = [&](i64 idx) {
        if (idx < 0 || idx >= c.sourceLength) return;
        const i64 b = idx / kBlockFrames;
        if (b == last) return;
        last = b;
        if (!blocks.block(c.asset, b)) ++lacking;
    };
    // Passo de 1000 amostras: nem a 16× de velocidade um bloco (24000) escapa.
    for (i64 t = from;; t += 1000) {
        const i64 at = std::min(t, to - 1);
        const i64 tt = c.reverse ? c.revFrom + c.revTo - 1 - at : at;
        const f64 pos = c.rate == 1.0 && c.srcByFrame.empty() ? static_cast<f64>(c.sourceAt0 + (tt - c.start))
                                                               : clip_source_pos(c, tt);
        if (std::isfinite(pos)) {
            const i64 i0 = static_cast<i64>(std::floor(pos));
            check(i0);
            check(i0 + 1);
        }
        if (at >= to - 1) break;
    }
    return lacking;
}

/// Avança o fluxo até `to` (exclusivo), sub-bloco por sub-bloco.
void advance(const AudioClip& c, MixState::Stream& st, BlockSource& blocks, i64 to, u32& missing) noexcept {
    while (st.pos < to) {
        const i64 end = std::min(to, (floor_div(st.pos, kSub) + 1) * kSub);
        process_block(c, st, blocks, st.pos, end, missing);
        st.pos = end;
        st.bufStart = std::max(st.bufStart, st.pos - static_cast<i64>(kRingFrames) + 1);
    }
}

} // namespace

void mix_fx_clip(const AudioClip& c, i64 s0, i64 s1, i64 start, BlockSource& blocks, f32* out, MixState* state,
                 u64 epoch, u32& missing) noexcept {
    if (s0 >= s1) return;
    // Pedido maior que o anel: em pedaços (o fluxo continua de um para o outro).
    if (s1 - s0 > kMaxRequest) {
        for (i64 a = s0; a < s1; a += kMaxRequest) {
            mix_fx_clip(c, a, std::min(s1, a + kMaxRequest), start, blocks, out, state, epoch, missing);
        }
        return;
    }
    std::unique_ptr<MixState> local;
    if (!state) {
        local = std::make_unique<MixState>();
        state = local.get();
    }
    MixState::Stream& st = state->slot(c.streamKey, epoch);
    st.blockIndex[0] = st.blockIndex[1] = -1;   // os ponteiros de bloco valem só nesta chamada
    st.block[0] = st.block[1] = nullptr;
    // Continua se o fluxo é o MESMO clipe e o pedido começa dentro do que ele
    // guarda ou logo depois; senão recomeça com pré-rolagem.
    const bool same = st.valid && st.fxHash == c.fxHash;
    const i64 gapLimit = std::max<i64>(c.preroll, kMixRate);
    const bool continues = same && s0 >= st.bufStart && s0 <= st.pos + gapLimit;
    if (!continues) {
        reset_stream(c, st);
        st.pos = std::max(c.start, s0 - c.preroll);
        st.bufStart = st.pos;
        state->count_restart();
    }
    // Bloco da fonte faltando no trecho a processar: nada sai deste clipe
    // agora (o mixer repete o mesmo ponto quando o bloco chegar) e o fluxo
    // fica onde estava.
    if (const u32 lacking = probe_blocks(c, blocks, st.pos, s1)) {
        missing += lacking;
        if (!continues) st.valid = false;
        return;
    }
    const u32 before = missing;
    advance(c, st, blocks, s1, missing);
    for (i64 t = s0; t < s1; ++t) {
        const u32 slot = static_cast<u32>(t & (kRingFrames - 1));
        f32* o = out + static_cast<usize>(t - start) * 2;
        o[0] += st.ring[slot * 2];
        o[1] += st.ring[slot * 2 + 1];
    }
    // Faltou bloco: o que foi processado está errado. O próximo pedido (a
    // repetição) recomeça do zero, com os blocos já decodificados.
    if (missing != before) st.valid = false;
}

} // namespace detail
} // namespace aurea::audio
