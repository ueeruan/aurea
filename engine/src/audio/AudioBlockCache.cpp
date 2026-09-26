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

namespace aurea::audio {

struct AudioBlockCache::Reader {
    u64 assetRevision = 0;
    std::unique_ptr<AudioDecoderBackend> dec;
    AudioStreamInfo info{};
    std::vector<f32> buf;       ///< estéreo, taxa da fonte
    i64 bufStart = 0;           ///< quadro da fonte de buf[0]
    bool bufValid = false;
    bool eos = false;
    u64 seeks = 0;
    std::vector<f32> raw, stereo;

    [[nodiscard]] i64 frames() const noexcept { return static_cast<i64>(buf.size() / 2); }

    /// Garante [from, to) na janela (quadros da fonte). O que o arquivo não
    /// tem (antes do início, depois do fim) fica de fora e vira silêncio.
    void ensure(i64 from, i64 to, i64 margin) {
        const i64 rate = info.sampleRate;
        const bool far = !bufValid || from < bufStart || from > bufStart + frames() + 2 * rate;
        if (far) {
            // Um pouco antes do alvo: o primeiro trecho decodificado pode
            // começar depois do ponto pedido.
            const i64 us = std::max<i64>(0, (from - rate / 50) * 1'000'000 / rate);
            if (!dec->seek(us).ok()) AUREA_LOG_WARN("audio: seek falhou (%lld us)", static_cast<long long>(us));
            buf.clear();
            bufValid = false;
            eos = false;
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
        u32 guard = 0;
        while ((!bufValid || bufStart + frames() < to) && !eos && guard++ < 100000) {
            i64 pts = 0;
            bool end = false;
            raw.clear();
            if (!dec->read(raw, pts, end).ok()) {
                eos = true;
                break;
            }
            if (end) eos = true;
            const u32 ch = std::max(1u, info.channels);
            const i64 n = static_cast<i64>(raw.size() / ch);
            if (n == 0) continue;
            stereo.resize(static_cast<usize>(n) * 2);
            for (i64 i = 0; i < n; ++i) to_stereo(raw.data() + i * ch, ch, stereo[2 * i], stereo[2 * i + 1]);
            const i64 chunkStart = static_cast<i64>(std::llround(static_cast<f64>(pts) * rate / 1e6));
            i64 skip = 0;
            if (!bufValid) {
                bufStart = chunkStart;
                bufValid = true;
            } else {
                const i64 d = chunkStart - (bufStart + frames());
                // Carimbos de AAC/MP3 tremem ±1 quadro no arredondamento: isso
                // é contíguo. Buraco de verdade vira silêncio; sobreposição é
                // cortada.
                if (d > 2) buf.insert(buf.end(), static_cast<usize>(d) * 2, 0.0f);
                else if (d < -2) skip = std::min(-d, n);
            }
            buf.insert(buf.end(), stereo.begin() + skip * 2, stereo.end());
        }
    }
};

AudioBlockCache::AudioBlockCache(VideoSourceFactory* factory, u64 budgetBytes, bool async)
    : factory_(factory), budget_(std::max<u64>(budgetBytes, 8ull * kBlockFrames * kMixChannels * sizeof(f32))),
      async_(async) {
    if (async_) {
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

void AudioBlockCache::want(u64 key, i64 block, i64 urgency) {
    if (!async_ || block < 0) return;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (blocks_.count(Key{key, block})) return;
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

std::shared_ptr<const AudioBlock> AudioBlockCache::fetch(u64 key, i64 block) {
    if (block < 0) return nullptr;
    if (auto b = find(key, block)) return b;
    auto b = decode_block(key, block);
    if (b) insert(Key{key, block}, b);
    return b;
}

std::shared_ptr<const AudioBlock> AudioBlockCache::decode_block(u64 key, i64 block) {
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
    if (!slot) {
        slot = std::make_unique<Reader>();
        slot->assetRevision = revision;
        if (factory_) slot->dec = factory_->open_audio(ref.path.c_str());
        if (slot->dec) slot->info = slot->dec->info();
        if (!slot->dec || slot->info.sampleRate == 0) {
            AUREA_LOG_WARN("audio: trilha ilegivel em '%s'", ref.path.c_str());
            slot->dec.reset();
            std::lock_guard<std::mutex> lock(mutex_);
            ++stats_.failures;
        }
    }
    Reader& r = *slot;
    if (!r.dec) return nullptr;

    const u64 t0 = monotonic_ns();
    const u64 seeksBefore = r.seeks;
    const f64 step = static_cast<f64>(r.info.sampleRate) / kMixRate;
    const i64 mixStart = block * kBlockFrames;
    const f64 srcPos = static_cast<f64>(mixStart) * step;
    const i64 half = resample_half_width(step);
    const i64 from = static_cast<i64>(std::floor(srcPos)) - half - 1;
    const i64 to = static_cast<i64>(std::ceil(static_cast<f64>(mixStart + kBlockFrames) * step)) + half + 1;
    r.ensure(std::max<i64>(0, from), to, half);

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
    ++stats_.decoded;
    stats_.seeks += r.seeks - seeksBefore;
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
