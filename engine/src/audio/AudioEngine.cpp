// =============================================================================
//  Aurea / audio / AudioEngine.cpp — reprodução e relógio mestre.
//
//  Threads:
//    render (motor)   play/stop/set_snapshot; lê position_ns() a cada frame.
//    mixer            mixa blocos de 256 quadros à frente (~130 ms) no anel.
//    áudio (sistema)  callback de tempo real: copia do anel. Sem lock, sem
//                     alocação: o anel é SPSC com índices atômicos.
//
//  Relógio: o callback anota "a amostra X da timeline foi entregue no quadro
//  W da saída". A saída diz quantos quadros já SAÍRAM no alto-falante agora
//  (AAudioStream_getTimestamp); a diferença dá a amostra que está soando. O
//  vídeo mostra o frame dessa amostra — sincronia de lábio medida no ouvido,
//  não no buffer.
//
//  Saída que morre: só a thread do mixer abre/inicia/para/fecha a saída da
//  plataforma (nada de start e stop correndo em threads diferentes). Se o
//  open/start falha, ou se o callback para de rodar com o play pedido
//  (stream desconectado sem aviso, sessão de áudio perdida), a saída é
//  fechada e reaberta com espera crescente — antes ela ficava morta até
//  reiniciar o app. Ao voltar, o som recomeça no ponto em que o transporte
//  está (o relógio do sistema conduziu o vídeo enquanto a saída estava fora).
// =============================================================================
#include "aurea/audio/Audio.hpp"

#include "aurea/core/Log.hpp"
#include "aurea/core/Thread.hpp"
#include "aurea/core/Time.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>

namespace aurea::audio {
namespace {

/// Missing blocks are requested; the mixer retries without consuming timeline samples.
class CacheBlocks final : public BlockSource {
public:
    explicit CacheBlocks(AudioBlockCache& c) : cache_(c) {}
    void reset() { held_.clear(); }
    const AudioBlock* block(u64 asset, i64 b) override {
        for (auto& h : held_) {
            if (h.asset == asset && h.block == b) return h.ptr.get();
        }
        // find_playable: decoder em falha vira silêncio só desta fonte (e é
        // reaberto em segundo plano) em vez de travar o mixer inteiro.
        auto p = cache_.find_playable(asset, b);
        if (!p) {
            cache_.want(asset, b, -1);   // atrasado: na frente de tudo
            return nullptr;
        }
        held_.push_back(Held{asset, b, p});
        if (held_.size() > 16) held_.erase(held_.begin());
        return held_.back().ptr.get();
    }

private:
    struct Held {
        u64 asset;
        i64 block;
        std::shared_ptr<const AudioBlock> ptr;
    };
    AudioBlockCache& cache_;
    std::vector<Held> held_;
};

constexpr u32 kLeadChunks = 24;   ///< ~128 ms mixados à frente
constexpr u64 kOutputRetryFirstNs = 100'000'000ull;   ///< 1ª nova tentativa de abrir/iniciar a saída
constexpr u64 kOutputRetryMaxNs = 2'000'000'000ull;

} // namespace

AudioEngine::~AudioEngine() { shutdown(); }

void AudioEngine::initialize(VideoSourceFactory* factory, AudioOutput* output, u64 cacheBudgetBytes) {
    shutdown();
    cache_ = std::make_unique<AudioBlockCache>(factory, cacheBudgetBytes, true);
    ring_ = std::make_unique<std::array<Chunk, kRingChunks>>();
    head_.store(0);
    tail_.store(0);
    readOffset_ = 0;
    output_ = output;
    outputOpen_.store(false);
    outputStarted_ = false;
    retryAtNs_ = retryDelayNs_ = 0;
    progressWritten_ = 0;
    progressAtNs_ = 0;
    downWhilePlaying_ = false;
    if (output_) {
        const Status s = output_->open(&AudioEngine::render_cb, this);
        outputOpen_.store(s.ok());
        // Não abriu agora (sessão ocupada, app ainda em segundo plano): o
        // mixer tenta de novo no play.
        if (!s.ok()) AUREA_LOG_WARN("audio: saida nao abriu: %s", s.message().data());
    }
    quit_ = false;
    mixer_ = std::thread([this] { mixer_main(); });
}

void AudioEngine::shutdown() {
    if (mixer_.joinable()) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            quit_ = true;
            playing_.store(false);
        }
        wake_.notify_all();
        mixer_.join();
    }
    if (output_ && outputOpen_.load()) {
        output_->stop();
        output_->close();
    }
    outputOpen_.store(false);
    outputStarted_ = false;
    output_ = nullptr;
    cache_.reset();
    ring_.reset();
}

void AudioEngine::set_snapshot(std::shared_ptr<const AudioMixSnapshot> snap, i64 transportNs) {
    std::lock_guard<std::mutex> lock(mutex_);
    snap_ = std::move(snap);
    if (playing_.load(std::memory_order_acquire)) {
        i64 position = position_ns();
        // Beta 08/10: o relógio do som pode estar preso no ponto do play (nada
        // apresentado nesta geração, contador da saída atrás). Reancorar ali
        // fazia o áudio "voltar para o começo" a cada edição tocando.
        constexpr i64 kStaleNs = 250'000'000;
        if (transportNs >= 0 && std::llabs(position - transportNs) > kStaleNs) position = transportNs;
        mixGen_ = gen_.fetch_add(1, std::memory_order_acq_rel) + 1;
        mixPos_ = ns_to_sample(position);
        playStartNs_.store(position, std::memory_order_release);
        cache_->clear_wants();
        wake_.notify_all();
    }
}

void AudioEngine::play(i64 ns, f64 rate) {
    if (!std::isfinite(rate) || rate <= 0.0 || rate > 16.0) return;
    if (!ring_) return;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const u64 g = gen_.fetch_add(1, std::memory_order_acq_rel) + 1;
        mixGen_ = g;
        playbackRate_.store(rate, std::memory_order_release);
        mixPos_ = ns_to_sample(ns);
        playStartNs_.store(sample_to_ns(mixPos_), std::memory_order_release);
        playing_.store(true, std::memory_order_release);
        if (cache_) cache_->clear_wants();
        if (downWhilePlaying_) {
            // Seek com a saída fora: ao voltar, o som parte daqui.
            downSinceNs_ = monotonic_ns();
            downPosNs_ = playStartNs_.load(std::memory_order_relaxed);
        }
    }
    wake_.notify_all();

}

void AudioEngine::stop() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!playing_.load()) return;
        playing_.store(false, std::memory_order_release);
        gen_.fetch_add(1, std::memory_order_acq_rel);   // o que sobrou no anel é descartado
    }
    wake_.notify_all();
}

void AudioEngine::prefetch(i64 ns) {
    std::shared_ptr<const AudioMixSnapshot> snap;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        snap = snap_;
    }
    if (!snap || !cache_) return;
    std::vector<std::pair<u64, i64>> need;
    blocks_needed(*snap, ns_to_sample(ns), kMixRate, need);
    i64 u = 0;
    for (auto& [a, b] : need) cache_->want(a, b, u++);
}

bool AudioEngine::available() const noexcept {
    return outputOpen_.load(std::memory_order_acquire) && playing_.load(std::memory_order_acquire);
}

void AudioEngine::output_down_locked(u64 nowNs, bool closeIt, std::unique_lock<std::mutex>& lock) {
    if (playing_.load(std::memory_order_acquire) && !downWhilePlaying_) {
        downWhilePlaying_ = true;
        downSinceNs_ = nowNs;
        downPosNs_ = position_ns();
    }
    outputStarted_ = false;
    // Fechada, `available()` cai: o relógio do sistema conduz o vídeo até a
    // saída voltar (em vez de o vídeo congelar esperando um som que não vem).
    const bool wasOpen = outputOpen_.exchange(false, std::memory_order_acq_rel);
    if (closeIt && wasOpen) {
        lock.unlock();
        output_->stop();
        output_->close();
        lock.lock();
    }
    retryDelayNs_ = retryDelayNs_ ? std::min(retryDelayNs_ * 2, kOutputRetryMaxNs) : kOutputRetryFirstNs;
    retryAtNs_ = nowNs + retryDelayNs_;
}

bool AudioEngine::service_output(std::unique_lock<std::mutex>& lock) {
    if (!output_) return false;
    const bool want = playing_.load(std::memory_order_acquire);
    const u64 now = monotonic_ns();
    if (!want) {
        downWhilePlaying_ = false;
        retryAtNs_ = retryDelayNs_ = 0;   // o próximo play tenta já
        if (!outputStarted_) return false;
        // Platform start/stop may block. Only this worker calls them;
        // UI/render and the real-time callback never wait for the device.
        lock.unlock();
        output_->stop();
        lock.lock();
        outputStarted_ = false;
        return true;
    }
    if (!outputOpen_.load(std::memory_order_acquire)) {
        if (!downWhilePlaying_) {
            downWhilePlaying_ = true;
            downSinceNs_ = now;
            downPosNs_ = position_ns();
        }
        if (now < retryAtNs_) return false;
        lock.unlock();
        const Status s = output_->open(&AudioEngine::render_cb, this);
        lock.lock();
        if (!s.ok()) {
            AUREA_LOG_WARN("audio: saida nao reabriu: %s", s.message().data());
            output_down_locked(now, false, lock);
            return true;
        }
        AUREA_LOG_INFO("audio: saida reaberta");
        outputOpen_.store(true, std::memory_order_release);
        return true;
    }
    if (!outputStarted_) {
        if (now < retryAtNs_) return false;
        lock.unlock();
        const Status s = output_->start();
        lock.lock();
        if (!s.ok()) {
            // Antes: o mixer marcava a saída como iniciada mesmo assim, e o som
            // só voltava num play futuro — ou nunca (stream desconectado).
            AUREA_LOG_WARN("audio: output start failed: %s", s.message().data());
            recoveries_.fetch_add(1, std::memory_order_relaxed);
            output_down_locked(now, true, lock);
            return true;
        }
        outputStarted_ = true;
        progressWritten_ = written_.load(std::memory_order_acquire);
        progressAtNs_ = monotonic_ns();
        if (downWhilePlaying_ && playing_.load(std::memory_order_acquire)) {
            // A saída volta no ponto em que o transporte está agora: o que o
            // anel guardava do instante em que ela caiu é descartado.
            const f64 rate = playbackRate_.load(std::memory_order_acquire);
            const i64 pos = downPosNs_ + static_cast<i64>(static_cast<f64>(progressAtNs_ - downSinceNs_) * rate);
            mixGen_ = gen_.fetch_add(1, std::memory_order_acq_rel) + 1;
            mixPos_ = static_cast<f64>(ns_to_sample(pos));
            playStartNs_.store(sample_to_ns(ns_to_sample(pos)), std::memory_order_release);
            if (cache_) cache_->clear_wants();
        }
        downWhilePlaying_ = false;
        return true;
    }
    // Vigia: tocando, o callback da plataforma tem de andar.
    const i64 w = written_.load(std::memory_order_acquire);
    if (w != progressWritten_) {
        progressWritten_ = w;
        progressAtNs_ = now;
        retryDelayNs_ = 0;
        return false;
    }
    if (now - progressAtNs_ < watchdogNs_.load(std::memory_order_relaxed)) return false;
    AUREA_LOG_WARN("audio: callback parado ha %llu ms com o play pedido; reabrindo a saida",
                   static_cast<unsigned long long>((now - progressAtNs_) / 1'000'000ull));
    recoveries_.fetch_add(1, std::memory_order_relaxed);
    output_down_locked(now, true, lock);
    return true;
}

i64 AudioEngine::position_ns() const noexcept {
    const u64 g = gen_.load(std::memory_order_acquire);
    i64 bw = 0, bt = 0;
    u64 bg = 0;
    f64 rate = 1.0;
    for (u32 tries = 0; tries < 8; ++tries) {
        const u64 s0 = clockSeq_.load(std::memory_order_acquire);
        if (s0 & 1u) continue;
        bw = baseWritten_.load(std::memory_order_relaxed);
        bt = baseTimeline_.load(std::memory_order_relaxed);
        bg = baseGen_.load(std::memory_order_relaxed);
        rate = baseRate_.load(std::memory_order_relaxed);
        if (clockSeq_.load(std::memory_order_acquire) == s0) break;
    }
    // Ainda não saiu nada desta geração: o relógio fica no ponto de partida (o
    // vídeo espera o som, em vez de o som chegar atrasado).
    if (bg != g) return playStartNs_.load(std::memory_order_acquire);
    i64 presented = 0;
    if (!output_ || !output_->presented(monotonic_ns(), presented)) {
        presented = written_.load(std::memory_order_acquire) - (output_ ? output_->latency_frames() : 0);
    }
    // Hardware keeps consuming silence during an underrun; timeline audio
    // did not advance. Clamp until decoded PCM resumes, then rebase.
    return sample_to_ns(std::min(deliveredEndTimeline_.load(std::memory_order_acquire),
                                  bt + static_cast<i64>(std::max<i64>(0, presented - bw) * rate)));
}

void AudioEngine::render(f32* out, u32 frames) noexcept {
    const u64 g = gen_.load(std::memory_order_acquire);
    const bool play = playing_.load(std::memory_order_acquire);
    const i64 written = written_.load(std::memory_order_relaxed);
    u32 i = 0;
    bool rebased = false;
    while (i < frames && ring_) {
        const u32 t = tail_.load(std::memory_order_relaxed);
        if (t == head_.load(std::memory_order_acquire)) break;
        Chunk& c = (*ring_)[t % kRingChunks];
        if (c.gen != g || !play) {
            tail_.store(t + 1, std::memory_order_release);
            readOffset_ = 0;
            continue;
        }
        if (!rebased && (baseGen_.load(std::memory_order_relaxed) != g || needRebase_)) {
            // Primeira amostra desta geração (ou volta de um buraco): ancora o
            // relógio aqui.
            clockSeq_.fetch_add(1, std::memory_order_acq_rel);
            baseWritten_.store(written + i, std::memory_order_relaxed);
            baseTimeline_.store(static_cast<i64>(c.start + readOffset_ * c.rate), std::memory_order_relaxed);
            baseRate_.store(c.rate, std::memory_order_relaxed);
            deliveredEndTimeline_.store(static_cast<i64>(c.start + readOffset_ * c.rate), std::memory_order_relaxed);
            baseGen_.store(g, std::memory_order_relaxed);
            clockSeq_.fetch_add(1, std::memory_order_acq_rel);
            needRebase_ = false;
            rebased = true;
        }
        const u32 n = std::min(frames - i, kChunkFrames - readOffset_);
        std::memcpy(out + static_cast<usize>(i) * 2, c.pcm + static_cast<usize>(readOffset_) * 2,
                    static_cast<usize>(n) * 2 * sizeof(f32));
        deliveredEndTimeline_.store(static_cast<i64>(c.start + (readOffset_ + n) * c.rate), std::memory_order_release);
        i += n;
        readOffset_ += n;
        if (readOffset_ == kChunkFrames) {
            tail_.store(t + 1, std::memory_order_release);
            readOffset_ = 0;
        }
    }
    if (i < frames) {
        std::memset(out + static_cast<usize>(i) * 2, 0, static_cast<usize>(frames - i) * 2 * sizeof(f32));
        if (play && baseGen_.load(std::memory_order_relaxed) == g) {
            // Buraco no meio do play: o som parou por falta de dado. O relógio
            // é re-ancorado quando o som voltar, e o vídeo espera junto.
            underruns_.fetch_add(1, std::memory_order_relaxed);
            needRebase_ = true;
        }
    }
    written_.store(written + frames, std::memory_order_release);
}

void AudioEngine::mixer_main() {
    set_current_thread_name("aurea-audio-mix");
    set_current_thread_priority(ThreadPriority::Audio);
    CacheBlocks blocks(*cache_);
    std::vector<std::pair<u64, i64>> need;
    u32 sincePrefetch = 1000;
    std::vector<f32> ratePcm; // mixer thread only; bounded by rate <= 16
    // Efeitos de áudio (atraso, reverb...): o fluxo de cada clipe vive aqui,
    // só nesta thread. Pedidos repetidos e sobrepostos saem do que já foi
    // processado; salto no tempo recomeça com pré-rolagem.
    MixState fx;
    std::unique_lock<std::mutex> lock(mutex_);
    while (!quit_) {
        if (service_output(lock)) continue;
        if (!playing_.load(std::memory_order_acquire)) {
            wake_.wait(lock, [this] { return quit_ || playing_.load(); });
            sincePrefetch = 1000;
            continue;
        }
        const u32 queued = head_.load(std::memory_order_relaxed) - tail_.load(std::memory_order_acquire);
        if (queued >= kLeadChunks || queued >= kRingChunks) {
            wake_.wait_for(lock, std::chrono::milliseconds(2));
            continue;
        }
        const u64 g = mixGen_;
        const f64 position = mixPos_;
        const i64 pos = static_cast<i64>(std::floor(position));
        const f64 rate = playbackRate_.load(std::memory_order_acquire);
        auto snap = snap_;
        const u64 dataRevision = cache_->data_revision();
        lock.unlock();

        const u32 h = head_.load(std::memory_order_relaxed);
        Chunk& c = (*ring_)[h % kRingChunks];
        c.gen = g;
        c.start = position;
        c.rate = rate;
        MixStats ms;
        blocks.reset();
        if (snap) {
            if (rate == 1.0) mix(*snap, pos, kChunkFrames, blocks, c.pcm, &ms, &fx);
            else {
                const i64 margin = resample_half_width(rate) + 1;
                const u32 count = static_cast<u32>(std::ceil(kChunkFrames * rate)) + 2 * static_cast<u32>(margin) + 2;
                ratePcm.resize(static_cast<usize>(count) * 2);
                mix(*snap, pos - margin, count, blocks, ratePcm.data(), &ms, &fx);
                if (!ms.missingBlocks) resample_to_mix(ratePcm.data(), count, margin + position - pos, rate, c.pcm, kChunkFrames);
            }
            // Pede adiante: 3 s, os mais próximos primeiro.
            if (++sincePrefetch >= 16) {
                sincePrefetch = 0;
                need.clear();
                blocks_needed(*snap, pos, 3 * kMixRate, need);
                for (auto& [a, b] : need) cache_->want(a, b, b * kBlockFrames - pos);
            }
        } else {
            std::memset(c.pcm, 0, sizeof(c.pcm));
        }
        if (ms.missingBlocks) {
            missing_.fetch_add(ms.missingBlocks, std::memory_order_relaxed);
            cache_->wait_for_data(dataRevision);
            lock.lock();
            continue; // Retry the SAME position. Never publish fabricated PCM.
        }
        lock.lock();
        if (quit_ || !playing_.load(std::memory_order_acquire) || g != mixGen_) continue;
        mixPos_ = position + kChunkFrames * rate;
        u32 bits;
        std::memcpy(&bits, &ms.peak, sizeof(bits));
        peakBits_.store(bits, std::memory_order_relaxed);
        head_.store(h + 1, std::memory_order_release);
    }
}

AudioEngine::Stats AudioEngine::stats() const noexcept {
    Stats s;
    s.outputOpen = outputOpen_.load();
    s.playing = playing_.load();
    s.outputRecoveries = recoveries_.load();
    s.underruns = underruns_.load();
    s.missingBlocks = missing_.load();
    const u32 q = head_.load() - tail_.load();
    s.queuedMs = q * kChunkFrames * 1000 / kMixRate;
    const u32 bits = peakBits_.load();
    std::memcpy(&s.peak, &bits, sizeof(bits));
    return s;
}

} // namespace aurea::audio
