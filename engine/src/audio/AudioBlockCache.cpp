// =============================================================================
//  Aurea / audio / AudioBlockCache.cpp
//
//  Um decoder por asset, com uma janela deslizante de PCM da fonte (estéreo,
//  taxa original). Blocos em sequência (o caso do play) reaproveitam a janela
//  e o decoder anda para a frente sem seek; um salto para longe custa um seek
//  e alguns ms de decode.
// =============================================================================
#include "aurea/audio/Audio.hpp"

#include "aurea/core/Log.hpp"
#include "aurea/core/Thread.hpp"
#include "aurea/core/Time.hpp"
#include "aurea/media/MediaManager.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace aurea::audio {

struct AudioBlockCache::Reader {
    u64 assetRevision = 0;
    std::unique_ptr<AudioDecoderBackend> dec;
    AudioStreamInfo info{};
    std::vector<f32> buf;       ///< estéreo, taxa da fonte
    i64 bufStart = 0;           ///< quadro da fonte de buf[0]
    bool bufValid = false;
    bool eos = false;
    /// O decoder devolveu erro (seek ou leitura): o que ele entregar daqui em
    /// diante não vale. MediaCodec recuperado pelo sistema, AVAssetReader
    /// interrompido em segundo plano — nada disso volta sozinho; o cache
    /// descarta o leitor e abre outro.
    bool broken = false;
    u64 seeks = 0;
    std::vector<f32> raw, stereo;
    bool chunkPending = false;
    i64 pendingStart = 0;

    [[nodiscard]] i64 frames() const noexcept { return static_cast<i64>(buf.size() / 2); }

    /// Garante [from, to) na janela (quadros da fonte). O que o arquivo não
    /// tem (antes do início, depois do fim) fica de fora e vira silêncio.
    void ensure(i64 from, i64 to, i64 margin, const std::atomic<bool>* cancel) {
        const i64 rate = info.sampleRate;
        const bool far = !bufValid || from < bufStart || from > bufStart + frames() + 2 * rate;
        if (far) {
            // Um pouco antes do alvo: o primeiro trecho decodificado pode
            // começar depois do ponto pedido.
            const f64 seekUs = std::max<f64>(0, static_cast<f64>(from - rate / 50) * 1e6 / rate);
            if (seekUs >= static_cast<f64>(std::numeric_limits<i64>::max())) { broken = true; return; }
            const i64 us = static_cast<i64>(seekUs);
            if (!dec->seek(us).ok()) {
                AUREA_LOG_WARN("audio: seek falhou (%lld us)", static_cast<long long>(us));
                broken = true;
                return;
            }
            buf.clear();
            bufValid = false;
            eos = false;
            chunkPending = false;
            ++seeks;
        }
        // Descarta o que ficou para trás (com margem para o kernel).
        if (bufValid) {
            const i64 keepFrom = from - margin - 1;
            if (keepFrom > bufStart) {
                const i64 drop = std::min(keepFrom - bufStart, frames());
                buf.erase(buf.begin(), buf.begin() + drop * 2);
                bufStart += drop;
            }
        }
        // Count progress in the decoder's source time, including valid preroll
        // before our requested window. A corrupt source repeating the same PCM
        // used to consume100,000 native reads, then cache padded silence as if
        // decoding had succeeded. Slow but advancing packets remain valid.
        u32 stagnant = 0;
        i64 furthest = std::numeric_limits<i64>::min();
        u64 progressAt = monotonic_ns();
        while ((!bufValid || bufStart + frames() < to) && (!eos || chunkPending)) {
            if (cancel && cancel->load(std::memory_order_acquire)) return;
            if (stagnant >= 64 || (stagnant && monotonic_ns() - progressAt >= 4'000'000'000ull)) {
                broken = true;
                return;
            }
            if (!chunkPending) {
            i64 pts = 0;
            bool end = false;
            raw.clear();
            if (!dec->read(raw, pts, end).ok()) {
                // Antes isto virava "fim do arquivo": os blocos seguintes saíam
                // mudos E ficavam no cache — o trecho nunca mais soava.
                broken = true;
                break;
            }
            if (cancel && cancel->load(std::memory_order_acquire)) return;
            if (end) eos = true;
            const u32 ch = std::max(1u, info.channels);
            const i64 n = static_cast<i64>(raw.size() / ch);
            if (n == 0) { ++stagnant; continue; }
            stereo.resize(static_cast<usize>(n) * 2);
            for (i64 i = 0; i < n; ++i) to_stereo(raw.data() + i * ch, ch, stereo[2 * i], stereo[2 * i + 1]);
            const f64 timestamp = static_cast<f64>(pts) * rate / 1e6;
            if (!std::isfinite(timestamp) || std::abs(timestamp) >= static_cast<f64>(std::numeric_limits<i64>::max() / 4)) {
                broken = true; break;
            }
            pendingStart = static_cast<i64>(std::llround(timestamp));
            chunkPending = true;
            }
            const i64 chunkStart = pendingStart;
            const i64 n = static_cast<i64>(stereo.size() / 2);
            const i64 chunkEnd = chunkStart + n;
            if (chunkEnd > furthest) {
                furthest = chunkEnd; stagnant = 0; progressAt = monotonic_ns();
            } else {
                ++stagnant;
            }
            i64 skip = 0;
            if (!bufValid) {
                // Preserve exact first-PTS alignment for decoder preroll,
                // without retaining an arbitrarily distant timestamp origin.
                bufStart = std::clamp(chunkStart, std::max<i64>(0, from - margin - 1), from);
                bufValid = true;
            }
            {
                const i64 d = chunkStart - (bufStart + frames());
                // Carimbos de AAC/MP3 tremem ±1 quadro no arredondamento: isso
                // é contíguo. Buraco de verdade vira silêncio; sobreposição é
                // cortada.
                if (d > 2) {
                    // Sparse/broken timestamps can be hours apart. Retain the
                    // decoded chunk, but allocate silence only for this block.
                    const i64 fill = std::min(d, std::max<i64>(0, to - (bufStart + frames())));
                    buf.insert(buf.end(), static_cast<usize>(fill) * 2, 0.0f);
                    if (fill < d) break;
                }
                else if (d < -2) skip = std::min(-d, n);
            }
            buf.insert(buf.end(), stereo.begin() + skip * 2, stereo.end());
            chunkPending = false;
        }
    }
};

AudioBlockCache::AudioBlockCache(VideoSourceFactory* factory, u64 budgetBytes, bool async)
    : factory_(factory), budget_(std::max<u64>(budgetBytes, 8ull * kBlockFrames * kMixChannels * sizeof(f32))),
      async_(async) {
    if (async_) {
        // Só o preview usa `find_playable` (o export e as análises não).
        auto mute = std::make_shared<AudioBlock>();
        mute->pcm.assign(static_cast<usize>(kBlockFrames) * kMixChannels, 0.0f);
        silence_ = std::move(mute);
        running_ = true;
        thread_ = std::thread([this] { thread_main(); });
    }
}

AudioBlockCache::~AudioBlockCache() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        running_ = false;
    }
    wake_.notify_all();
    if (thread_.joinable()) thread_.join();
}

void AudioBlockCache::register_asset(u64 key, AudioAssetRef ref) {
    {
        // Caminho de sempre (o snapshot re-registra a cada edição): só o lock curto.
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = assets_.find(key);
        if (it != assets_.end() && it->second.path == ref.path) {
            it->second.durationSamples = ref.durationSamples;
            return;
        }
    }
    std::lock_guard<std::mutex> lock(mutex_);
    assets_[key] = std::move(ref);
    ++assetVersions_[key];
    failed_.erase(key);   // caminho novo: tenta já
    for (auto b = blocks_.begin(); b != blocks_.end();) {
        if (b->first.asset == key) b = blocks_.erase(b); else ++b;
    }
    // The decoder worker replaces its reader lazily. No decoder lock/join here.

}

bool AudioBlockCache::knows(u64 key) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return assets_.count(key) != 0;
}

std::shared_ptr<const AudioBlock> AudioBlockCache::find(u64 key, i64 block) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = blocks_.find(Key{key, block});
    if (it == blocks_.end()) return nullptr;
    it->second.lastUse = ++useClock_;
    return it->second.block;
}

std::shared_ptr<const AudioBlock> AudioBlockCache::find_playable(u64 key, i64 block) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = blocks_.find(Key{key, block});
    if (it != blocks_.end()) {
        it->second.lastUse = ++useClock_;
        return it->second.block;
    }
    // Decoder em falha: silêncio só desta fonte enquanto ele é reaberto; as
    // outras camadas continuam soando.
    if (silence_ && failed_.count(key)) return silence_;
    return nullptr;
}

void AudioBlockCache::note_failure(u64 key, u64 revision) {
    // Sob mutex_.
    Failure& f = failed_[key];
    if (f.revision != revision) f = Failure{};
    f.revision = revision;
    const u32 shift = std::min<u32>(f.streak, 4);
    f.retryAtNs = monotonic_ns() + std::min<u64>(kRetryMinNs << shift, kRetryMaxNs);
    ++f.streak;
    ++stats_.failures;
}

void AudioBlockCache::want(u64 key, i64 block, i64 urgency) {
    if (!async_ || block < 0) return;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (blocks_.count(Key{key, block})) return;
        // Em falha: só pede de novo quando der a hora da próxima tentativa.
        if (auto f = failed_.find(key); f != failed_.end() && monotonic_ns() < f->second.retryAtNs) return;
        for (Want& w : wants_) {
            if (w.key == Key{key, block}) {
                w.urgency = std::min(w.urgency, urgency);
                return;
            }
        }
        if (wants_.size() >= 256) return;
        wants_.push_back(Want{Key{key, block}, urgency});
    }
    wake_.notify_one();
}

void AudioBlockCache::clear_wants() {
    std::lock_guard<std::mutex> lock(mutex_);
    wants_.clear();
}

std::shared_ptr<const AudioBlock> AudioBlockCache::fetch(u64 key, i64 block, const std::atomic<bool>* cancel) {
    if (cancel && cancel->load(std::memory_order_acquire)) return nullptr;
    if (block < 0) return nullptr;
    if (auto b = find(key, block)) return b;
    auto b = decode_block(key, block, cancel);
    if (cancel && cancel->load(std::memory_order_acquire)) return nullptr;
    if (b) insert(Key{key, block}, b);
    return b;
}

std::shared_ptr<const AudioBlock> AudioBlockCache::decode_block(u64 key, i64 block, const std::atomic<bool>* cancel) {
    if (block < 0 || block > (std::numeric_limits<i64>::max() / 4) / kBlockFrames - 1) return nullptr;
    AudioAssetRef ref;
    u64 revision = 0;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = assets_.find(key);
        if (it == assets_.end()) return nullptr;
        ref = it->second;
        revision = assetVersions_[key];
    }
    std::lock_guard<std::mutex> rl(readerMutex_);
    auto& slot = readers_[key];
    if (slot && slot->assetRevision != revision) slot.reset();
    // Um decoder que falhou NÃO fica para sempre: antes, o leitor sem decoder
    // devolvia nulo até reiniciar o app (e o mixer do preview esperava esse
    // bloco para sempre, mudo). Agora ele é descartado e reaberto, com espera
    // crescente entre as tentativas.
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto f = failed_.find(key);
        if (f != failed_.end() && f->second.revision == revision && monotonic_ns() < f->second.retryAtNs) return nullptr;
    }
    if (slot && !slot->dec) slot.reset();
    const u64 t0 = monotonic_ns();
    // Duas tentativas: um erro no meio da leitura (codec recuperado pelo
    // sistema, leitor interrompido) costuma sumir com um decoder novo.
    for (int attempt = 0; attempt < 2; ++attempt) {
        if (cancel && cancel->load(std::memory_order_acquire)) return nullptr;
        if (!slot) {
            slot = std::make_unique<Reader>();
            slot->assetRevision = revision;
            if (factory_) slot->dec = factory_->open_audio(ref.path.c_str());
            if (slot->dec) slot->info = slot->dec->info();
            if (!slot->dec || slot->info.sampleRate == 0 || slot->info.sampleRate > 768000
                || slot->info.channels == 0 || slot->info.channels > 32) {
                AUREA_LOG_WARN("audio: trilha ilegivel em '%s'", ref.path.c_str());
                slot->dec.reset();
                std::lock_guard<std::mutex> lock(mutex_);
                note_failure(key, revision);
                return nullptr;
            }
        }
        Reader& r = *slot;
        const u64 seeksBefore = r.seeks;
        const f64 step = static_cast<f64>(r.info.sampleRate) / kMixRate;
        const i64 mixStart = block * kBlockFrames;
        const f64 srcPos = static_cast<f64>(mixStart) * step;
        if (srcPos >= static_cast<f64>(std::numeric_limits<i64>::max() / 4) - kBlockFrames * step - 64) return nullptr;
        const i64 half = resample_half_width(step);
        const i64 from = static_cast<i64>(std::floor(srcPos)) - half - 1;
        const i64 to = static_cast<i64>(std::ceil(static_cast<f64>(mixStart + kBlockFrames) * step)) + half + 1;
        r.ensure(std::max<i64>(0, from), to, half, cancel);
        if (cancel && cancel->load(std::memory_order_acquire)) {
            // The native read may have advanced before cancellation. Reopen on
            // a later request rather than reuse a window missing that packet.
            slot.reset();
            return nullptr;
        }
        {
            std::lock_guard<std::mutex> lock(mutex_);
            stats_.seeks += r.seeks - seeksBefore;
        }
        if (r.broken) {
            // O que veio deste decoder não vai para o cache (seria silêncio
            // permanente): descarta e tenta com um novo.
            AUREA_LOG_WARN("audio: decoder falhou no bloco %lld de '%s'; reabrindo", static_cast<long long>(block),
                           ref.path.c_str());
            slot.reset();
            std::lock_guard<std::mutex> lock(mutex_);
            ++stats_.reopened;
            if (attempt == 1) {
                note_failure(key, revision);
                return nullptr;
            }
            continue;
        }
        return finish_block(r, key, block, revision, t0);
    }
    return nullptr;
}

std::shared_ptr<const AudioBlock> AudioBlockCache::finish_block(Reader& r, u64 key, i64 block, u64 revision, u64 t0) {
    const f64 step = static_cast<f64>(r.info.sampleRate) / kMixRate;
    const i64 mixStart = block * kBlockFrames;
    const f64 srcPos = static_cast<f64>(mixStart) * step;

    auto out = std::make_shared<AudioBlock>();
    out->assetRevision = revision;
    out->pcm.assign(static_cast<usize>(kBlockFrames) * kMixChannels, 0.0f);
    if (r.bufValid) {
        resample_to_mix(r.buf.data(), r.frames(), srcPos - static_cast<f64>(r.bufStart), step, out->pcm.data(),
                        kBlockFrames);
    }
    // Decoder de aparelho com defeito (NaN/inf) não chega ao alto-falante nem
    // ao export: vira silêncio e fica registrado.
    u32 bad = 0;
    for (f32& v : out->pcm) {
        if (!std::isfinite(v)) { v = 0.0f; ++bad; }
    }
    if (bad) AUREA_LOG_WARN("audio: %u amostras nao finitas zeradas no bloco %lld", bad, static_cast<long long>(block));
    const f64 ms = static_cast<f64>(monotonic_ns() - t0) / 1e6;
    std::lock_guard<std::mutex> lock(mutex_);
    // Decodificou de novo: sai do estado de falha (a fonte volta a soar).
    if (auto f = failed_.find(key); f != failed_.end() && f->second.revision == revision) failed_.erase(f);
    ++stats_.decoded;
    decodeMsTotal_ += ms;
    decodedSeconds_ += static_cast<f64>(kBlockFrames) / kMixRate;
    return out;
}

void AudioBlockCache::wait_for_data(u64 revision) {
    std::unique_lock<std::mutex> lock(mutex_);
    dataReady_.wait_for(lock, std::chrono::milliseconds(10), [&] {
        return !running_ || dataRevision_.load(std::memory_order_acquire) != revision;
    });
}

void AudioBlockCache::insert(const Key& k, std::shared_ptr<const AudioBlock> b) {
    constexpr u64 kBlockBytes = static_cast<u64>(kBlockFrames) * kMixChannels * sizeof(f32);
    std::lock_guard<std::mutex> lock(mutex_);
    if (b->assetRevision != assetVersions_[k.asset]) return;
    blocks_[k] = Entry{std::move(b), ++useClock_};
    dataRevision_.fetch_add(1, std::memory_order_release);
    dataReady_.notify_all();
    // Despejo pelo menos usado: o mixer toca os blocos em ordem de tempo, e o
    // que foi pedido adiante é o mais recente — o passado sai primeiro.
    while (blocks_.size() * kBlockBytes > budget_ && blocks_.size() > 1) {
        auto victim = blocks_.begin();
        for (auto it = blocks_.begin(); it != blocks_.end(); ++it) {
            if (it->second.lastUse < victim->second.lastUse) victim = it;
        }
        blocks_.erase(victim);
    }
}

void AudioBlockCache::thread_main() {
    set_current_thread_name("aurea-audio-dec");
    std::unique_lock<std::mutex> lock(mutex_);
    while (running_) {
        wake_.wait(lock, [this] { return !running_ || !wants_.empty(); });
        if (!running_) break;
        auto best = std::min_element(wants_.begin(), wants_.end(),
                                     [](const Want& a, const Want& b) { return a.urgency < b.urgency; });
        const Key k = best->key;
        wants_.erase(best);
        if (blocks_.count(k)) continue;
        lock.unlock();
        auto b = decode_block(k.asset, k.block);
        if (b) insert(k, std::move(b));
        lock.lock();
    }
}

AudioBlockCache::Stats AudioBlockCache::stats() const {
    std::lock_guard<std::mutex> lock(mutex_);
    Stats s = stats_;
    s.blocks = static_cast<u32>(blocks_.size());
    s.bytes = static_cast<u64>(blocks_.size()) * kBlockFrames * kMixChannels * sizeof(f32);
    s.decodeMsPerSecond = decodedSeconds_ > 0.0 ? static_cast<f32>(decodeMsTotal_ / decodedSeconds_) : 0.0f;
    return s;
}

} // namespace aurea::audio
