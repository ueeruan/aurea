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
    attach(nullptr);
}

void WaveformCache::attach(MemoryManager* memory) noexcept {
    MemoryManager* old = nullptr;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (memory_ == memory) return;
        old = memory_;
        if (old) old->free(MemoryClass::Waveforms, static_cast<usize>(bytes_));
        memory_ = memory;
        if (memory_) memory_->commit(MemoryClass::Waveforms, static_cast<usize>(bytes_));
    }
    // Fora do lock do cache: um trim segura o registro e entra em reclaim().
    if (old) old->unregister_reclaimable(this);
    if (memory) (void)memory->register_reclaimable(this);
}

u64 WaveformCache::budget_locked() const noexcept {
    const u64 b = memory_ ? memory_->budget(MemoryClass::Waveforms) : 0;
    return b ? b : kDefaultBudget;
}

void WaveformCache::account_locked(Entry& e) noexcept {
    u64 now = 0;
    for (const std::vector<u8>& lv : e.levels) now += lv.capacity();
    if (now > e.bytes) {
        bytes_ += now - e.bytes;
        if (memory_) memory_->commit(MemoryClass::Waveforms, static_cast<usize>(now - e.bytes));
    } else if (now < e.bytes) {
        bytes_ -= std::min<u64>(bytes_, e.bytes - now);
        if (memory_) memory_->free(MemoryClass::Waveforms, static_cast<usize>(e.bytes - now));
    }
    e.bytes = now;
}

void WaveformCache::erase_locked(std::unordered_map<u64, Entry>::iterator it) noexcept {
    const u64 b = std::min<u64>(bytes_, it->second.bytes);
    bytes_ -= b;
    if (memory_) memory_->free(MemoryClass::Waveforms, static_cast<usize>(b));
    queue_.erase(std::remove(queue_.begin(), queue_.end(), it->first), queue_.end());
    entries_.erase(it);
    ++evictions_;
}

void WaveformCache::enforce_budget_locked() noexcept {
    const u64 budget = budget_locked();
    const u64 now = monotonic_ns();
    while (bytes_ > budget) {
        auto victim = entries_.end();
        for (auto it = entries_.begin(); it != entries_.end(); ++it) {
            if (activeValid_ && it->first == activeKey_) continue;
            if (it->second.lastQueryNs && now - it->second.lastQueryNs < kRecentNs) continue;
            if (victim == entries_.end() || it->second.lastQueryNs < victim->second.lastQueryNs) victim = it;
        }
        if (victim == entries_.end()) break;   // tudo na tela: não despeja o que se vê
        erase_locked(victim);
    }
}

void WaveformCache::request(u64 key, const AudioAssetRef& ref) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = entries_.find(key);
        if (it != entries_.end() && it->second.ref.path == ref.path) return;
        if (it != entries_.end()) erase_locked(it);   // relink: o antigo não vale mais
        Entry e;
        e.ref = ref;
        e.total = std::max<i64>(1, (ref.durationSamples + kBaseSamples - 1) / kBaseSamples);
        e.levels.emplace_back(static_cast<usize>(e.total), u8{0});
        e.lastQueryNs = monotonic_ns();   // pedida agora = está na tela
        auto [ins, ok] = entries_.emplace(key, std::move(e));
        (void)ok;
        account_locked(ins->second);
        queue_.push_back(key);
        enforce_budget_locked();
    }
    wake_.notify_one();
}

bool WaveformCache::query(u64 key, f64 srcStart, f64 samplesPerBucket, u32 count, u8* out) const {
    std::fill(out, out + count, u8{0});
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = entries_.find(key);
    if (it == entries_.end() || it->second.failed) { ++misses_; return false; }
    ++hits_;
    const Entry& e = it->second;
    e.lastQueryNs = monotonic_ns();
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

void WaveformCache::clear() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        while (!entries_.empty()) erase_locked(entries_.begin());
        queue_.clear();
        ++version_;
    }
    generation_.fetch_add(1, std::memory_order_acq_rel);
}

u64 WaveformCache::bytes() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return bytes_;
}

u32 WaveformCache::entry_count() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return static_cast<u32>(entries_.size());
}

usize WaveformCache::reclaim(usize targetBytes) noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    const u64 before = bytes_;
    const u64 now = monotonic_ns();
    // Só a "antiga": pronta (ou com falha) e sem consulta recente. A que está
    // sendo calculada ou na tela fica — tirar a da tela só a faria voltar.
    for (;;) {
        if (targetBytes != MemoryManager::kReclaimAll && before - bytes_ >= targetBytes) break;
        auto victim = entries_.end();
        for (auto it = entries_.begin(); it != entries_.end(); ++it) {
            if (activeValid_ && it->first == activeKey_) continue;
            if (!it->second.done && !it->second.failed) continue;
            if (it->second.lastQueryNs && now - it->second.lastQueryNs < kRecentNs) continue;
            if (victim == entries_.end() || it->second.lastQueryNs < victim->second.lastQueryNs) victim = it;
        }
        if (victim == entries_.end()) break;
        erase_locked(victim);
    }
    if (before != bytes_) generation_.fetch_add(1, std::memory_order_acq_rel);
    return static_cast<usize>(before - bytes_);
}

bool WaveformCache::metrics(CacheMetrics& out) const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    out.name = "waveform";
    out.cls = MemoryClass::Waveforms;
    out.stage = TrimStage::OldWaveforms;
    out.bytes = bytes_;
    out.budgetBytes = budget_locked();
    out.entries = static_cast<u32>(entries_.size());
    out.hits = hits_;
    out.misses = misses_;
    out.evictions = evictions_;
    out.version = version_;
    return true;
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
        activeKey_ = key;
        activeValid_ = true;
        const u32 version = version_;
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
                // Projeto trocado (clear) ou asset relinkado no meio: para.
                if (e == entries_.end() || quit_ || version != version_) break;
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
        activeValid_ = false;
        auto e = entries_.find(key);
        if (e != entries_.end() && version == version_) {
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
                account_locked(e->second);
                enforce_budget_locked();
                AUREA_LOG_INFO("waveform: %.1f s de audio em %.0f ms", static_cast<f64>(total * kBaseSamples) / kMixRate,
                               static_cast<f64>(monotonic_ns() - t0) / 1e6);
            }
        }
        generation_.fetch_add(1, std::memory_order_acq_rel);
    }
}

} // namespace aurea::audio
