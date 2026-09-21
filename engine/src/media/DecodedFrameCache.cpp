#include "aurea/media/DecodedFrameCache.hpp"

#include <algorithm>
#include <cstdlib>

namespace aurea {

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

void DecodedFrameCache::set_focus(i64 playheadUs, i32 direction) noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    focusUs_ = playheadUs;
    direction_ = direction > 0 ? 1 : (direction < 0 ? -1 : 0);
}

f64 DecodedFrameCache::cost_locked(i64 ptsUs) const noexcept {
    const i64 d = ptsUs - focusUs_;
    const f64 dist = static_cast<f64>(d < 0 ? -d : d);
    const bool behind = (direction_ > 0 && d < 0) || (direction_ < 0 && d > 0);
    return behind ? dist * 3.0 : dist;
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
        stats_.bytes -= std::min<u64>(stats_.bytes, (*it)->approx_bytes());
        *it = std::move(frame);
        stats_.bytes += (*it)->approx_bytes();
        return true;
    }

    if (frames_.size() >= config_.maxFrames) {
        // Cheio: só entra se não for ele o pior de todos.
        f64 worst = cost_locked(pts);
        bool newIsWorst = true;
        for (const FrameRef& f : frames_) {
            if (cost_locked(f->ptsUs) > worst) { newIsWorst = false; break; }
        }
        if (newIsWorst) return false;
    }

    stats_.bytes += frame->approx_bytes();
    frames_.insert(it, std::move(frame));
    evict_locked();
    stats_.frames = static_cast<u32>(frames_.size());
    return true;
}

void DecodedFrameCache::evict_locked() noexcept {
    while (!frames_.empty()
           && (frames_.size() > config_.maxFrames || stats_.bytes > config_.maxBytes)) {
        usize worst = 0;
        f64 worstCost = -1.0;
        for (usize i = 0; i < frames_.size(); ++i) {
            const f64 c = cost_locked(frames_[i]->ptsUs);
            if (c > worstCost) { worstCost = c; worst = i; }
        }
        stats_.bytes -= std::min<u64>(stats_.bytes, frames_[worst]->approx_bytes());
        frames_.erase(frames_.begin() + static_cast<std::ptrdiff_t>(worst));
        ++stats_.evictions;
    }
    stats_.frames = static_cast<u32>(frames_.size());
}

FrameRef DecodedFrameCache::find(i64 targetUs, i64 halfFrameUs, bool* exact) noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    if (exact) *exact = false;
    if (frames_.empty()) { ++stats_.misses; return FrameRef{}; }

    auto it = std::lower_bound(frames_.begin(), frames_.end(), targetUs - halfFrameUs,
                               [](const FrameRef& f, i64 v) { return f->ptsUs < v; });
    if (it != frames_.end() && std::llabs((*it)->ptsUs - targetUs) <= halfFrameUs) {
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
    auto it = std::lower_bound(frames_.begin(), frames_.end(), targetUs - halfFrameUs,
                               [](const FrameRef& f, i64 v) { return f->ptsUs < v; });
    return it != frames_.end() && std::llabs((*it)->ptsUs - targetUs) <= halfFrameUs;
}

i64 DecodedFrameCache::contiguous_end(i64 fromUs, i64 frameUs) const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
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

void DecodedFrameCache::clear() noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    frames_.clear();
    stats_.bytes = 0;
    stats_.frames = 0;
}

DecodedFrameCache::Stats DecodedFrameCache::stats() const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    return stats_;
}

} // namespace aurea
