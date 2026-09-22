// =============================================================================
//  Testes do motor de áudio: tempo exato, reamostragem, cache de blocos,
//  mixer (posição, fades, balanço, volume animado, mudo/solo/corte) e o
//  relógio mestre da reprodução.
// =============================================================================
#include "TestFramework.hpp"
#include "SyntheticVideo.hpp"

#include "aurea/Engine.hpp"
#include "aurea/audio/Audio.hpp"
#include "aurea/core/Time.hpp"

#include <chrono>
#include <cmath>
#include <thread>
#include <vector>

using namespace aurea;
using namespace aurea::test;

namespace {

constexpr f64 kTestPi = 3.14159265358979323846;

/// Mixer → cache síncrono (o mesmo caminho do export).
class FetchBlocks final : public audio::BlockSource {
public:
    explicit FetchBlocks(audio::AudioBlockCache& c) : cache_(c) {}
    const audio::AudioBlock* block(u64 asset, i64 b) override {
        auto p = cache_.fetch(asset, b);
        if (!p) return nullptr;
        held_.push_back(p);
        return p.get();
    }

private:
    audio::AudioBlockCache& cache_;
    std::vector<std::shared_ptr<const audio::AudioBlock>> held_;
};

SyntheticConfig audio_cfg(u32 rate, u32 channels = 2) {
    SyntheticConfig c;
    c.audioRate = rate;
    c.audioChannels = channels;
    c.audioFreq = 440.0;
    c.audioSeconds = 10.0;
    return c;
}

/// Saída de teste: o "alto-falante" está `latency` quadros atrás do que foi
/// puxado.
class FakeOutput final : public audio::AudioOutput {
public:
    Status open(audio::AudioRenderFn, void*) noexcept override { return OkStatus; }
    Status start() noexcept override { started = true; return OkStatus; }
    void stop() noexcept override { started = false; }
    void close() noexcept override {}
    bool presented(u64, i64& frames) noexcept override {
        frames = pulled - latency;
        return true;
    }
    u32 latency_frames() const noexcept override { return static_cast<u32>(latency); }
    i64 pulled = 0;
    i64 latency = 512;
    bool started = false;
};

EngineConfig headless() {
    EngineConfig cfg;
    cfg.workerCount = 2;
    cfg.memoryBudgetBytes = 64ull * 1024 * 1024;
    cfg.disableAutosave = true;
    return cfg;
}

void wait_until(auto&& pred, u32 ms = 3000) {
    const auto t0 = std::chrono::steady_clock::now();
    while (!pred() && std::chrono::steady_clock::now() - t0 < std::chrono::milliseconds(ms)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

} // namespace

AUREA_TEST(Audio, TimeToSampleIsExactForIntegerAndNtscRates) {
    AUREA_CHECK_EQ(audio::frame_to_sample(30, 30.0), 48000);
    AUREA_CHECK_EQ(audio::frame_to_sample(1, 25.0), 1920);
    // NTSC em inteiros: nenhuma deriva, nem depois de uma hora.
    AUREA_CHECK_EQ(audio::frame_to_sample(30000, 30000.0 / 1001.0), 48048000);
    AUREA_CHECK_EQ(audio::frame_to_sample(24, 24000.0 / 1001.0), 48048);
    AUREA_CHECK_EQ(audio::frame_to_sample(60, 60000.0 / 1001.0), 48048);
    AUREA_CHECK_EQ(audio::frame_to_sample(107892, 29.97), 107892LL * 48000 * 1001 / 30000);
    AUREA_CHECK_EQ(audio::frame_to_sample(-1, 30.0), -1600);
    AUREA_CHECK_EQ(audio::ns_to_sample(1'000'000'000), 48000);
    AUREA_CHECK_EQ(audio::sample_to_ns(48000), 1'000'000'000);
    AUREA_CHECK_EQ(audio::ns_to_sample(audio::sample_to_ns(123457)), 123457);
}

AUREA_TEST(Audio, ResamplerIsCleanUpAndRejectsAliasesDown) {
    // 44,1 kHz → 48 kHz, senoide de 1 kHz: erro contra a senoide ideal.
    const u32 srcRate = 44100;
    std::vector<f32> src(static_cast<usize>(srcRate) * 2);
    for (u32 i = 0; i < srcRate; ++i) {
        const f32 v = static_cast<f32>(0.5 * std::sin(2.0 * kTestPi * 1000.0 * i / srcRate));
        src[2 * i] = v;
        src[2 * i + 1] = -v;
    }
    const f64 step = static_cast<f64>(srcRate) / 48000.0;
    std::vector<f32> out(24000 * 2);
    audio::resample_to_mix(src.data(), srcRate, 1000.0, step, out.data(), 24000);
    f64 maxErr = 0.0;
    for (u32 n = 0; n < 24000; ++n) {
        const f64 t = (1000.0 + n * step) / srcRate;
        const f64 want = 0.5 * std::sin(2.0 * kTestPi * 1000.0 * t);
        maxErr = std::max({maxErr, std::fabs(out[2 * n] - want), std::fabs(out[2 * n + 1] + want)});
    }
    std::printf("    reamostragem 44,1k->48k: erro maximo %.2e (%.1f dB)\n", maxErr, 20.0 * std::log10(maxErr / 0.5));
    AUREA_CHECK(maxErr < 0.5 * 1e-3);   // abaixo de −60 dB do sinal

    // 96 kHz → 48 kHz com um tom de 30 kHz (acima do novo Nyquist): tem de
    // sumir, não voltar como 18 kHz.
    const u32 hi = 96000;
    std::vector<f32> s2(static_cast<usize>(hi) * 2);
    for (u32 i = 0; i < hi; ++i) {
        const f32 v = static_cast<f32>(0.5 * std::sin(2.0 * kTestPi * 30000.0 * i / hi));
        s2[2 * i] = s2[2 * i + 1] = v;
    }
    std::vector<f32> o2(20000 * 2);
    audio::resample_to_mix(s2.data(), hi, 2000.0, 2.0, o2.data(), 20000);
    f64 rms = 0.0;
    for (f32 v : o2) rms += static_cast<f64>(v) * v;
    rms = std::sqrt(rms / o2.size());
    std::printf("    alias de 30 kHz em 96k->48k: %.1f dB\n", 20.0 * std::log10(rms / (0.5 / std::sqrt(2.0)) + 1e-12));
    AUREA_CHECK(rms < 0.5 / std::sqrt(2.0) * 1e-3);   // < −60 dB
}

AUREA_TEST(Audio, BlockCacheMatchesSourceAndReadsSequentiallyWithoutSeeks) {
    for (u32 rate : {48000u, 44100u}) {
        SyntheticFactory f(audio_cfg(rate));
        audio::AudioBlockCache cache(&f, 64ull << 20, false);
        cache.register_asset(7, audio::AudioAssetRef{"sintetico", 10 * 48000});
        for (i64 b = 0; b < 6; ++b) AUREA_CHECK(cache.fetch(7, b) != nullptr);
        AUREA_CHECK_EQ(cache.stats().seeks, 1u);   // só o primeiro posicionamento
        auto blk = cache.fetch(7, 3);
        f64 maxErr = 0.0;
        for (u32 n = 100; n < audio::kBlockFrames - 100; ++n) {
            const f64 t = static_cast<f64>(3 * audio::kBlockFrames + n) / 48000.0;
            maxErr = std::max(maxErr, static_cast<f64>(std::fabs(blk->pcm[2 * n] - synthetic_audio_value(audio_cfg(rate), 0, t))));
            maxErr = std::max(maxErr, static_cast<f64>(std::fabs(blk->pcm[2 * n + 1] - synthetic_audio_value(audio_cfg(rate), 1, t))));
        }
        std::printf("    bloco a %u Hz: erro maximo %.2e\n", rate, maxErr);
        AUREA_CHECK(maxErr < (rate == 48000 ? 1e-6 : 1e-3));
        // Salto para longe: um seek, e o conteúdo continua certo.
        AUREA_CHECK(cache.fetch(7, 15) != nullptr);
        AUREA_CHECK_EQ(cache.stats().seeks, 2u);
    }
    // Mono vira os dois lados iguais.
    SyntheticFactory fm(audio_cfg(48000, 1));
    audio::AudioBlockCache cm(&fm, 16ull << 20, false);
    cm.register_asset(1, audio::AudioAssetRef{"m", 10 * 48000});
    auto b = cm.fetch(1, 1);
    AUREA_CHECK(b && b->pcm[2 * 500] == b->pcm[2 * 500 + 1] && std::fabs(b->pcm[2 * 500]) > 0.0f);
}

AUREA_TEST(Audio, MixerPlacesClipsWithEqualPowerFadesAndBalance) {
    const SyntheticConfig cfg = audio_cfg(48000);
    SyntheticFactory f(cfg);
    audio::AudioBlockCache cache(&f, 64ull << 20, false);
    cache.register_asset(9, audio::AudioAssetRef{"s", 10 * 48000});
    FetchBlocks blocks(cache);

    audio::AudioMixSnapshot snap;
    audio::AudioClip c;
    c.asset = 9;
    c.start = 48000;           // começa em 1 s
    c.end = 4 * 48000;
    c.sourceAt0 = 24000;       // a partir de 0,5 s da fonte
    c.sourceLength = 10 * 48000;
    c.fadeFrom = c.start;
    c.fadeTo = c.end;
    c.fadeIn = 24000;          // 0,5 s
    snap.clips.push_back(c);
    snap.endSample = 5 * 48000;

    std::vector<f32> out(2 * 48000);
    audio::MixStats st;
    audio::mix(snap, 0, 48000, blocks, out.data(), &st);
    f32 before = 0.0f;
    for (f32 v : out) before = std::max(before, std::fabs(v));
    AUREA_CHECK_EQ(before, 0.0f);          // antes do clipe: silêncio absoluto

    // No meio do fade de entrada: seno(π/4) = −3 dB.
    const i64 mid = c.start + 12000;
    audio::mix(snap, mid, 1, blocks, out.data());
    const f64 src = synthetic_audio_value(cfg, 0, (24000.0 + 12000.0) / 48000.0);
    AUREA_CHECK_NEAR(out[0], src * std::sin(kTestPi / 4.0), 1e-5);
    // Depois do fade: ganho 1, amostra exata da fonte (sem limitador: 0,5 < −1 dBFS).
    const i64 t = c.start + 48000;
    audio::mix(snap, t, 1, blocks, out.data());
    AUREA_CHECK_NEAR(out[0], synthetic_audio_value(cfg, 0, (24000.0 + 48000.0) / 48000.0), 1e-6);
    AUREA_CHECK_NEAR(out[1], synthetic_audio_value(cfg, 1, (24000.0 + 48000.0) / 48000.0), 1e-6);

    // Balanço todo à direita: o lado esquerdo zera, o direito fica inteiro.
    snap.clips[0].pan = 1.0f;
    audio::mix(snap, t, 1, blocks, out.data());
    AUREA_CHECK_EQ(out[0], 0.0f);
    AUREA_CHECK_NEAR(out[1], synthetic_audio_value(cfg, 1, (24000.0 + 48000.0) / 48000.0), 1e-6);

    // Dois clipes somados passando do teto: limitador suave, nunca > 1.
    snap.clips[0].pan = 0.0f;
    snap.clips[0].gain = 3.0f;
    audio::mix(snap, c.start + 24000, 48000, blocks, out.data());
    f32 peak = 0.0f;
    for (f32 v : out) peak = std::max(peak, std::fabs(v));
    AUREA_CHECK(peak <= 1.0f && peak > 0.95f);
}

AUREA_TEST(Audio, SnapshotFollowsTrimMuteSoloAndVolumeKeyframes) {
    SyntheticConfig cfg = audio_cfg(48000);
    SyntheticFactory f(cfg);
    EngineConfig ec = headless();
    ec.mediaFactory = &f;
    Engine e;
    AUREA_CHECK(e.initialize(ec).ok());
    AUREA_CHECK(e.new_project(64, 36, 30.0, nullptr).ok());
    VideoImport vi;
    vi.sourcePath = "s";
    vi.displayName = "s";
    auto id1 = e.import_video(vi);
    auto id2 = e.import_video(vi);
    AUREA_CHECK(id1.ok() && id2.ok());
    const Composition* comp = e.project()->timeline().composition(e.project()->timeline().current());

    auto snap = audio::build_snapshot(*comp, *e.project(), nullptr, nullptr, nullptr);
    AUREA_CHECK_EQ(snap->clips.size(), static_cast<usize>(2));

    // Corte do início: a layer começa em 1 s e o conteúdo em 0,5 s.
    Command r;
    r.type = CommandType::LayerSetTimeRange;
    r.layer_range.layer = LayerId::unpack(*id1);
    r.layer_range.start = FrameIndex{30};
    r.layer_range.end = FrameIndex{90};
    r.layer_range.offset = FrameIndex{15};
    r.layer_range.setOffset = 1;
    AUREA_CHECK(e.apply_command(r).ok());
    // Mudo na segunda.
    Command m;
    m.type = CommandType::AudioSetMuted;
    m.audio_flag.layer = LayerId::unpack(*id2);
    m.audio_flag.flag = true;
    AUREA_CHECK(e.apply_command(m).ok());
    snap = audio::build_snapshot(*comp, *e.project(), nullptr, nullptr, nullptr);
    AUREA_CHECK_EQ(snap->clips.size(), static_cast<usize>(1));
    AUREA_CHECK_EQ(snap->clips[0].start, 48000);
    AUREA_CHECK_EQ(snap->clips[0].end, 3 * 48000);
    AUREA_CHECK_EQ(snap->clips[0].sourceAt0, 24000);

    // Solo na segunda (desmutada): só ela toca.
    m.audio_flag.flag = false;
    AUREA_CHECK(e.apply_command(m).ok());
    Command so;
    so.type = CommandType::AudioSetSolo;
    so.audio_flag.layer = LayerId::unpack(*id2);
    so.audio_flag.flag = true;
    AUREA_CHECK(e.apply_command(so).ok());
    snap = audio::build_snapshot(*comp, *e.project(), nullptr, nullptr, nullptr);
    AUREA_CHECK_EQ(snap->clips.size(), static_cast<usize>(1));
    AUREA_CHECK_EQ(snap->clips[0].start, 0);

    // Volume animado 0 → 1 em 1 s: na metade, 0,5.
    for (auto [time, v] : {std::pair{0, 0.0f}, std::pair{30, 1.0f}}) {
        Command k;
        k.type = CommandType::KeyframeInsert;
        k.keyframe.track.layer = LayerId::unpack(*id2);
        k.keyframe.track.property = TrackProperty::AudioVolume;
        k.keyframe.track.effectIndex = kInvalidIndex;
        k.keyframe.track.effectParamIndex = 0;
        k.keyframe.time = FrameIndex{time};
        k.keyframe.value = v;
        AUREA_CHECK(e.apply_command(k).ok());
    }
    snap = audio::build_snapshot(*comp, *e.project(), nullptr, nullptr, nullptr);
    AUREA_CHECK(!snap->clips[0].volumeByFrame.empty());
    audio::AudioBlockCache cache(&f, 16ull << 20, false);
    cache.register_asset(snap->clips[0].asset, audio::AudioAssetRef{"s", 10 * 48000});
    FetchBlocks blocks(cache);
    std::vector<f32> out(2);
    audio::mix(*snap, 24000, 1, blocks, out.data());
    AUREA_CHECK_NEAR(out[0], 0.5 * synthetic_audio_value(cfg, 0, 0.5), 1e-4);

    // Volume parado pelo comando (sem keyframes em outra layer).
    Command vol;
    vol.type = CommandType::AudioSetVolume;
    vol.audio_gain.layer = LayerId::unpack(*id1);
    vol.audio_gain.gain = 0.25f;
    AUREA_CHECK(e.apply_command(vol).ok());
    so.audio_flag.flag = false;
    AUREA_CHECK(e.apply_command(so).ok());
    snap = audio::build_snapshot(*comp, *e.project(), nullptr, nullptr, nullptr);
    bool found = false;
    for (auto& c : snap->clips) found |= c.start == 48000 && std::fabs(c.volume - 0.25f) < 1e-6f;
    AUREA_CHECK(found);
    // Sair do solo devolve a outra camada: solo não grava "mudo" em ninguém.
    AUREA_CHECK_EQ(snap->clips.size(), static_cast<usize>(2));

    // Extrair o áudio: camada de áudio com o mesmo corte, vídeo mudo.
    auto ex = e.extract_audio(*id1);
    AUREA_CHECK(ex.ok());
    snap = audio::build_snapshot(*comp, *e.project(), nullptr, nullptr, nullptr);
    usize fromAudioLayer = 0;
    for (auto& c : snap->clips) fromAudioLayer += c.start == 48000 && c.sourceAt0 == 24000;
    AUREA_CHECK_EQ(fromAudioLayer, static_cast<usize>(1));   // o vídeo não toca mais em dobro
    e.shutdown();
}

AUREA_TEST(Audio, ImportAudioCreatesAudioLayerSizedToTheSound) {
    SyntheticConfig cfg = audio_cfg(44100, 2);
    cfg.width = 0;   // arquivo só de áudio
    cfg.audioSeconds = 4.0;
    SyntheticFactory f(cfg);
    EngineConfig ec = headless();
    ec.mediaFactory = &f;
    Engine e;
    AUREA_CHECK(e.initialize(ec).ok());
    AUREA_CHECK(e.new_project(1920, 1080, 30.0, nullptr).ok());
    VideoImport vi;
    vi.sourcePath = "musica.m4a";
    vi.displayName = "musica";
    auto id = e.import_audio(vi);
    AUREA_CHECK(id.ok());
    const Composition* comp = e.project()->timeline().composition(e.project()->timeline().current());
    const Layer* l = comp->layer(LayerId::unpack(*id));
    AUREA_CHECK(l && l->kind == LayerKind::Audio);
    AUREA_CHECK_EQ(l->end.value, 120);
    AUREA_CHECK_EQ(comp->duration().value, 120);
    auto snap = audio::build_snapshot(*comp, *e.project(), nullptr, nullptr, nullptr);
    AUREA_CHECK_EQ(snap->clips.size(), static_cast<usize>(1));
    AUREA_CHECK_EQ(snap->clips[0].sourceLength, 4 * 48000);
    // Sem trilha de áudio: recusa com motivo, não cria camada vazia.
    SyntheticConfig nv;
    SyntheticFactory f2(nv);
    EngineConfig ec2 = headless();
    ec2.mediaFactory = &f2;
    Engine e2;
    AUREA_CHECK(e2.initialize(ec2).ok());
    AUREA_CHECK(e2.new_project(64, 36, 30.0, nullptr).ok());
    auto bad = e2.import_audio(vi);
    AUREA_CHECK(!bad.ok() && bad.status().code() == Errc::UnsupportedFormat);
    e2.shutdown();
    e.shutdown();
}

AUREA_TEST(Audio, EngineClockFollowsTheSampleAtTheSpeaker) {
    const SyntheticConfig cfg = audio_cfg(48000);
    SyntheticFactory f(cfg);
    FakeOutput out;
    audio::AudioEngine eng;
    eng.initialize(&f, &out, 32ull << 20);
    auto snap = std::make_shared<audio::AudioMixSnapshot>();
    audio::AudioClip c;
    c.asset = 5;
    c.start = 0;
    c.end = 10 * 48000;
    c.sourceLength = 10 * 48000;
    c.fadeTo = c.end;
    snap->clips.push_back(c);
    snap->endSample = c.end;
    eng.cache()->register_asset(5, audio::AudioAssetRef{"s", 10 * 48000});
    for (i64 b = 3; b < 8; ++b) AUREA_CHECK(eng.cache()->fetch(5, b) != nullptr);
    eng.set_snapshot(snap);

    AUREA_CHECK(!eng.available());
    eng.play(2'000'000'000);
    AUREA_CHECK(eng.available() && out.started);
    // Nada saiu ainda: o relógio espera no ponto de partida.
    AUREA_CHECK_EQ(eng.position_ns(), 2'000'000'000);
    wait_until([&] { return eng.stats().queuedMs >= 100; });

    std::vector<f32> buf(480 * 2);
    for (int i = 0; i < 10; ++i) {
        eng.debug_render(buf.data(), 480);
        out.pulled += 480;
        if (i == 0) {
            // A primeira amostra entregue é a de 2 s da fonte.
            AUREA_CHECK_NEAR(buf[0], synthetic_audio_value(cfg, 0, 2.0), 1e-6);
            AUREA_CHECK_NEAR(buf[2 * 100], synthetic_audio_value(cfg, 0, 2.0 + 100.0 / 48000.0), 1e-6);
        }
    }
    // 4800 puxados, 512 ainda no buffer da saída: soa a amostra 96000 + 4288.
    AUREA_CHECK_EQ(eng.position_ns(), audio::sample_to_ns(96000 + 4800 - 512));

    // Seek tocando: o que sobrou no anel é descartado sem lock; a primeira
    // amostra nova é a de 5 s.
    eng.play(5'000'000'000);
    wait_until([&] { return eng.stats().queuedMs >= 100; });
    eng.debug_render(buf.data(), 480);
    out.pulled += 480;
    AUREA_CHECK_NEAR(buf[0], synthetic_audio_value(cfg, 0, 5.0), 1e-6);
    AUREA_CHECK_EQ(eng.position_ns(), 5'000'000'000);   // ainda no buffer da saída
    for (int i = 0; i < 4; ++i) {
        eng.debug_render(buf.data(), 480);
        out.pulled += 480;
    }
    AUREA_CHECK_EQ(eng.position_ns(), audio::sample_to_ns(240000 + 2400 - 512));
    AUREA_CHECK_EQ(eng.stats().underruns, 0u);

    eng.stop();
    AUREA_CHECK(!eng.available() && !out.started);
    eng.shutdown();
}
