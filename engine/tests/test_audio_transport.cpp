// =============================================================================
//  Testes do transporte de áudio sob abuso: play/stop/seek em rajada, saída da
//  plataforma que falha ou morre calada, decoder que quebra no meio.
//
//  Relato de beta: "o som para depois de apertar play algumas vezes e só volta
//  reiniciando o app". A saída de teste aqui roda numa thread própria, como o
//  callback do AAudio / AVAudioEngine, conta quantos quadros NÃO mudos chegam e
//  denuncia start/stop/open/close sobrepostos (chamadas de threads diferentes).
// =============================================================================
#include "TestFramework.hpp"
#include "SyntheticVideo.hpp"

#include "aurea/audio/Audio.hpp"

#include <atomic>
#include <chrono>
#include <cmath>
#include <mutex>
#include <random>
#include <thread>
#include <vector>

using namespace aurea;
using namespace aurea::test;

namespace {

SyntheticConfig transport_cfg() {
    SyntheticConfig c;
    c.audioRate = 48000;
    c.audioChannels = 2;
    c.audioFreq = 440.0;
    c.audioSeconds = 10.0;
    return c;
}

/// "Alto-falante" com thread de callback em tempo real (quadros pelo relógio
/// de parede, como o hardware). Falhas injetáveis:
///  - failOpens: os N primeiros `open` falham (sessão ocupada no boot);
///  - failStartEvery: um a cada N `start` devolve erro;
///  - deadStartEvery: um a cada N `start` "dá certo" e o callback nunca roda
///    (stream desconectado sem callback de erro); morto até close+open.
class ThreadedOutput final : public audio::AudioOutput {
public:
    ThreadedOutput() : thread_([this] { run(); }) {}
    ~ThreadedOutput() override {
        quit_.store(true);
        thread_.join();
    }

    Status open(audio::AudioRenderFn fn, void* ctx) noexcept override {
        Guard g(*this);
        std::lock_guard<std::mutex> lock(cb_);
        ++opens;
        if (failOpens > 0) {
            --failOpens;
            return Status{Errc::IoError, "open falso falhou"};
        }
        fn_ = fn;
        ctx_ = ctx;
        open_ = true;
        dead_ = false;
        return OkStatus;
    }
    Status start() noexcept override {
        Guard g(*this);
        std::lock_guard<std::mutex> lock(cb_);
        ++starts;
        if (!open_) return Status{Errc::InvalidState, "fechada"};
        if (dead_) return Status{Errc::IoError, "desconectado"};
        if (failStartEvery && starts % failStartEvery == 0) return Status{Errc::IoError, "start falso falhou"};
        if (deadStartEvery && starts % deadStartEvery == 0) dead_ = true;
        running_ = true;
        last_ = std::chrono::steady_clock::now();
        return OkStatus;
    }
    void stop() noexcept override {
        Guard g(*this);
        // Pausar um stream leva tempo (AAudio espera sair de PAUSING).
        if (stopDelayMaxMs) {
            thread_local std::mt19937 rng(7);
            std::this_thread::sleep_for(std::chrono::milliseconds(rng() % (stopDelayMaxMs + 1)));
        }
        std::lock_guard<std::mutex> lock(cb_);
        running_ = false;
    }
    void close() noexcept override {
        Guard g(*this);
        std::lock_guard<std::mutex> lock(cb_);
        ++closes;
        running_ = false;
        open_ = false;
        dead_ = false;
        fn_ = nullptr;
    }
    bool presented(u64, i64& frames) noexcept override {
        frames = pulled_.load() - 480;
        return true;
    }
    u32 latency_frames() const noexcept override { return 480; }

    [[nodiscard]] u64 audible() const noexcept { return audible_.load(); }

    u32 failOpens = 0;
    u32 failStartEvery = 0;
    u32 deadStartEvery = 0;
    u32 stopDelayMaxMs = 0;
    std::atomic<u32> opens{0}, starts{0}, closes{0};
    std::atomic<u32> overlaps{0};   ///< chamadas de controle sobrepostas (deve ficar 0)

private:
    struct Guard {
        explicit Guard(ThreadedOutput& o) : o_(o) {
            if (o_.inCall_.fetch_add(1) != 0) o_.overlaps.fetch_add(1);
        }
        ~Guard() { o_.inCall_.fetch_sub(1); }
        ThreadedOutput& o_;
    };

    void run() {
        std::vector<f32> buf(4096 * 2);
        while (!quit_.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
            std::lock_guard<std::mutex> lock(cb_);
            if (!running_ || dead_ || !fn_) continue;
            const auto now = std::chrono::steady_clock::now();
            const i64 us = std::chrono::duration_cast<std::chrono::microseconds>(now - last_).count();
            u32 frames = static_cast<u32>(std::min<i64>(4096, us * 48 / 1000));
            if (frames < 64) continue;
            last_ = now;
            fn_(ctx_, buf.data(), frames);
            u64 loud = 0;
            for (u32 i = 0; i < frames; ++i) {
                if (std::fabs(buf[2 * i]) > 1e-3f || std::fabs(buf[2 * i + 1]) > 1e-3f) ++loud;
            }
            audible_.fetch_add(loud);
            pulled_.fetch_add(frames);
        }
    }

    std::mutex cb_;   ///< o callback roda sob ele: close/stop esperam o callback em voo
    audio::AudioRenderFn fn_ = nullptr;
    void* ctx_ = nullptr;
    bool open_ = false, running_ = false, dead_ = false;
    std::chrono::steady_clock::time_point last_{};
    std::atomic<u64> audible_{0};
    std::atomic<i64> pulled_{0};
    std::atomic<u32> inCall_{0};
    std::atomic<bool> quit_{false};
    std::thread thread_;
};

/// Espera chegar ao alto-falante ao menos `frames` quadros não mudos.
bool hears(const ThreadedOutput& out, u64 frames, u32 timeoutMs) {
    const u64 from = out.audible();
    const auto t0 = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() - t0 < std::chrono::milliseconds(timeoutMs)) {
        if (out.audible() - from >= frames) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return false;
}

std::shared_ptr<audio::AudioMixSnapshot> tone_snapshot(audio::AudioEngine& eng, u64 asset, const char* path) {
    auto snap = std::make_shared<audio::AudioMixSnapshot>();
    audio::AudioClip c;
    c.asset = asset;
    c.end = c.sourceLength = c.fadeTo = 10 * 48000;
    snap->clips.push_back(c);
    snap->endSample = c.end;
    eng.cache()->register_asset(asset, audio::AudioAssetRef{path, c.end});
    return snap;
}

void sleep_ms(u32 ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }

} // namespace

// 160 rodadas de play/stop/seek com tempos aleatórios — stop logo depois do
// play (start ainda em voo), seek tocando, stop duplo, troca de snapshot — e o
// som tem de sair depois de TODO play que fica tocando.
AUREA_TEST(AudioTransport, RapidPlayStopSeekKeepsAudioAudible) {
    SyntheticFactory factory(transport_cfg());
    ThreadedOutput out;
    out.stopDelayMaxMs = 12;
    audio::AudioEngine eng;
    eng.initialize(&factory, &out, 32ull << 20);
    auto snap = tone_snapshot(eng, 3, "tom");
    eng.set_snapshot(snap);

    std::mt19937 rng(20261003);
    u32 silentPlays = 0, checkedPlays = 0;
    for (int round = 0; round < 160; ++round) {
        const i64 pos = static_cast<i64>(rng() % 8000) * 1'000'000;
        eng.play(pos);
        switch (rng() % 5) {
            case 0:   // stop com o start ainda em voo
                eng.stop();
                break;
            case 1:   // seek tocando (às vezes duas vezes seguidas)
                sleep_ms(rng() % 8);
                eng.play(static_cast<i64>(rng() % 8000) * 1'000'000);
                if (rng() % 2) eng.play(static_cast<i64>(rng() % 8000) * 1'000'000);
                ++checkedPlays;
                if (!hears(out, 2400, 2000)) ++silentPlays;
                eng.stop();
                break;
            case 2:   // play → stop → play sem esperar nada
                eng.stop();
                eng.play(pos);
                ++checkedPlays;
                if (!hears(out, 2400, 2000)) ++silentPlays;
                sleep_ms(rng() % 20);
                eng.stop();
                eng.stop();
                break;
            default:
                if (rng() % 7 == 0) eng.set_snapshot(snap);   // edição tocando
                ++checkedPlays;
                if (!hears(out, 2400, 2000)) ++silentPlays;
                sleep_ms(rng() % 20);
                eng.stop();
                break;
        }
        if (rng() % 3 == 0) sleep_ms(rng() % 15);
    }
    eng.play(1'000'000'000);
    AUREA_CHECK(hears(out, 9600, 2000));
    AUREA_CHECK(checkedPlays > 60);
    AUREA_CHECK_EQ(silentPlays, 0u);
    AUREA_CHECK_EQ(out.overlaps.load(), 0u);           // start/stop nunca correm juntos
    AUREA_CHECK_EQ(eng.stats().outputRecoveries, 0u);  // saída sadia: o vigia não mexe
    eng.stop();
    eng.shutdown();
}

// Saída que não abre no boot, start que falha e stream que morre calado: o
// motor reabre sozinho e todo play volta a soar — sem reiniciar o app.
AUREA_TEST(AudioTransport, DeadOrFailingOutputIsReopenedOnPlay) {
    SyntheticFactory factory(transport_cfg());
    ThreadedOutput out;
    out.failOpens = 2;        // sessão de áudio ocupada no boot
    out.failStartEvery = 5;   // ERROR_INVALID_STATE / DISCONNECTED no requestStart
    out.deadStartEvery = 7;   // start "ok" e nenhum callback, até reabrir
    out.stopDelayMaxMs = 5;
    audio::AudioEngine eng;
    eng.set_output_watchdog_ms(150);
    eng.initialize(&factory, &out, 32ull << 20);
    AUREA_CHECK(!eng.stats().outputOpen);
    eng.set_snapshot(tone_snapshot(eng, 4, "tom"));

    std::mt19937 rng(77);
    u32 silentPlays = 0;
    for (int round = 0; round < 40; ++round) {
        eng.play(static_cast<i64>(rng() % 8000) * 1'000'000);
        if (!hears(out, 2400, 4000)) ++silentPlays;
        sleep_ms(rng() % 30);
        eng.stop();
        if (rng() % 2) sleep_ms(rng() % 10);
    }
    AUREA_CHECK_EQ(silentPlays, 0u);
    AUREA_CHECK(eng.stats().outputRecoveries > 0);
    AUREA_CHECK(out.opens > 3);
    AUREA_CHECK_EQ(out.overlaps.load(), 0u);

    // Morre no meio de um play longo (sem stop/play do usuário): o vigia
    // percebe o callback parado e o som volta sozinho.
    out.deadStartEvery = 0;
    out.failStartEvery = 0;
    eng.play(0);
    AUREA_CHECK(hears(out, 2400, 2000));
    const u32 startsBefore = out.starts.load();
    out.deadStartEvery = 1;   // a próxima reabertura também nasce morta...
    out.close();              // ...e a atual cai agora (rota trocou)
    const auto t0 = std::chrono::steady_clock::now();
    while (out.starts.load() == startsBefore && std::chrono::steady_clock::now() - t0 < std::chrono::seconds(3)) sleep_ms(1);
    AUREA_CHECK(out.starts.load() > startsBefore);
    out.deadStartEvery = 0;   // depois disso a plataforma volta ao normal
    AUREA_CHECK(hears(out, 2400, 4000));
    eng.stop();
    eng.shutdown();
}

namespace {

/// Decoder que quebra depois de `failAfter` leituras (codec recuperado pelo
/// sistema, AVAssetReader interrompido): a partir daí só devolve erro.
class FlakyDecoder final : public audio::AudioDecoderBackend {
public:
    FlakyDecoder(const SyntheticConfig& c, i32 failAfter) : inner_(c), failAfter_(failAfter) {}
    const audio::AudioStreamInfo& info() const noexcept override { return inner_.info(); }
    Status seek(i64 us) noexcept override {
        if (broken()) return Status{Errc::DecodeFailed, "codec morto"};
        return inner_.seek(us);
    }
    Status read(std::vector<f32>& out, i64& ptsUs, bool& eos) noexcept override {
        out.clear();
        eos = false;
        if (broken()) return Status{Errc::DecodeFailed, "codec morto"};
        ++reads_;
        return inner_.read(out, ptsUs, eos);
    }

private:
    bool broken() const noexcept { return failAfter_ >= 0 && reads_ >= failAfter_; }
    SyntheticAudioDecoder inner_;
    i32 failAfter_;
    i32 reads_ = 0;
};

/// Fábrica: `failOpens` aberturas falham; o primeiro decoder aberto quebra
/// depois de `firstFailsAfter` leituras; o caminho "ruim" nunca abre.
class FlakyFactory final : public VideoSourceFactory {
public:
    explicit FlakyFactory(const SyntheticConfig& c) : cfg(c) {}
    bool probe(const char*, MediaProbe&) override { return false; }
    std::unique_ptr<VideoDecoderBackend> open_video(const Asset&, MediaPriority) override { return nullptr; }
    std::unique_ptr<audio::AudioDecoderBackend> open_audio(const char* path) override {
        ++opens;
        if (path && std::string(path) == "ruim") return nullptr;
        if (failOpens > 0) {
            --failOpens;
            return nullptr;
        }
        const i32 after = firstDecoder ? firstFailsAfter : -1;
        firstDecoder = false;
        return std::make_unique<FlakyDecoder>(cfg, after);
    }
    SyntheticConfig cfg;
    std::atomic<u32> opens{0};
    std::atomic<i32> failOpens{0};
    bool firstDecoder = true;
    i32 firstFailsAfter = -1;
};

} // namespace

// Decoder que quebra no meio da leitura: o bloco seguinte NÃO pode sair mudo
// (nem ficar mudo no cache) — o cache troca o decoder e entrega o som certo.
AUREA_TEST(AudioTransport, BrokenDecoderIsReplacedInsteadOfCachingSilence) {
    const SyntheticConfig cfg = transport_cfg();
    FlakyFactory factory(cfg);
    factory.firstFailsAfter = 30;   // ~0,64 s de 1024 quadros: quebra dentro do bloco 1
    audio::AudioBlockCache cache(&factory, 32ull << 20, false);
    cache.register_asset(9, audio::AudioAssetRef{"tom", 10 * 48000});
    auto b0 = cache.fetch(9, 0);
    auto b1 = cache.fetch(9, 1);
    AUREA_CHECK(b0 && b1);
    if (b1) {
        // Amostra no fim do bloco 1 (≈0,99 s): som de verdade, não silêncio.
        const i64 i = audio::kBlockFrames - 100;
        const f64 t = (audio::kBlockFrames + static_cast<f64>(i)) / 48000.0;
        AUREA_CHECK_NEAR(b1->pcm[static_cast<usize>(2 * i)], synthetic_audio_value(cfg, 0, t), 1e-4);
    }
    AUREA_CHECK(cache.stats().reopened >= 1);
    auto b5 = cache.fetch(9, 5);
    AUREA_CHECK(b5 != nullptr);
    if (b5) AUREA_CHECK_NEAR(b5->pcm[200], synthetic_audio_value(cfg, 0, 2.5 + 100.0 / 48000.0), 1e-4);
}

// Decoder que não abre (codec ocupado) volta numa nova tentativa; e um asset
// ilegível de vez não emudece as OUTRAS camadas do preview.
AUREA_TEST(AudioTransport, UnreadableAssetDoesNotMuteTheMixAndTransientFailureHeals) {
    const SyntheticConfig cfg = transport_cfg();
    FlakyFactory factory(cfg);
    factory.failOpens = 2;
    ThreadedOutput out;
    audio::AudioEngine eng;
    eng.initialize(&factory, &out, 32ull << 20);
    auto snap = std::make_shared<audio::AudioMixSnapshot>();
    for (u64 asset : {11ull, 12ull}) {
        audio::AudioClip c;
        c.asset = asset;
        c.end = c.sourceLength = c.fadeTo = 10 * 48000;
        snap->clips.push_back(c);
        eng.cache()->register_asset(asset, audio::AudioAssetRef{asset == 11 ? "tom" : "ruim", c.end});
    }
    snap->endSample = 10 * 48000;
    eng.set_snapshot(snap);
    eng.play(0);
    // A fonte boa abre na nova tentativa (sem reiniciar) e soa, mesmo com a
    // outra camada ilegível para sempre.
    AUREA_CHECK(hears(out, 9600, 5000));
    eng.stop();
    sleep_ms(30);
    eng.play(3'000'000'000);
    AUREA_CHECK(hears(out, 9600, 3000));
    eng.stop();
    eng.shutdown();
}
