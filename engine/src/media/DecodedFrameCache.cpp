#include "aurea/media/DecodedFrameCache.hpp"

#include <algorithm>
#include <cstdlib>

namespace aurea {

DecodedFrameCache::~DecodedFrameCache() {
    // Primeiro sai do registro (espera um trim em curso terminar), depois
    // devolve os bytes: ninguém mais entra aqui depois do unregister.
    attach(nullptr);
    std::lock_guard<std::mutex> lock(mutex_);
    frames_.clear();
}

void DecodedFrameCache::configure(const Config& c) noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    config_ = c;
    if (config_.maxFrames == 0) config_.maxFrames = 1;
    evict_locked();
}

DecodedFrameCache::Config DecodedFrameCache::config() const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    return config_;
}

void DecodedFrameCache::attach(MemoryManager* memory) noexcept {
    MemoryManager* old = nullptr;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (memory_ == memory) return;
        old = memory_;
        if (old) old->free(MemoryClass::DecodedFrames, static_cast<usize>(stats_.bytes));
        memory_ = memory;
        if (memory_) memory_->commit(MemoryClass::DecodedFrames, static_cast<usize>(stats_.bytes));
    }
    // Registro FORA do lock do cache: um trim segura o registro e entra aqui
    // (reclaim), então pegar os dois na ordem inversa travaria.
    if (old) old->unregister_reclaimable(this);
    if (memory) (void)memory->register_reclaimable(this);
}

void DecodedFrameCache::set_focus(i64 playheadUs, i32 direction) noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    focusUs_ = playheadUs;
    direction_ = direction > 0 ? 1 : (direction < 0 ? -1 : 0);
}

f64 DecodedFrameCache::cost_locked(i64 ptsUs, i64 durationUs) const noexcept {
    if (required_locked(ptsUs, durationUs)) return -1;
    if (durationUs > 0 && focusUs_ >= ptsUs && focusUs_ - ptsUs < durationUs) return 0;
    const i64 d = ptsUs - focusUs_;
    const f64 dist = static_cast<f64>(d < 0 ? -d : d);
    const bool behind = (direction_ > 0 && d < 0) || (direction_ < 0 && d > 0);
    return behind ? dist * 3.0 : dist;
}

bool DecodedFrameCache::required_locked(i64 pts, i64 duration) const noexcept {
    for (u32 i = 0; i < requiredCount_; ++i) {
        const i64 delta = requiredTimes_[i] - pts;
        if (duration > 0 ? (delta >= 0 && delta < duration) : std::llabs(delta) <= requiredTolerance_) return true;
    }
    return false;
}

void DecodedFrameCache::set_required_times(const i64* times, u32 count, i64 toleranceUs) noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    count = times ? std::min<u32>(count, static_cast<u32>(requiredTimes_.size())) : 0;
    bool changed = count != requiredCount_ || toleranceUs != requiredTolerance_;
    for (u32 i = 0; i < count; ++i) {
        changed |= requiredTimes_[i] != times[i];
        requiredTimes_[i] = times[i];
    }
    requiredCount_ = count;
    requiredTolerance_ = toleranceUs;
    if (changed) { ++stats_.version; evict_locked(); }
}

usize DecodedFrameCache::worst_locked() const noexcept {
    usize worst = 0;
    f64 worstCost = -1.0;
    for (usize i = 0; i < frames_.size(); ++i) {
        const f64 c = cost_locked(frames_[i]->ptsUs, frames_[i]->durationUs);
        if (c > worstCost) { worstCost = c; worst = i; }
    }
    return worst;
}

void DecodedFrameCache::erase_locked(usize i) noexcept {
    const u64 b = std::min<u64>(stats_.bytes, frames_[i]->approx_bytes());
    stats_.bytes -= b;
    if (memory_) memory_->free(MemoryClass::DecodedFrames, static_cast<usize>(b));
    frames_.erase(frames_.begin() + static_cast<std::ptrdiff_t>(i));
    ++stats_.evictions;
}

bool DecodedFrameCache::over_shared_budget_locked() const noexcept {
    if (!memory_) return false;
    const usize budget = memory_->budget(MemoryClass::DecodedFrames);
    return budget != 0 && memory_->used(MemoryClass::DecodedFrames) > budget;
}

bool DecodedFrameCache::insert(FrameRef frame) noexcept {
    if (!frame) return false;
    std::lock_guard<std::mutex> lock(mutex_);

    const i64 pts = frame->ptsUs;
    auto it = std::lower_bound(frames_.begin(), frames_.end(), pts,
                               [](const FrameRef& f, i64 v) { return f->ptsUs < v; });
    if (it != frames_.end() && (*it)->ptsUs == pts) {
        // Mesmo instante decodificado de novo (depois de um seek): o novo
        // substitui — o antigo pode ter vindo de um estado de decoder anterior.
        const u64 oldB = std::min<u64>(stats_.bytes, (*it)->approx_bytes());
        const u64 newB = frame->approx_bytes();
        stats_.bytes = stats_.bytes - oldB + newB;
        if (memory_) {
            memory_->free(MemoryClass::DecodedFrames, static_cast<usize>(oldB));
            memory_->commit(MemoryClass::DecodedFrames, static_cast<usize>(newB));
        }
        *it = std::move(frame);
        // A replacement may have a different resolution/bit depth. Apply
        // both local and shared budgets just as for a newly inserted PTS.
        evict_locked();
        return true;
    }

    if (frames_.size() >= config_.maxFrames) {
        // Cheio: só entra se não for ele o pior de todos.
        f64 worst = cost_locked(pts, frame->durationUs);
        bool newIsWorst = true;
        for (const FrameRef& f : frames_) {
            if (cost_locked(f->ptsUs, f->durationUs) > worst) { newIsWorst = false; break; }
        }
        if (newIsWorst) return false;
    }

    const u64 b = frame->approx_bytes();
    stats_.bytes += b;
    if (memory_) memory_->commit(MemoryClass::DecodedFrames, static_cast<usize>(b));
    frames_.insert(it, std::move(frame));
    evict_locked();
    stats_.frames = static_cast<u32>(frames_.size());
    return true;
}

void DecodedFrameCache::evict_locked() noexcept {
    // Teto local (quantidade: buffers do decoder; bytes: esta fonte) e teto
    // COMPARTILHADO da categoria. Com o compartilhado estourado, a fonte que
    // insere devolve os próprios piores — mas nunca o último frame: a tela
    // precisa de algo para mostrar.
    while (!frames_.empty()
           && (frames_.size() > config_.maxFrames || (frames_.size() > 1 && stats_.bytes > config_.maxBytes)
               || (frames_.size() > 1 && over_shared_budget_locked()))) {
        const usize worst = worst_locked();
        if (frames_.size() <= config_.maxFrames && required_locked(frames_[worst]->ptsUs, frames_[worst]->durationUs)) break;
        erase_locked(worst);
    }
    stats_.frames = static_cast<u32>(frames_.size());
}

usize DecodedFrameCache::reclaim(usize targetBytes) noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    const u64 before = stats_.bytes;
    if (targetBytes == MemoryManager::kReclaimAll) {
        // Pressão do sistema: fica só o frame do playhead (o que está na
        // tela); o resto se decodifica de novo quando voltar a ser preciso.
        while (frames_.size() > 1) erase_locked(worst_locked());
    } else {
        while (frames_.size() > 1 && before - stats_.bytes < targetBytes) erase_locked(worst_locked());
    }
    stats_.frames = static_cast<u32>(frames_.size());
    if (before != stats_.bytes) ++stats_.version;
    return static_cast<usize>(before - stats_.bytes);
}

bool DecodedFrameCache::metrics(CacheMetrics& out) const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    out.name = "quadros-decodificados";
    out.cls = MemoryClass::DecodedFrames;
    out.stage = TrimStage::UnusedDecodedFrames;
    out.bytes = stats_.bytes;
    out.budgetBytes = memory_ ? memory_->budget(MemoryClass::DecodedFrames) : config_.maxBytes;
    out.entries = static_cast<u32>(frames_.size());
    out.hits = stats_.hits;
    out.misses = stats_.misses;
    out.evictions = stats_.evictions;
    out.version = stats_.version;
    return true;
}

FrameRef DecodedFrameCache::find(i64 targetUs, i64 halfFrameUs, bool* exact) noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    if (exact) *exact = false;
    if (frames_.empty()) { ++stats_.misses; return FrameRef{}; }

    auto covering = std::upper_bound(frames_.begin(), frames_.end(), targetUs,
        [](i64 time, const FrameRef& frame) { return time < frame->ptsUs; });
    if (covering != frames_.begin() && (*(covering - 1))->covers(targetUs)) {
        ++stats_.hits;
        if (exact) *exact = true;
        return *(covering - 1);
    }

    auto it = std::lower_bound(frames_.begin(), frames_.end(), targetUs - halfFrameUs,
                               [](const FrameRef& f, i64 v) { return f->ptsUs < v; });
    if (it != frames_.end() && (*it)->durationUs == 0 && std::llabs((*it)->ptsUs - targetUs) <= halfFrameUs) {
        ++stats_.hits;
        if (exact) *exact = true;
        return *it;
    }
    ++stats_.misses;

    // Mais próximo anterior; na falta dele, o mais próximo de qualquer lado.
    auto after = std::upper_bound(frames_.begin(), frames_.end(), targetUs,
                                  [](i64 v, const FrameRef& f) { return v < f->ptsUs; });
    if (after != frames_.begin()) return *(after - 1);
    return frames_.front();
}

bool DecodedFrameCache::contains(i64 targetUs, i64 halfFrameUs) const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    auto covering = std::upper_bound(frames_.begin(), frames_.end(), targetUs,
        [](i64 time, const FrameRef& frame) { return time < frame->ptsUs; });
    if (covering != frames_.begin() && (*(covering - 1))->covers(targetUs)) return true;
    auto it = std::lower_bound(frames_.begin(), frames_.end(), targetUs - halfFrameUs,
                               [](const FrameRef& f, i64 v) { return f->ptsUs < v; });
    return it != frames_.end() && (*it)->durationUs == 0 && std::llabs((*it)->ptsUs - targetUs) <= halfFrameUs;
}

i64 DecodedFrameCache::contiguous_end(i64 fromUs, i64 frameUs) const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    auto interval = std::upper_bound(frames_.begin(), frames_.end(), fromUs,
        [](i64 time, const FrameRef& frame) { return time < frame->ptsUs; });
    if (interval != frames_.begin() && (*(interval - 1))->covers(fromUs)) {
        --interval;
        i64 end = (*interval)->ptsUs + (*interval)->durationUs;
        for (++interval; interval != frames_.end(); ++interval) {
            if ((*interval)->durationUs <= 0 || (*interval)->ptsUs > end) break;
            end = std::max(end, (*interval)->ptsUs + (*interval)->durationUs);
        }
        // Scheduler adds one nominal interval to request the next uncovered time.
        return end - frameUs;
    }
    const i64 half = frameUs / 2;
    auto it = std::lower_bound(frames_.begin(), frames_.end(), fromUs - half,
                               [](const FrameRef& f, i64 v) { return f->ptsUs < v; });
    if (it == frames_.end() || (*it)->ptsUs > fromUs + half) return fromUs - frameUs;
    i64 end = (*it)->ptsUs;
    for (++it; it != frames_.end(); ++it) {
        if ((*it)->ptsUs - end > frameUs + half) break;
        end = (*it)->ptsUs;
    }
    return end;
}

i64 DecodedFrameCache::contiguous_begin(i64 fromUs, i64 frameUs) const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    auto interval = std::upper_bound(frames_.begin(), frames_.end(), fromUs,
        [](i64 time, const FrameRef& frame) { return time < frame->ptsUs; });
    if (interval != frames_.begin() && (*(interval - 1))->covers(fromUs)) {
        --interval;
        i64 begin = (*interval)->ptsUs;
        while (interval != frames_.begin()) {
            --interval;
            if ((*interval)->durationUs <= 0 || (*interval)->ptsUs + (*interval)->durationUs < begin) break;
            begin = (*interval)->ptsUs;
        }
        return begin;
    }
    const i64 half = frameUs / 2;
    // Primeiro frame com pts > fromUs + meio frame; o anterior a ele é o
    // candidato a "fromUs".
    auto it = std::upper_bound(frames_.begin(), frames_.end(), fromUs + half,
                               [](i64 v, const FrameRef& f) { return v < f->ptsUs; });
    if (it == frames_.begin()) return fromUs + frameUs;
    --it;
    if ((*it)->ptsUs < fromUs - half) return fromUs + frameUs;
    i64 begin = (*it)->ptsUs;
    while (it != frames_.begin()) {
        --it;
        if (begin - (*it)->ptsUs > frameUs + half) break;
        begin = (*it)->ptsUs;
    }
    return begin;
}

void DecodedFrameCache::clear() noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    if (memory_) memory_->free(MemoryClass::DecodedFrames, static_cast<usize>(stats_.bytes));
    frames_.clear();
    stats_.bytes = 0;
    stats_.frames = 0;
    ++stats_.version;   // o que estava guardado não vale mais (seek num decoder novo, suspensão)
}

DecodedFrameCache::Stats DecodedFrameCache::stats() const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    return stats_;
}

} // namespace aurea
