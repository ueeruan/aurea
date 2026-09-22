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
// =============================================================================
#include "aurea/audio/Audio.hpp"

#include "aurea/core/Log.hpp"
#include "aurea/core/Thread.hpp"
#include "aurea/core/Time.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace aurea::audio {
namespace {

/// Mixer → cache: blocos que faltam contam como silêncio e são pedidos.
class CacheBlocks final : public BlockSource {
public:
    explicit CacheBlocks(AudioBlockCache& c) : cache_(c) {}
    const AudioBlock* block(u64 asset, i64 b) override {
        for (auto& h : held_) {
            if (h.asset == asset && h.block == b) return h.ptr.get();
        }
        auto p = cache_.find(asset, b);
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
    outputOpen_ = false;
    if (output_) {
        const Status s = output_->open(&AudioEngine::render_cb, this);
        outputOpen_ = s.ok();
        if (!outputOpen_) AUREA_LOG_WARN("audio: saida nao abriu: %s", s.message().data());
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
    if (output_ && outputOpen_) {
        output_->stop();
        output_->close();
    }
    outputOpen_ = false;
    output_ = nullptr;
    cache_.reset();
    ring_.reset();
}

void AudioEngine::set_snapshot(std::shared_ptr<const AudioMixSnapshot> snap) {
    std::lock_guard<std::mutex> lock(mutex_);
    snap_ = std::move(snap);
}

void AudioEngine::play(i64 ns) {
    if (!ring_) return;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const u64 g = gen_.fetch_add(1, std::memory_order_acq_rel) + 1;
        mixGen_ = g;
        mixPos_ = ns_to_sample(ns);
        playStartNs_.store(sample_to_ns(mixPos_), std::memory_order_release);
        playing_.store(true, std::memory_order_release);
        if (cache_) cache_->clear_wants();
    }
    wake_.notify_all();
    if (output_ && outputOpen_) {
        const Status s = output_->start();
        if (!s.ok()) AUREA_LOG_WARN("audio: start falhou: %s", s.message().data());
    }
}

void AudioEngine::stop() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!playing_.load()) return;
        playing_.store(false, std::memory_order_release);
        gen_.fetch_add(1, std::memory_order_acq_rel);   // o que sobrou no anel é descartado
    }
    if (output_ && outputOpen_) output_->stop();
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
    return outputOpen_ && playing_.load(std::memory_order_acquire);
}

i64 AudioEngine::position_ns() const noexcept {
    const u64 g = gen_.load(std::memory_order_acquire);
    i64 bw = 0, bt = 0;
    u64 bg = 0;
    for (u32 tries = 0; tries < 8; ++tries) {
        const u64 s0 = clockSeq_.load(std::memory_order_acquire);
        if (s0 & 1u) continue;
        bw = baseWritten_.load(std::memory_order_relaxed);
        bt = baseTimeline_.load(std::memory_order_relaxed);
        bg = baseGen_.load(std::memory_order_relaxed);
        if (clockSeq_.load(std::memory_order_acquire) == s0) break;
    }
    // Ainda não saiu nada desta geração: o relógio fica no ponto de partida (o
    // vídeo espera o som, em vez de o som chegar atrasado).
    if (bg != g) return playStartNs_.load(std::memory_order_acquire);
    i64 presented = 0;
    if (!output_ || !output_->presented(monotonic_ns(), presented)) {
        presented = written_.load(std::memory_order_acquire) - (output_ ? output_->latency_frames() : 0);
    }
    return sample_to_ns(bt + std::max<i64>(0, presented - bw));
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
            baseTimeline_.store(c.start + readOffset_, std::memory_order_relaxed);
            baseGen_.store(g, std::memory_order_relaxed);
            clockSeq_.fetch_add(1, std::memory_order_acq_rel);
            needRebase_ = false;
            rebased = true;
        }
        const u32 n = std::min(frames - i, kChunkFrames - readOffset_);
        std::memcpy(out + static_cast<usize>(i) * 2, c.pcm + static_cast<usize>(readOffset_) * 2,
                    static_cast<usize>(n) * 2 * sizeof(f32));
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
    std::unique_lock<std::mutex> lock(mutex_);
    while (!quit_) {
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
        const i64 pos = mixPos_;
        auto snap = snap_;
        mixPos_ += kChunkFrames;
        lock.unlock();

        const u32 h = head_.load(std::memory_order_relaxed);
        Chunk& c = (*ring_)[h % kRingChunks];
        c.gen = g;
        c.start = pos;
        MixStats ms;
        if (snap) {
            mix(*snap, pos, kChunkFrames, blocks, c.pcm, &ms);
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
        if (ms.missingBlocks) missing_.fetch_add(ms.missingBlocks, std::memory_order_relaxed);
        u32 bits;
        std::memcpy(&bits, &ms.peak, sizeof(bits));
        peakBits_.store(bits, std::memory_order_relaxed);
        head_.store(h + 1, std::memory_order_release);
        lock.lock();
    }
}

AudioEngine::Stats AudioEngine::stats() const noexcept {
    Stats s;
    s.outputOpen = outputOpen_;
    s.playing = playing_.load();
    s.underruns = underruns_.load();
    s.missingBlocks = missing_.load();
    const u32 q = head_.load() - tail_.load();
    s.queuedMs = q * kChunkFrames * 1000 / kMixRate;
    const u32 bits = peakBits_.load();
    std::memcpy(&s.peak, &bits, sizeof(bits));
    return s;
}

} // namespace aurea::audio
