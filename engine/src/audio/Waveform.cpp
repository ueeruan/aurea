// =============================================================================
//  Aurea / audio / Waveform.cpp — picos multirresolução, calculados uma vez.
// =============================================================================
#include "aurea/audio/Audio.hpp"

#include "aurea/core/Log.hpp"
#include "aurea/core/Thread.hpp"
#include "aurea/core/Time.hpp"

#include <algorithm>
#include <cmath>

namespace aurea::audio {

WaveformCache::WaveformCache(VideoSourceFactory* factory) : factory_(factory) {
    thread_ = std::thread([this] { thread_main(); });
}

WaveformCache::~WaveformCache() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        quit_ = true;
    }
    wake_.notify_all();
    if (thread_.joinable()) thread_.join();
}

void WaveformCache::request(u64 key, const AudioAssetRef& ref) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = entries_.find(key);
        if (it != entries_.end() && it->second.ref.path == ref.path) return;
        Entry e;
        e.ref = ref;
        e.total = std::max<i64>(1, (ref.durationSamples + kBaseSamples - 1) / kBaseSamples);
        e.levels.emplace_back(static_cast<usize>(e.total), u8{0});
        entries_[key] = std::move(e);
        queue_.push_back(key);
    }
    wake_.notify_one();
}

bool WaveformCache::query(u64 key, f64 srcStart, f64 samplesPerBucket, u32 count, u8* out) const {
    std::fill(out, out + count, u8{0});
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = entries_.find(key);
    if (it == entries_.end() || it->second.failed) return false;
    const Entry& e = it->second;
    // Nível: o mais grosso cujo balde ainda cabe no balde pedido.
    u32 level = 0;
    if (e.done) {
        while (level + 1 < e.levels.size()
               && static_cast<f64>(kBaseSamples << (level + 1)) <= samplesPerBucket) {
            ++level;
        }
    }
    const std::vector<u8>& lv = e.levels[level];
    const f64 unit = static_cast<f64>(kBaseSamples << level);
    const i64 n = static_cast<i64>(lv.size());
    const i64 readyAt = e.done ? n : (e.ready >> level);
    for (u32 b = 0; b < count; ++b) {
        const f64 a = (srcStart + b * samplesPerBucket) / unit;
        const f64 z = (srcStart + (b + 1) * samplesPerBucket) / unit;
        i64 i0 = static_cast<i64>(std::floor(a));
        i64 i1 = std::max(i0 + 1, static_cast<i64>(std::ceil(z)));
        i0 = std::max<i64>(0, i0);
        i1 = std::min(i1, readyAt);
        u8 m = 0;
        for (i64 i = i0; i < i1; ++i) m = std::max(m, lv[static_cast<usize>(i)]);
        out[b] = m;
    }
    return true;
}

f32 WaveformCache::progress(u64 key) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = entries_.find(key);
    if (it == entries_.end() || it->second.failed) return -1.0f;
    return it->second.done ? 1.0f : static_cast<f32>(it->second.ready) / static_cast<f32>(it->second.total);
}

void WaveformCache::thread_main() {
    set_current_thread_name("aurea-waveform");
    set_current_thread_priority(ThreadPriority::Background);
    std::unique_lock<std::mutex> lock(mutex_);
    while (!quit_) {
        wake_.wait(lock, [this] { return quit_ || !queue_.empty(); });
        if (quit_) break;
        const u64 key = queue_.front();
        queue_.erase(queue_.begin());
        auto it = entries_.find(key);
        if (it == entries_.end() || it->second.done) continue;
        const AudioAssetRef ref = it->second.ref;
        const i64 total = it->second.total;
        lock.unlock();

        const u64 t0 = monotonic_ns();
        // Decoder próprio (cache sem thread, pequeno): não disputa com o som
        // do preview nem com o export.
        AudioBlockCache blocks(factory_, 2ull << 20, false);
        blocks.register_asset(key, ref);
        constexpr i64 kPerBlock = kBlockFrames / kBaseSamples;
        std::vector<u8> chunk(static_cast<usize>(kPerBlock));
        bool failed = false;
        i64 done = 0;
        u32 sinceBump = 0;
        for (i64 b = 0; done < total; ++b) {
            auto blk = blocks.fetch(key, b);
            if (!blk) {
                failed = b == 0;
                break;
            }
            const i64 n = std::min<i64>(kPerBlock, total - done);
            for (i64 i = 0; i < n; ++i) {
                f32 peak = 0.0f;
                const f32* p = blk->pcm.data() + static_cast<usize>(i * kBaseSamples) * 2;
                for (u32 s = 0; s < kBaseSamples * 2; ++s) peak = std::max(peak, std::fabs(p[s]));
                chunk[static_cast<usize>(i)] = static_cast<u8>(std::lround(255.0f * std::sqrt(std::min(1.0f, peak))));
            }
            {
                std::lock_guard<std::mutex> g(mutex_);
                auto e = entries_.find(key);
                if (e == entries_.end() || quit_) break;
                std::copy(chunk.begin(), chunk.begin() + n, e->second.levels[0].begin() + done);
                done += n;
                e->second.ready = done;
            }
            // A UI redesenha a cada ~2 s de áudio pronto, não a cada bloco.
            if (++sinceBump >= 4) {
                sinceBump = 0;
                generation_.fetch_add(1, std::memory_order_acq_rel);
            }
            if (quit_) break;
        }
        lock.lock();
        auto e = entries_.find(key);
        if (e != entries_.end()) {
            if (failed) {
                e->second.failed = true;
                AUREA_LOG_WARN("waveform: audio ilegivel");
            } else {
                // Níveis: máximo de pares até sobrar um balde.
                auto& levels = e->second.levels;
                while (levels.back().size() > 1) {
                    const std::vector<u8>& prev = levels.back();
                    std::vector<u8> next((prev.size() + 1) / 2);
                    for (usize i = 0; i < next.size(); ++i) {
                        next[i] = std::max(prev[2 * i], 2 * i + 1 < prev.size() ? prev[2 * i + 1] : u8{0});
                    }
                    levels.push_back(std::move(next));
                }
                e->second.done = true;
                e->second.ready = e->second.total;
                AUREA_LOG_INFO("waveform: %.1f s de audio em %.0f ms", static_cast<f64>(total * kBaseSamples) / kMixRate,
                               static_cast<f64>(monotonic_ns() - t0) / 1e6);
            }
        }
        generation_.fetch_add(1, std::memory_order_acq_rel);
    }
}

} // namespace aurea::audio
