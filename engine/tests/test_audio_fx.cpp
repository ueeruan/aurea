// =============================================================================
//  Testes dos efeitos de ÁUDIO da camada (audio/AudioFx.cpp): o comportamento
//  numérico de cada um, a pré-rolagem (tocar do meio == o meio de tocar do
//  começo), a independência do tamanho do bloco e preview == export.
//
//  AUREA_FX_DUMP=<pasta>: grava um WAV antes/depois de cada efeito lá.
// =============================================================================
#include "TestFramework.hpp"
#include "SyntheticVideo.hpp"

#include "aurea/Engine.hpp"
#include "aurea/audio/Audio.hpp"
#include "aurea/audio/AudioEffects.hpp"
#include "aurea/command/Command.hpp"
#include "aurea/effects/EffectRegistry.hpp"
#include "aurea/expr/Expression.hpp"
#include "aurea/project/Project.hpp"
#include "aurea/timeline/Composition.hpp"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>
#include <vector>

using namespace aurea;
using namespace aurea::test;

namespace {

constexpr f64 kPiFx = 3.14159265358979323846;
constexpr i64 kSr = 48000;

class FetchBlocksFx final : public audio::BlockSource {
public:
    explicit FetchBlocksFx(audio::AudioBlockCache& c) : cache_(c) {}
    const audio::AudioBlock* block(u64 asset, i64 b) override {
        auto p = cache_.fetch(asset, b);
        if (!p) return nullptr;
        held_.push_back(p);
        if (held_.size() > 64) held_.erase(held_.begin());
        return p.get();
    }

private:
    audio::AudioBlockCache& cache_;
    std::vector<std::shared_ptr<const audio::AudioBlock>> held_;
};

SyntheticConfig sine_cfg() {
    SyntheticConfig c;
    c.audioRate = 48000;
    c.audioChannels = 2;
    c.audioFreq = 440.0;
    c.audioSeconds = 10.0;
    return c;
}

/// Um estágio com os valores na ordem dos parâmetros.
audio::FxStage stage(audio::FxKind kind, std::initializer_list<f32> values, f32 maxDelayMs = 0.0f) {
    audio::FxStage s;
    s.kind = kind;
    for (f32 v : values) {
        audio::FxParam p;
        p.value = v;
        s.params.push_back(p);
    }
    s.maxDelayMs = maxDelayMs;
    return s;
}

/// Clipe da fonte sintética em [0, len) com a cadeia `own` + envelope.
audio::AudioClip fx_clip(i64 len, std::vector<audio::FxStage> own, u64 key = 1) {
    audio::AudioClip c;
    c.asset = 9;
    c.start = 0;
    c.end = len;
    c.sourceLength = 10 * kSr;
    c.fadeFrom = 0;
    c.fadeTo = len;
    c.chain = std::move(own);
    audio::FxStage env;
    env.kind = audio::FxKind::Envelope;
    c.chain.push_back(env);
    c.revFrom = 0;
    c.revTo = len;
    c.preroll = audio::chain_preroll(c.chain);
    c.streamKey = key;
    c.fxHash = 0xC0FFEE + key;
    return c;
}

audio::AudioClip dry_clip(i64 len) {
    audio::AudioClip c;
    c.asset = 9;
    c.start = 0;
    c.end = len;
    c.sourceLength = 10 * kSr;
    c.fadeFrom = 0;
    c.fadeTo = len;
    return c;
}

struct Rig {
    SyntheticConfig cfg;
    SyntheticFactory factory;
    audio::AudioBlockCache cache;
    FetchBlocksFx blocks;
    explicit Rig(SyntheticConfig c = sine_cfg())
        : cfg(c), factory(cfg), cache(&factory, 64ull << 20, false), blocks(cache) {
        cache.register_asset(9, audio::AudioAssetRef{"s", 10 * kSr});
    }
    /// Mixa [start, start+frames) em pedaços de `chunk` com um estado só.
    std::vector<f32> render(const audio::AudioMixSnapshot& snap, i64 start, i64 frames, u32 chunk = 4096,
                            audio::MixState* state = nullptr) {
        std::vector<f32> out(static_cast<usize>(frames) * 2);
        audio::MixState local;
        audio::MixState* st = state ? state : &local;
        for (i64 at = 0; at < frames;) {
            const u32 n = static_cast<u32>(std::min<i64>(chunk, frames - at));
            audio::mix(snap, start + at, n, blocks, out.data() + at * 2, nullptr, st);
            at += n;
        }
        return out;
    }
};

audio::AudioMixSnapshot snap_of(audio::AudioClip c) {
    audio::AudioMixSnapshot s;
    s.endSample = c.end;
    s.clips.push_back(std::move(c));
    return s;
}

f64 rms(const std::vector<f32>& pcm, u32 ch, i64 a, i64 b) {
    f64 acc = 0.0;
    for (i64 i = a; i < b; ++i) acc += static_cast<f64>(pcm[static_cast<usize>(i) * 2 + ch]) * pcm[static_cast<usize>(i) * 2 + ch];
    return std::sqrt(acc / static_cast<f64>(std::max<i64>(1, b - a)));
}

u32 zero_crossings(const std::vector<f32>& pcm, u32 ch, i64 a, i64 b) {
    u32 n = 0;
    for (i64 i = a + 1; i < b; ++i) {
        const f32 p = pcm[static_cast<usize>(i - 1) * 2 + ch], q = pcm[static_cast<usize>(i) * 2 + ch];
        if ((p < 0.0f) != (q < 0.0f)) ++n;
    }
    return n;
}

/// WAV 16 bits estéreo 48 kHz (só com AUREA_FX_DUMP).
void dump_wav(const char* name, const std::vector<f32>& pcm) {
    const char* dir = std::getenv("AUREA_FX_DUMP");
    if (!dir || !*dir) return;
    const std::string path = std::string(dir) + "/" + name + ".wav";
    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return;
    std::vector<i16> s(pcm.size());
    audio::to_pcm16(pcm.data(), pcm.size(), s.data());
    const u32 data = static_cast<u32>(s.size() * 2);
    auto u32le = [&](u32 v) { std::fwrite(&v, 4, 1, f); };
    auto u16le = [&](u16 v) { std::fwrite(&v, 2, 1, f); };
    std::fwrite("RIFF", 1, 4, f); u32le(36 + data); std::fwrite("WAVE", 1, 4, f);
    std::fwrite("fmt ", 1, 4, f); u32le(16); u16le(1); u16le(2); u32le(48000); u32le(48000 * 4); u16le(4); u16le(16);
    std::fwrite("data", 1, 4, f); u32le(data);
    std::fwrite(s.data(), 2, s.size(), f);
    std::fclose(f);
}

} // namespace

// -----------------------------------------------------------------------------
// Cada efeito
// -----------------------------------------------------------------------------
AUREA_TEST(AudioFx, DelayEchoesAtTheDelayTimeWithFeedback) {
    Rig rig;
    // 100 ms, quantidade 50 %, sem realimentação, seca e molhada 100 %:
    // y(t) = x(t) + 0,5·x(t − 4800).
    auto snap = snap_of(fx_clip(kSr, {stage(audio::FxKind::Delay, {100, 50, 0, 100, 100}, 100)}));
    const auto out = rig.render(snap, 0, kSr);
    for (i64 t : {100, 4799, 4800, 9000, 30000}) {
        const f64 want = synthetic_audio_value(rig.cfg, 0, t / 48000.0)
                       + (t >= 4800 ? 0.5 * synthetic_audio_value(rig.cfg, 0, (t - 4800) / 48000.0) : 0.0);
        AUREA_CHECK_NEAR(out[static_cast<usize>(t) * 2], want, 1e-4);
    }
    // Realimentação 50 %: o segundo eco (t − 2D) entra com 0,5 · 0,5.
    snap = snap_of(fx_clip(kSr, {stage(audio::FxKind::Delay, {100, 50, 50, 0, 100}, 100)}));
    const auto echo = rig.render(snap, 0, kSr);
    const i64 t = 12000;
    const f64 want = 0.5 * (synthetic_audio_value(rig.cfg, 0, (t - 4800) / 48000.0)
                          + 0.5 * synthetic_audio_value(rig.cfg, 0, (t - 9600) / 48000.0));
    AUREA_CHECK_NEAR(echo[static_cast<usize>(t) * 2], want, 1e-4);
    AUREA_CHECK_NEAR(echo[100 * 2], 0.0, 1e-6);   // seca 0: antes do primeiro eco, silêncio
}

AUREA_TEST(AudioFx, HighLowPassAttenuatesBeyondTheCutoff) {
    Rig rig;
    const auto dry = rig.render(snap_of(dry_clip(kSr)), 0, kSr);
    const f64 dryL = rms(dry, 0, 12000, kSr), dryR = rms(dry, 1, 12000, kSr);
    // Passa-baixa 200 Hz: 440 Hz cai ~−14 dB, 880 Hz ~−26 dB.
    auto lp = rig.render(snap_of(fx_clip(kSr, {stage(audio::FxKind::HighLowPass, {1, 200, 0, 100})})), 0, kSr);
    AUREA_CHECK(rms(lp, 0, 12000, kSr) < 0.25 * dryL);
    AUREA_CHECK(rms(lp, 1, 12000, kSr) < 0.07 * dryR);
    // Passa-baixa 8 kHz: os dois passam (> −0,5 dB).
    lp = rig.render(snap_of(fx_clip(kSr, {stage(audio::FxKind::HighLowPass, {1, 8000, 0, 100})})), 0, kSr);
    AUREA_CHECK(rms(lp, 0, 12000, kSr) > 0.94 * dryL);
    // Passa-alta 2 kHz: 440 Hz cai muito; 880 Hz menos (−14 dB).
    const auto hp = rig.render(snap_of(fx_clip(kSr, {stage(audio::FxKind::HighLowPass, {0, 2000, 0, 100})})), 0, kSr);
    AUREA_CHECK(rms(hp, 0, 12000, kSr) < 0.06 * dryL);
    AUREA_CHECK(rms(hp, 1, 12000, kSr) < 0.25 * dryR);
    AUREA_CHECK(rms(hp, 1, 12000, kSr) > rms(hp, 0, 12000, kSr));
    // A resposta do filtro que toca é a do gráfico: −3 dB no corte.
    AUREA_CHECK_NEAR(audio::biquad_response_db(audio::pass_filter(false, 1000), 1000), -3.01, 0.05);
    AUREA_CHECK_NEAR(audio::biquad_response_db(audio::pass_filter(true, 1000), 1000), -3.01, 0.05);
}

AUREA_TEST(AudioFx, BackwardsPlaysTheLayerFromItsEndAndSwapsChannels) {
    Rig rig;
    auto c = fx_clip(kSr, {stage(audio::FxKind::Backwards, {0})});
    c.reverse = true;
    auto out = rig.render(snap_of(c), 0, kSr);
    for (i64 t : {i64{0}, i64{1}, i64{777}, i64{24000}, kSr - 1}) {
        AUREA_CHECK_NEAR(out[static_cast<usize>(t) * 2], synthetic_audio_value(rig.cfg, 0, (kSr - 1 - t) / 48000.0), 1e-6);
        AUREA_CHECK_NEAR(out[static_cast<usize>(t) * 2 + 1], synthetic_audio_value(rig.cfg, 1, (kSr - 1 - t) / 48000.0), 1e-6);
    }
    // Trocar canais: o esquerdo toca o direito.
    c = fx_clip(kSr, {stage(audio::FxKind::Backwards, {1})});
    c.reverse = true;
    out = rig.render(snap_of(c), 0, kSr);
    AUREA_CHECK_NEAR(out[1000 * 2], synthetic_audio_value(rig.cfg, 1, (kSr - 1 - 1000) / 48000.0), 1e-6);
    AUREA_CHECK_NEAR(out[1000 * 2 + 1], synthetic_audio_value(rig.cfg, 0, (kSr - 1 - 1000) / 48000.0), 1e-6);
}

AUREA_TEST(AudioFx, ToneGeneratesTheRequestedFrequenciesWithoutASource) {
    Rig rig;
    for (u32 shape = 0; shape < 4; ++shape) {
        auto c = fx_clip(kSr, {stage(audio::FxKind::Tone, {static_cast<f32>(shape), 1000, 0, 0, 0, 0, 50})});
        c.asset = 0;           // sólido: nada a ler
        c.sourceLength = 0;
        const auto out = rig.render(snap_of(c), 0, kSr);
        // 1 kHz → 2000 cruzamentos por zero em 1 s (±2 nas pontas).
        const u32 zc = zero_crossings(out, 0, 0, kSr);
        AUREA_CHECK(zc >= 1996 && zc <= 2002);
        f32 peak = 0.0f;
        for (f32 v : out) peak = std::max(peak, std::fabs(v));
        AUREA_CHECK(peak > 0.45f && peak < 0.6f);   // nível 50 %
    }
    // Acorde: dois tons de 305 e 705 Hz (no meio das faixas de 10 Hz) somam;
    // o FFT acha os dois.
    auto c = fx_clip(kSr, {stage(audio::FxKind::Tone, {0, 305, 705, 0, 0, 0, 20})});
    c.asset = 0;
    c.sourceLength = 0;
    const auto out = rig.render(snap_of(c), 0, kSr);
    std::vector<f32> mono(8192);
    for (u32 i = 0; i < 8192; ++i) mono[i] = out[(10000 + i) * 2];
    std::vector<f32> bands(100);
    audio::analyze_linear_bands(mono.data(), 8192, 0, 1000, 100, false, bands.data());
    AUREA_CHECK(bands[30] > 0.15f && bands[70] > 0.15f);     // ~0,2 cada (nível 20 %)
    AUREA_CHECK(bands[50] < 0.02f);
}

AUREA_TEST(AudioFx, StereoMixerFollowsLevelPanAndPhase) {
    Rig rig;
    // Os dois canais no centro: mono = média.
    auto out = rig.render(snap_of(fx_clip(kSr, {stage(audio::FxKind::StereoMixer, {100, 100, 0, 0, 0})})), 0, kSr);
    const i64 t = 5000;
    const f64 l = synthetic_audio_value(rig.cfg, 0, t / 48000.0), r = synthetic_audio_value(rig.cfg, 1, t / 48000.0);
    AUREA_CHECK_NEAR(out[t * 2], 0.5 * l + 0.5 * r, 1e-5);
    AUREA_CHECK_NEAR(out[t * 2 + 1], 0.5 * l + 0.5 * r, 1e-5);
    // Esquerdo a 50 % todo à direita, direito a 200 % todo à esquerda, invertido.
    out = rig.render(snap_of(fx_clip(kSr, {stage(audio::FxKind::StereoMixer, {50, 200, 100, -100, 1})})), 0, kSr);
    AUREA_CHECK_NEAR(out[t * 2], -2.0 * r, 1e-5);
    AUREA_CHECK_NEAR(out[t * 2 + 1], -0.5 * l, 1e-5);
    // O padrão é identidade.
    out = rig.render(snap_of(fx_clip(kSr, {stage(audio::FxKind::StereoMixer, {100, 100, -100, 100, 0})})), 0, kSr);
    AUREA_CHECK_NEAR(out[t * 2], l, 1e-6);
    AUREA_CHECK_NEAR(out[t * 2 + 1], r, 1e-6);
}

AUREA_TEST(AudioFx, ModulatorAddsVibratoAndTremolo) {
    Rig rig;
    // Só amplitude (100 %, 2 Hz): o nível vai a zero no vale e volta.
    auto out = rig.render(snap_of(fx_clip(kSr, {stage(audio::FxKind::Modulator, {0, 2, 0, 100}, 1)})), 0, kSr);
    const f64 atPeak = rms(out, 0, 0, 1200);          // fase 0: ganho 1
    const f64 atValley = rms(out, 0, 11400, 12600);   // fase 0,5 (t = 0,25 s): ganho 0
    AUREA_CHECK(atPeak > 0.3);
    AUREA_CHECK(atValley < 0.03);
    // Só frequência (10 %, 1 Hz): cruzamentos por zero sobem e descem ±10 %.
    audio::FxStage fm = stage(audio::FxKind::Modulator, {0, 1, 10, 0}, 60);
    out = rig.render(snap_of(fx_clip(2 * kSr, {fm})), 0, 2 * kSr);
    // Seno do atraso = A(1 − cos): a frequência cai no primeiro quarto e sobe no terceiro.
    const u32 slow = zero_crossings(out, 0, 12000 - 2400, 12000 + 2400);
    const u32 fast = zero_crossings(out, 0, 36000 - 2400, 36000 + 2400);
    const u32 flat = static_cast<u32>(2 * 440 * 0.1);
    AUREA_CHECK(slow < flat - 4 && slow > flat * 0.85);
    AUREA_CHECK(fast > flat + 4 && fast < flat * 1.15);
}

AUREA_TEST(AudioFx, ParametricEqBoostsOnlyTheBand) {
    Rig rig;
    const auto dry = rig.render(snap_of(dry_clip(kSr)), 0, kSr);
    // Banda 1 em 440 Hz, +4 dB, 20 %; bandas 2 e 3 desligadas (o reforço
    // fica abaixo do limitador do mixer: 0,5 × 1,58).
    auto out = rig.render(snap_of(fx_clip(kSr, {stage(audio::FxKind::ParametricEq,
        {1, 440, 20, 4, 0, 1000, 30, 12, 0, 4000, 30, 12})})), 0, kSr);
    AUREA_CHECK_NEAR(rms(out, 0, 12000, kSr) / rms(dry, 0, 12000, kSr), 1.585, 0.03);   // +4 dB
    AUREA_CHECK(rms(out, 1, 12000, kSr) / rms(dry, 1, 12000, kSr) < 1.1);             // 880 Hz quase igual
    // Corte de −12 dB em 440 Hz: um quarto da amplitude.
    out = rig.render(snap_of(fx_clip(kSr, {stage(audio::FxKind::ParametricEq,
        {1, 440, 20, -12, 0, 1000, 30, 0, 0, 4000, 30, 0})})), 0, kSr);
    AUREA_CHECK_NEAR(rms(out, 0, 12000, kSr) / rms(dry, 0, 12000, kSr), 0.251, 0.01);
    // Corte de −20 dB na banda 3 em 880 Hz (a do direito).
    out = rig.render(snap_of(fx_clip(kSr, {stage(audio::FxKind::ParametricEq,
        {0, 440, 20, 0, 0, 1000, 30, 0, 1, 880, 20, -20})})), 0, kSr);
    AUREA_CHECK_NEAR(rms(out, 1, 12000, kSr) / rms(dry, 1, 12000, kSr), 0.1, 0.01);
    AUREA_CHECK_NEAR(rms(out, 0, 12000, kSr) / rms(dry, 0, 12000, kSr), 1.0, 0.1);
    // O gráfico do painel usa a mesma conta.
    AUREA_CHECK_NEAR(audio::biquad_response_db(audio::eq_band(1000, 30, 9), 1000), 9.0, 0.01);
    AUREA_CHECK(std::fabs(audio::biquad_response_db(audio::eq_band(1000, 30, 9), 100)) < 0.2);
}

AUREA_TEST(AudioFx, FlangeChorusDelaysEachVoice) {
    Rig rig;
    // Uma voz, 5 ms, sem modulação, só molhado: x(t − 240).
    auto out = rig.render(snap_of(fx_clip(kSr, {stage(audio::FxKind::FlangeChorus, {5, 1, 0, 0, 0, 0, 0, 0, 100}, 6)})), 0, kSr);
    for (i64 t : {1000, 20000}) {
        AUREA_CHECK_NEAR(out[t * 2], synthetic_audio_value(rig.cfg, 0, (t - 240) / 48000.0), 1e-4);
    }
    // Inverter fase: o molhado com sinal trocado.
    out = rig.render(snap_of(fx_clip(kSr, {stage(audio::FxKind::FlangeChorus, {5, 1, 0, 0, 0, 1, 0, 0, 100}, 6)})), 0, kSr);
    AUREA_CHECK_NEAR(out[1000 * 2], -synthetic_audio_value(rig.cfg, 0, (1000 - 240) / 48000.0), 1e-4);
    // Com modulação e duas vozes estéreo: muda e continua finito.
    out = rig.render(snap_of(fx_clip(kSr, {stage(audio::FxKind::FlangeChorus, {10, 2, 1, 80, 90, 0, 1, 50, 50}, 30)})), 0, kSr);
    bool finite = true;
    for (f32 v : out) finite = finite && std::isfinite(v);
    AUREA_CHECK(finite);
    AUREA_CHECK(std::fabs(out[20000 * 2] - out[20000 * 2 + 1]) > 1e-4);
}

AUREA_TEST(AudioFx, ReverbLeavesATailThatGrowsWithDecay) {
    Rig rig;
    auto tail = [&](f32 decay) {
        auto c = fx_clip(2 * kSr, {stage(audio::FxKind::Reverb, {100, 75, decay, 50, 100, 100}, 100)});
        c.sourceLength = kSr / 2;   // a fonte acaba em 0,5 s; o clipe segue
        c.preroll = audio::chain_preroll(c.chain);
        const auto out = rig.render(snap_of(c), 0, 2 * kSr);
        return std::pair{rms(out, 0, kSr / 2 + 2400, kSr / 2 + 12000), rms(out, 0, kSr + 12000, kSr + 24000)};
    };
    const auto shortTail = tail(10);
    const auto longTail = tail(90);
    AUREA_CHECK(shortTail.first > 1e-3);                 // há rabo depois da fonte
    AUREA_CHECK(longTail.second > 4.0 * shortTail.second);   // decaimento maior, rabo maior
    // Molhado 0: o sinal seco passa inteiro.
    const auto dry = rig.render(snap_of(dry_clip(kSr)), 0, kSr);
    const auto same = rig.render(snap_of(fx_clip(kSr, {stage(audio::FxKind::Reverb, {100, 75, 50, 50, 100, 0}, 100)})), 0, kSr);
    f64 diff = 0.0;
    for (usize i = 0; i < dry.size(); ++i) diff = std::max<f64>(diff, std::fabs(dry[i] - same[i]));
    AUREA_CHECK(diff < 1e-6);
}

// -----------------------------------------------------------------------------
// Fluxo: blocos, pré-rolagem, preview == export
// -----------------------------------------------------------------------------
namespace {
audio::AudioMixSnapshot heavy_chain_snapshot(i64 len) {
    return snap_of(fx_clip(len, {
        stage(audio::FxKind::Delay, {180, 60, 45, 80, 70}, 180),
        stage(audio::FxKind::FlangeChorus, {4, 2, 0.7f, 60, 45, 0, 1, 70, 40}, 12),
        stage(audio::FxKind::ParametricEq, {1, 300, 40, 6, 1, 3000, 30, -6, 0, 8000, 30, 0}),
        stage(audio::FxKind::Reverb, {80, 80, 40, 30, 80, 30}, 80),
    }));
}
} // namespace

AUREA_TEST(AudioFx, ChainIsIndependentOfTheBlockSize) {
    Rig rig;
    const auto snap = heavy_chain_snapshot(3 * kSr);
    const auto big = rig.render(snap, 0, 2 * kSr, 4096);
    const auto small = rig.render(snap, 0, 2 * kSr, 127);
    const auto odd = rig.render(snap, 0, 2 * kSr, 256);
    AUREA_CHECK(big == small);
    AUREA_CHECK(big == odd);
}

AUREA_TEST(AudioFx, PrerollMakesMidStartMatchPlayingFromTheStart) {
    Rig rig;
    // Longe do começo (9,5 s: a pré-rolagem de até 8 s não alcança o início
    // do clipe) — o estado vem SÓ da pré-rolagem.
    constexpr i64 kAt = 9 * kSr + kSr / 2;
    const auto snap = heavy_chain_snapshot(11 * kSr);
    const auto full = rig.render(snap, 0, kAt + kSr);
    audio::MixState fresh;
    const auto mid = rig.render(snap, kAt, kSr, 4096, &fresh);
    AUREA_CHECK_EQ(fresh.restarts(), 1u);
    AUREA_CHECK(snap.clips[0].preroll < kAt);
    f64 worst = 0.0;
    for (i64 i = 0; i < kSr * 2; ++i) worst = std::max<f64>(worst, std::fabs(mid[i] - full[kAt * 2 + i]));
    std::printf("    pre-rolagem: maior diferenca %.2e\n", worst);
    AUREA_CHECK(worst < 1e-4);
    // Em 16 bits, as amostras saem iguais (ou a um degrau, no arredondamento).
    std::vector<i16> a(static_cast<usize>(kSr) * 2), b(a.size());
    audio::to_pcm16(mid.data(), a.size(), a.data());
    audio::to_pcm16(full.data() + kAt * 2, b.size(), b.data());
    u32 off = 0;
    for (usize i = 0; i < a.size(); ++i) off += std::abs(a[i] - b[i]) > 1 ? 1u : 0u;
    AUREA_CHECK_EQ(off, 0u);
    // Sem pré-rolagem (estado zerado no ponto) o eco de antes falta: o teste
    // acima mede alguma coisa.
    auto cut = heavy_chain_snapshot(11 * kSr);
    cut.clips[0].preroll = 0;
    cut.clips[0].fxHash ^= 1;
    audio::MixState cold;
    const auto noPre = rig.render(cut, kAt, kSr, 4096, &cold);
    f64 worstCold = 0.0;
    for (i64 i = 0; i < kSr * 2; ++i) worstCold = std::max<f64>(worstCold, std::fabs(noPre[i] - full[kAt * 2 + i]));
    AUREA_CHECK(worstCold > 1e-2);
}

AUREA_TEST(AudioFx, RetryAfterMissingBlockAndOverlappingRequestsAreExact) {
    Rig rig;
    const auto snap = heavy_chain_snapshot(3 * kSr);
    const auto ref = rig.render(snap, 0, 2 * kSr);
    // Pedidos sobrepostos (varispeed) e repetidos saem do que já foi processado.
    audio::MixState st;
    std::vector<f32> out(static_cast<usize>(2 * kSr) * 2);
    std::vector<f32> tmp(8192 * 2);
    for (i64 at = 0; at < 2 * kSr; at += 1000) {
        const i64 back = std::max<i64>(0, at - 300);
        const u32 n = static_cast<u32>(std::min<i64>(1300, 2 * kSr - back));
        audio::mix(snap, back, n, rig.blocks, tmp.data(), nullptr, &st);
        audio::mix(snap, back, n, rig.blocks, tmp.data(), nullptr, &st);   // repetição
        for (u32 i = 0; i < n; ++i) {
            out[static_cast<usize>(back + i) * 2] = tmp[i * 2];
            out[static_cast<usize>(back + i) * 2 + 1] = tmp[i * 2 + 1];
        }
    }
    AUREA_CHECK(out == ref);
    AUREA_CHECK_EQ(st.restarts(), 1u);
}

AUREA_TEST(AudioFx, MissingBlockPublishesNothingAndTheRetryIsExact) {
    // Preview: o bloco da pré-rolagem ainda não decodificou. A chamada conta
    // o que falta e não processa nada; quando o bloco chega, a repetição dá o
    // mesmo som de quem tinha tudo desde o começo.
    struct Gated final : audio::BlockSource {
        audio::BlockSource& inner;
        i64 withheld = -1;
        explicit Gated(audio::BlockSource& b) : inner(b) {}
        const audio::AudioBlock* block(u64 asset, i64 b) override { return b == withheld ? nullptr : inner.block(asset, b); }
    };
    Rig rig;
    const auto snap = heavy_chain_snapshot(11 * kSr);
    const i64 at = 9 * kSr;
    const auto ref = rig.render(snap, 0, at + 4096);
    Gated gated(rig.blocks);
    gated.withheld = (at - kSr) / audio::kBlockFrames;   // dentro da pré-rolagem
    audio::MixState st;
    std::vector<f32> out(4096 * 2);
    audio::MixStats stats;
    audio::mix(snap, at, 4096, gated, out.data(), &stats, &st);
    AUREA_CHECK(stats.missingBlocks > 0);
    f32 loud = 0.0f;
    for (f32 v : out) loud = std::max(loud, std::fabs(v));
    AUREA_CHECK_EQ(loud, 0.0f);
    gated.withheld = -1;
    audio::MixStats again;
    audio::mix(snap, at, 4096, gated, out.data(), &again, &st);
    AUREA_CHECK_EQ(again.missingBlocks, 0u);
    f64 worst = 0.0;
    for (usize i = 0; i < out.size(); ++i) worst = std::max<f64>(worst, std::fabs(out[i] - ref[static_cast<usize>(at) * 2 + i]));
    AUREA_CHECK(worst < 1e-4);
}

namespace {
struct FxEngine {
    SyntheticConfig cfg;
    SyntheticFactory factory;
    Engine e;
    u64 layer = 0;
    explicit FxEngine(SyntheticConfig c) : cfg(c), factory(cfg) {
        EngineConfig config;
        config.workerCount = 2;
        config.memoryBudgetBytes = 64ull * 1024 * 1024;
        config.disableAutosave = true;
        config.mediaFactory = &factory;
        AUREA_CHECK(e.initialize(config).ok());
        AUREA_CHECK(e.new_project(64, 36, 30, nullptr).ok());
        VideoImport vi;
        vi.sourcePath = "s";
        vi.displayName = "s";
        auto id = e.import_video(vi);
        AUREA_CHECK(id.ok());
        if (id.ok()) layer = *id;
    }
    ~FxEngine() { e.shutdown(); }
    u32 add(const char* key) {
        Command cmd;
        cmd.type = CommandType::EffectAdd;
        cmd.effect_add.layer = LayerId::unpack(layer);
        cmd.effect_add.effectType = effect_type_id(key);
        cmd.effect_add.index = kInvalidIndex;
        AUREA_CHECK(e.apply_command(cmd).ok());
        const Layer* l = e.project()->timeline().composition(e.project()->timeline().current())->layer(LayerId::unpack(layer));
        return l && !l->effects.empty() ? l->effects.back().id : 0u;
    }
    void set(u32 effect, u32 param, f32 value) {
        Command cmd;
        cmd.type = CommandType::EffectSetParam;
        cmd.effect_param.layer = LayerId::unpack(layer);
        cmd.effect_param.effect = EffectId{effect, 0};
        cmd.effect_param.paramIndex = param;
        cmd.effect_param.value = value;
        AUREA_CHECK(e.apply_command(cmd).ok());
    }
    std::shared_ptr<audio::AudioMixSnapshot> snapshot() {
        auto* comp = e.project()->timeline().composition(e.project()->timeline().current());
        return audio::build_snapshot(*comp, *e.project(), nullptr, nullptr, nullptr);
    }
};
} // namespace

AUREA_TEST(AudioFx, EffectsFromTheStackReachTheSnapshotInOrder) {
    FxEngine fx(sine_cfg());
    const u32 delay = fx.add(audio::fx_keys::kDelay);
    fx.add(audio::fx_keys::kHighLowPass);
    fx.set(delay, audio::fxp::kDelayTime, 250.0f);
    auto snap = fx.snapshot();
    AUREA_CHECK_EQ(snap->clips.size(), static_cast<usize>(1));
    if (snap->clips.empty()) return;
    const audio::AudioClip& c = snap->clips[0];
    AUREA_CHECK_EQ(c.chain.size(), static_cast<usize>(3));   // atraso, filtro, envelope
    if (c.chain.size() == 3) {
        AUREA_CHECK(c.chain[0].kind == audio::FxKind::Delay);
        AUREA_CHECK(c.chain[1].kind == audio::FxKind::HighLowPass);
        AUREA_CHECK(c.chain[2].kind == audio::FxKind::Envelope);
        AUREA_CHECK_NEAR(c.chain[0].at(audio::fxp::kDelayTime, 0), 250.0, 1e-4);
        AUREA_CHECK_NEAR(c.chain[1].at(audio::fxp::kCutoff, 0), 2000.0, 1e-4);
    }
    AUREA_CHECK(c.preroll >= 250 * 48);
    // Reverso marca o clipe; Tom num sólido sem som cria um clipe gerado.
    fx.add(audio::fx_keys::kBackwards);
    snap = fx.snapshot();
    AUREA_CHECK(!snap->clips.empty() && snap->clips[0].reverse);
    auto* comp = fx.e.project()->timeline().composition(fx.e.project()->timeline().current());
    const LayerId solid = comp->add_layer(LayerKind::Shape, "solido");
    comp->layer(solid)->end = FrameIndex{60};
    Layer* sl = comp->layer(solid);
    EffectInstance tone;
    tone.id = sl->alloc_effect_id();
    tone.type = effect_type_id(audio::fx_keys::kTone);
    initialize_instance(tone, *expr::builtin_effects().params(tone.type));
    sl->effects.push_back(std::move(tone));
    snap = fx.snapshot();
    AUREA_CHECK_EQ(snap->clips.size(), static_cast<usize>(2));
    bool generated = false;
    for (const auto& clip : snap->clips) generated = generated || (clip.asset == 0 && !clip.chain.empty());
    AUREA_CHECK(generated);
}

AUREA_TEST(AudioFx, AnimatedToneFrequencyStaysPhaseContinuous) {
    // Frequência com keyframes (200 → 800 Hz em 1 s): a fase é a integral,
    // então não há salto de amostra (clique) nem entre blocos.
    Rig rig;
    auto c = fx_clip(kSr, {stage(audio::FxKind::Tone, {0, 200, 0, 0, 0, 0, 50})});
    c.asset = 0;
    c.sourceLength = 0;
    audio::FxStage& s = c.chain[0];
    s.fps = 30.0;
    s.frame0 = 0;
    s.origin = 0;
    audio::FxParam& f = s.params[audio::fxp::kFreq1];
    f.byFrame.resize(31);
    for (u32 k = 0; k <= 30; ++k) f.byFrame[k] = 200.0f + 600.0f * k / 30.0f;
    f.integral.resize(31);
    f64 acc = 0.0;
    for (u32 k = 0; k <= 30; ++k) { f.integral[k] = acc; if (k < 30) acc += 0.5 * (f.byFrame[k] + f.byFrame[k + 1]) / 30.0; }
    const auto out = rig.render(snap_of(c), 0, kSr, 333);
    f32 jump = 0.0f;
    for (i64 i = 1; i < kSr; ++i) jump = std::max(jump, std::fabs(out[i * 2] - out[(i - 1) * 2]));
    // Maior passo de um seno de 800 Hz a 0,5: 2π·800/48000·0,5 ≈ 0,052.
    AUREA_CHECK(jump < 0.06f);
    // 200→800 Hz lineares: média 500 Hz → ~1000 cruzamentos em 1 s.
    const u32 zc = zero_crossings(out, 0, 0, kSr);
    AUREA_CHECK(zc > 980 && zc < 1020);
}

namespace {
class FakeOut final : public audio::AudioOutput {
public:
    Status open(audio::AudioRenderFn, void*) noexcept override { return OkStatus; }
    Status start() noexcept override { return OkStatus; }
    void stop() noexcept override {}
    void close() noexcept override {}
    bool presented(u64, i64& frames) noexcept override { frames = pulled; return true; }
    u32 latency_frames() const noexcept override { return 0; }
    i64 pulled = 0;
};
} // namespace

AUREA_TEST(AudioFx, PreviewPlaybackMatchesExportBitForBit) {
    SyntheticConfig cfg = sine_cfg();
    cfg.audioBpm = 120.0;   // bumbo: ecos e rabo audíveis
    FxEngine fx(cfg);
    const u32 d = fx.add(audio::fx_keys::kDelay);
    fx.set(d, audio::fxp::kDelayTime, 250.0f);
    fx.add(audio::fx_keys::kFlangeChorus);
    fx.add(audio::fx_keys::kParametricEq);
    const u32 r = fx.add(audio::fx_keys::kReverb);
    fx.set(r, audio::fxp::kReverbWet, 40.0f);
    auto snap = fx.snapshot();
    AUREA_CHECK(!snap->clips.empty() && !snap->clips[0].chain.empty());
    if (snap->clips.empty()) return;
    const i64 frames = kSr * 3 / 2;

    // Export: o caminho de Engine::write_export_audio (pedaços de 4096).
    audio::AudioBlockCache exportCache(&fx.factory, 64ull << 20, false);
    snap = std::make_shared<audio::AudioMixSnapshot>(*snap);
    exportCache.register_asset(snap->clips[0].asset, audio::AudioAssetRef{"s", 10 * kSr});
    FetchBlocksFx blocks(exportCache);
    audio::MixState exportState;
    std::vector<f32> exported(static_cast<usize>(frames) * 2);
    for (i64 at = 0; at < frames;) {
        const u32 n = static_cast<u32>(std::min<i64>(4096, frames - at));
        audio::mix(*snap, at, n, blocks, exported.data() + at * 2, nullptr, &exportState);
        at += n;
    }

    // Preview: o motor de reprodução de verdade (thread do mixer, blocos de 256,
    // cache assíncrono), puxado pelo "alto-falante".
    FakeOut output;
    audio::AudioEngine engine;
    engine.initialize(&fx.factory, &output, 32ull << 20);
    engine.cache()->register_asset(snap->clips[0].asset, audio::AudioAssetRef{"s", 10 * kSr});
    engine.set_snapshot(snap);
    engine.play(0);
    std::vector<f32> played(static_cast<usize>(frames) * 2);
    for (i64 at = 0; at < frames;) {
        const auto t0 = std::chrono::steady_clock::now();
        while (engine.stats().queuedMs < 60 && std::chrono::steady_clock::now() - t0 < std::chrono::seconds(5)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        const u32 n = static_cast<u32>(std::min<i64>(480, frames - at));
        engine.debug_render(played.data() + at * 2, n);
        output.pulled += n;
        at += n;
    }
    AUREA_CHECK_EQ(engine.stats().underruns, 0u);
    engine.shutdown();
    std::vector<i16> a(exported.size()), b(played.size());
    audio::to_pcm16(exported.data(), a.size(), a.data());
    audio::to_pcm16(played.data(), b.size(), b.data());
    AUREA_CHECK(a == b);
    f64 energy = 0.0;
    for (f32 v : exported) energy += std::fabs(v);
    AUREA_CHECK(energy > 100.0);
}

AUREA_TEST(AudioFx, ProjectReopenKeepsAudioEffectsAndLayerReferences) {
    FxEngine fx(sine_cfg());
    const u32 d = fx.add(audio::fx_keys::kDelay);
    fx.set(d, audio::fxp::kDelayFeedback, 70.0f);
    auto* comp = fx.e.project()->timeline().composition(fx.e.project()->timeline().current());
    // Uma camada à frente, a Forma de onda nela apontando para o vídeo.
    const LayerId extra = comp->add_layer(LayerKind::Shape, "a-apagar");
    const LayerId host = comp->add_layer(LayerKind::Shape, "onda");
    comp->remove_layer(extra);   // abre um buraco nos índices: reabrir remapeia
    Layer* h = comp->layer(host);
    EffectInstance wave;
    wave.id = h->alloc_effect_id();
    wave.type = effect_type_id(effect_keys::kAudioWaveform);
    initialize_instance(wave, *expr::builtin_effects().params(wave.type));
    AUREA_CHECK_NEAR(wave.params[0].constant.v[0], -1.0, 1e-6);   // nenhuma camada
    h->effects.push_back(std::move(wave));
    // Escolher a camada pelo índice (o que a UI faz).
    Command cmd;
    cmd.type = CommandType::EffectSetParam;
    cmd.effect_param.layer = host;
    cmd.effect_param.effect = EffectId{h->effects.back().id, 0};
    cmd.effect_param.paramIndex = 0;
    cmd.effect_param.value = static_cast<f32>(LayerId::unpack(fx.layer).index);
    AUREA_CHECK(fx.e.apply_command(cmd).ok());
    AUREA_CHECK_EQ(comp->layer(host)->effects.back().params[0].constant.ref, fx.layer);

    const std::string path = "aurea_efeitos_audio_reabrir.aurea";
    for (const char* suffix : {"", ".bak", ".tmp"}) std::remove((path + suffix).c_str());
    AUREA_CHECK(fx.e.save_project(path.c_str()).ok());
    Engine g;
    EngineConfig config;
    config.workerCount = 2;
    config.memoryBudgetBytes = 64ull * 1024 * 1024;
    config.disableAutosave = true;
    config.mediaFactory = &fx.factory;
    AUREA_CHECK(g.initialize(config).ok());
    AUREA_CHECK(g.load_project(path.c_str()).ok());
    if (!g.project()) return;
    Project& p = *g.project();
    Composition* c = p.timeline().composition(p.timeline().current());
    const Layer* video = nullptr;
    const Layer* onda = nullptr;
    LayerId videoId{};
    c->layers().for_each([&](LayerId id, const Layer& l) {
        if (l.kind == LayerKind::Video) { video = &l; videoId = id; }
        if (l.name == "onda") onda = &l;
    });
    AUREA_CHECK(video && onda);
    if (!video || !onda) return;
    AUREA_CHECK_EQ(video->effects.size(), static_cast<usize>(1));
    AUREA_CHECK_NEAR(video->effects[0].params[audio::fxp::kDelayFeedback].constant.v[0], 70.0, 1e-4);
    AUREA_CHECK_EQ(onda->effects[0].params[0].constant.ref, videoId.pack());
    AUREA_CHECK_NEAR(onda->effects[0].params[0].constant.v[0], static_cast<f32>(videoId.index), 1e-6);
    g.shutdown();
    for (const char* suffix : {"", ".bak", ".tmp"}) std::remove((path + suffix).c_str());
}

// -----------------------------------------------------------------------------
// WAV antes/depois (só com AUREA_FX_DUMP): a mesma fonte por cada efeito.
// -----------------------------------------------------------------------------
AUREA_TEST(AudioFx, DumpBeforeAfterWavs) {
    if (!std::getenv("AUREA_FX_DUMP")) return;
    SyntheticConfig cfg = sine_cfg();
    cfg.audioBpm = 100.0;
    cfg.audioFreq = 220.0;
    Rig rig(cfg);
    const i64 len = 4 * kSr;
    const auto before = rig.render(snap_of(dry_clip(len)), 0, len);
    dump_wav("00_original", before);
    struct Case { const char* name; audio::FxStage s; bool reverse; bool generator; };
    const Case cases[] = {
        {"01_reverso", stage(audio::FxKind::Backwards, {1}), true, false},
        {"02_atraso", stage(audio::FxKind::Delay, {300, 60, 50, 75, 75}, 300), false, false},
        {"03_flange_chorus", stage(audio::FxKind::FlangeChorus, {3, 1, 0.5f, 80, 0, 0, 0, 50, 50}, 6), false, false},
        {"03b_chorus", stage(audio::FxKind::FlangeChorus, {20, 3, 1.2f, 40, 120, 0, 1, 60, 60}, 90), false, false},
        {"04_passa_baixa", stage(audio::FxKind::HighLowPass, {1, 400, 0, 100}), false, false},
        {"04b_passa_alta", stage(audio::FxKind::HighLowPass, {0, 330, 0, 100}), false, false},
        {"05_mixer_estereo", stage(audio::FxKind::StereoMixer, {100, 100, 60, -60, 0}), false, false},
        {"06_modulador", stage(audio::FxKind::Modulator, {0, 5, 3, 60}, 10), false, false},
        {"07_eq_parametrico", stage(audio::FxKind::ParametricEq, {1, 120, 50, 12, 1, 440, 30, -15, 1, 3000, 40, 8}), false, false},
        {"08_reverb", stage(audio::FxKind::Reverb, {120, 80, 60, 40, 70, 45}, 120), false, false},
        {"09_tom", stage(audio::FxKind::Tone, {3, 220, 277.18f, 329.63f, 0, 0, 20}), false, true},
    };
    for (const Case& k : cases) {
        auto c = fx_clip(len, {k.s});
        c.reverse = k.reverse;
        if (k.generator) { c.asset = 0; c.sourceLength = 0; }
        const auto after = rig.render(snap_of(c), 0, len);
        dump_wav(k.name, after);
    }
}
