#include "aurea/ai/RotoService.hpp"

#include "aurea/core/Log.hpp"
#include "aurea/core/Thread.hpp"
#include "aurea/core/Time.hpp"

#include <algorithm>
#include <limits>
#include <new>
#include <system_error>

namespace aurea::ai {
namespace {
u64 strokes_key(const RotoStrokes& s) noexcept { return roto_dependency(s, std::numeric_limits<i64>::max()); }
bool has_stroke(const RotoStrokes& s, i64 frame) noexcept {
    return std::any_of(s.begin(), s.end(), [frame](const RotoStroke& r) { return r.frame == frame; });
}
} // namespace

RotoService::~RotoService() { clear(); }

u64 RotoService::key_of(u64 track, i64 frame) noexcept {
    u64 h = track ^ (static_cast<u64>(frame) * 0x9E3779B97F4A7C15ull);
    h ^= h >> 31; h *= 0xBF58476D1CE4E5B9ull; return h ^ (h >> 29);
}

RotoService::MattePtr RotoService::cached(u64 track, const RotoStrokes& strokes, i64 frame) {
    const u64 dep = roto_dependency(strokes, frame);
    if (!dep) return nullptr;
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = cache_.find(key_of(track, frame));
    if (it == cache_.end() || it->second.dep != dep) return nullptr;
    it->second.stamp = ++stamp_;
    auto out = std::make_shared<std::vector<f32>>();
    if (!roto_unpack(it->second.rle, static_cast<usize>(kSize) * kSize, *out)) return nullptr;
    return out;
}

void RotoService::store(u64 track, i64 frame, u64 dep, const std::vector<f32>& matte) {
    Entry e; e.dep = dep;
    roto_pack(matte, e.rle);
    std::lock_guard<std::mutex> lock(mutex_);
    e.stamp = ++stamp_;
    auto& slot = cache_[key_of(track, frame)];
    bytes_ -= std::min(bytes_, slot.rle.size());
    bytes_ += e.rle.size();
    slot = std::move(e);
    // Limite de memória: sai o recorte usado há mais tempo.
    while (bytes_ > maxBytes_ && cache_.size() > 1) {
        auto old = std::min_element(cache_.begin(), cache_.end(),
                                    [](const auto& a, const auto& b) { return a.second.stamp < b.second.stamp; });
        bytes_ -= std::min(bytes_, old->second.rle.size());
        cache_.erase(old);
    }
}

void RotoService::set_max_bytes(usize bytes) noexcept {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        maxBytes_ = std::max<usize>(bytes, 1);
    }
    (void)trim_to(maxBytes_);
}

usize RotoService::trim_to(usize keepBytes) noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    const usize before = bytes_;
    while (bytes_ > keepBytes && !cache_.empty()) {
        auto old = std::min_element(cache_.begin(), cache_.end(),
                                    [](const auto& a, const auto& b) { return a.second.stamp < b.second.stamp; });
        bytes_ -= std::min(bytes_, old->second.rle.size());
        cache_.erase(old);
    }
    if (cache_.empty()) bytes_ = 0;
    return before - bytes_;
}

usize RotoService::cached_bytes() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return bytes_;
}

DepthMapPtr RotoService::raw(const Source& source, i64 frame) {
    DepthMapPtr map;
    if (!source.factory) map = source.image;
    else if (source.fg) map = source.fg->foreground_video_frame(source.factory, source.asset, source.sourceKey, frame, source.fps);
    const usize n = static_cast<usize>(kSize) * kSize;
    if (!map || map->disparity.size() != n || map->foregroundRgb.size() != n * 3) return nullptr;
    return map;
}

RotoService::MattePtr RotoService::compute_impl(u64 track, const RotoStrokes& strokes, const Source& source, i64 frame,
                                                u64 deadlineNs, const std::atomic<u64>* generation, u64 expected) try {
    const std::vector<i64> bases = roto_bases(strokes);
    if (bases.empty()) return nullptr;
    const i64 last = source.factory ? std::max<i64>(0, source.frameCount - 1) : 0;
    frame = std::clamp<i64>(frame, 0, last);
    const i64 first = std::clamp<i64>(bases.front(), 0, last);
    const i64 dir = frame >= first ? 1 : -1;
    // Volta pela cadeia até um recorte válido (ou a primeira base).
    i64 k = frame;
    MattePtr hit;
    for (;;) {
        hit = cached(track, strokes, k);
        if (hit || k == first) break;
        k -= dir;
    }
    if (hit && k == frame) return hit;
    auto stop = [&] {
        return (generation && generation->load(std::memory_order_relaxed) != expected)
            || (deadlineNs && monotonic_ns() > deadlineNs);
    };
    std::vector<f32> matte;
    std::vector<u8> labels;
    DepthMapPtr prev;
    if (hit) {
        matte = *hit;
    } else {
        prev = raw(source, first);
        if (!prev) return nullptr;
        roto_rasterize(strokes, first, kSize, source.layerW, source.layerH, labels);
        roto_segment(prev->disparity.data(), prev->foregroundRgb.data(), labels.data(), nullptr, kSize, matte);
        store(track, first, roto_dependency(strokes, first), matte);
        (void)roto_unpack([&] { std::vector<u8> p; roto_pack(matte, p); return p; }(), matte.size(), matte);
        computed_.fetch_add(1, std::memory_order_relaxed);
    }
    while (k != frame) {
        if (stop()) return nullptr;
        if (!prev) { prev = raw(source, k); if (!prev) return nullptr; }
        const i64 m = k + dir;
        const DepthMapPtr cur = raw(source, m);
        if (!cur) return nullptr;
        const u8* lab = nullptr;
        if (dir > 0 && has_stroke(strokes, m)) {
            roto_rasterize(strokes, m, kSize, source.layerW, source.layerH, labels);
            lab = labels.data();
        }
        std::vector<f32> next;
        roto_propagate(matte, prev->foregroundRgb.data(), cur->foregroundRgb.data(), cur->disparity.data(), lab, kSize, next);
        store(track, m, roto_dependency(strokes, m), next);
        // O que continua a cadeia é o MESMO valor do cache: retomar do cache
        // (seek, export depois do preview) dá o mesmo recorte.
        std::vector<u8> packed; roto_pack(next, packed);
        (void)roto_unpack(packed, next.size(), matte);
        computed_.fetch_add(1, std::memory_order_relaxed);
        prev = cur;
        k = m;
    }
    return std::make_shared<const std::vector<f32>>(std::move(matte));
} catch (const std::bad_alloc&) {
    AUREA_LOG_WARN("Roto: memoria insuficiente");
    return nullptr;
}

RotoService::MattePtr RotoService::compute(u64 track, const RotoStrokes& strokes, const Source& source, i64 frame,
                                           u64 deadlineNs) {
    return compute_impl(track, strokes, source, frame, deadlineNs, nullptr, 0);
}

void RotoService::ensure_thread() {
    if (running_) return;
    thread_ = std::thread([this] { thread_main(); });
    running_ = true;
}

void RotoService::request(u64 track, const RotoStrokes& strokes, const Source& source, i64 frame) try {
    std::lock_guard<std::mutex> lock(queueMutex_);
    if (progress_.running && progress_.track == track) return;   // a propagação chega lá
    auto job = std::make_unique<Job>();
    job->track = track; job->strokes = strokes; job->source = source; job->first = job->last = frame;
    if (pending_ && pending_->range) return;                      // não troca "Propagar" por um quadro
    pending_ = std::move(job);
    ensure_thread();
    wake_.notify_one();
} catch (const std::system_error&) {
    AUREA_LOG_WARN("Roto: worker indisponivel");
}

void RotoService::propagate(u64 track, const RotoStrokes& strokes, const Source& source, i64 first, i64 last) try {
    std::lock_guard<std::mutex> lock(queueMutex_);
    generation_.fetch_add(1, std::memory_order_relaxed);   // o que estava rodando para
    auto job = std::make_unique<Job>();
    job->track = track; job->strokes = strokes; job->source = source;
    job->first = std::min(first, last); job->last = std::max(first, last); job->range = true;
    progress_ = Progress{track, 0, job->last - job->first + 1, true, false};
    pending_ = std::move(job);
    ensure_thread();
    wake_.notify_one();
} catch (const std::system_error&) {
    AUREA_LOG_WARN("Roto: worker indisponivel");
}

RotoService::Progress RotoService::progress() const {
    std::lock_guard<std::mutex> lock(queueMutex_);
    return progress_;
}

void RotoService::cancel() noexcept {
    std::lock_guard<std::mutex> lock(queueMutex_);
    generation_.fetch_add(1, std::memory_order_relaxed);
    if (pending_) pending_.reset();
    progress_.running = false;
}

void RotoService::set_ready_callback(void (*fn)(void*), void* ctx) noexcept {
    std::lock_guard<std::mutex> lock(queueMutex_);
    readyFn_ = fn; readyCtx_ = ctx;
}

void RotoService::thread_main() noexcept {
    set_current_thread_name("aurea-roto");
    set_current_thread_priority(ThreadPriority::Background);
    for (;;) {
        std::unique_ptr<Job> job;
        u64 gen = 0;
        {
            std::unique_lock<std::mutex> lock(queueMutex_);
            wake_.wait(lock, [&] { return !running_ || pending_ != nullptr; });
            if (!running_) break;
            job = std::move(pending_);
            gen = generation_.load(std::memory_order_relaxed);
        }
        bool ok = true;
        auto notify = [&] {
            void (*fn)(void*) = nullptr; void* ctx = nullptr;
            { std::lock_guard<std::mutex> lock(queueMutex_); fn = readyFn_; ctx = readyCtx_; }
            if (fn) fn(ctx);
        };
        if (!job->range) {
            ok = compute_impl(job->track, job->strokes, job->source, job->first, 0, &generation_, gen) != nullptr;
            if (ok) notify();
            continue;
        }
        // Propagar: para a frente da primeira base até o fim, depois para trás.
        const std::vector<i64> bases = roto_bases(job->strokes);
        const i64 base = bases.empty() ? job->first : std::clamp(bases.front(), job->first, job->last);
        i64 done = 0;
        auto step = [&](i64 f) {
            if (!ok) return;
            ok = compute_impl(job->track, job->strokes, job->source, f, 0, &generation_, gen) != nullptr;
            std::lock_guard<std::mutex> lock(queueMutex_);
            if (generation_.load(std::memory_order_relaxed) == gen) progress_.done = ++done;
        };
        for (i64 f = base; f <= job->last && ok; ++f) { step(f); if ((f - base) % 8 == 7) notify(); }
        // Um quadro que falhou adiante não impede os quadros antes da base
        // (só o cancelamento, que troca a geração, para tudo).
        const bool forwardOk = ok;
        if (!ok && generation_.load(std::memory_order_relaxed) == gen) ok = true;
        for (i64 f = base - 1; f >= job->first && ok; --f) { step(f); if ((base - f) % 8 == 0) notify(); }
        ok = ok && forwardOk;
        {
            std::lock_guard<std::mutex> lock(queueMutex_);
            if (generation_.load(std::memory_order_relaxed) == gen) {
                progress_.running = false;
                progress_.failed = !ok;
            }
        }
        notify();
    }
}

void RotoService::clear() noexcept {
    {
        std::lock_guard<std::mutex> lock(queueMutex_);
        running_ = false;
        pending_.reset();
        generation_.fetch_add(1, std::memory_order_relaxed);
        progress_ = Progress{};
    }
    wake_.notify_all();
    if (thread_.joinable()) thread_.join();
    std::lock_guard<std::mutex> lock(mutex_);
    cache_.clear();
    bytes_ = 0;
}

} // namespace aurea::ai
