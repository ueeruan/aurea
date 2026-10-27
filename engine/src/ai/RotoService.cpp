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
usize refined_cap(usize coarseCap) noexcept { return coarseCap / 3 * RotoService::kRefinedShare; }
} // namespace

RotoService::~RotoService() { clear(); }

u64 RotoService::key_of(u64 track, i64 frame) noexcept {
    u64 h = track ^ (static_cast<u64>(frame) * 0x9E3779B97F4A7C15ull);
    h ^= h >> 31; h *= 0xBF58476D1CE4E5B9ull; return h ^ (h >> 29);
}

u64 RotoService::refined_dep(const RotoStrokes& strokes, i64 frame, u64 refineKey) noexcept {
    const u64 dep = roto_dependency(strokes, frame);
    if (!dep || !refineKey) return 0;
    u64 h = dep ^ (refineKey * 0x9E3779B97F4A7C15ull);
    h ^= h >> 29; h *= 0xBF58476D1CE4E5B9ull; h ^= h >> 32;
    return h ? h : 1;
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

RotoService::RefinedPtr RotoService::cached_refined_dep(u64 track, i64 frame, u64 dep) {
    if (!dep) return nullptr;
    std::vector<u8> packed;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = refined_.find(key_of(track, frame));
        if (it == refined_.end() || it->second.dep != dep) return nullptr;
        it->second.stamp = ++stamp_;
        packed = it->second.rle;
    }
    auto out = std::make_shared<RotoRefined>();
    if (!roto_refined_unpack(packed, *out)) return nullptr;
    return out;
}

RotoService::RefinedPtr RotoService::cached_refined(u64 track, const RotoStrokes& strokes, const RotoRefineSettings& refine,
                                                    i64 frame) {
    if (!refine.on()) return nullptr;
    return cached_refined_dep(track, frame, refined_dep(strokes, frame, refine.hash()));
}

bool RotoService::ready(u64 track, const RotoStrokes& strokes, i64 frame, u64 refineKey) const {
    const u64 dep = roto_dependency(strokes, frame);
    if (!dep) return false;
    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = cache_.find(key_of(track, frame));
    if (it == cache_.end() || it->second.dep != dep) return false;
    if (!refineKey) return true;
    const auto rt = refined_.find(key_of(track, frame));
    return rt != refined_.end() && rt->second.dep == refined_dep(strokes, frame, refineKey);
}

i64 RotoService::ready_count(u64 track, const RotoStrokes& strokes, i64 first, i64 last, u64 refineKey) const {
    if (last < first) std::swap(first, last);
    if (last - first > 100'000) last = first + 100'000;   // teto de custo da consulta
    i64 n = 0;
    std::lock_guard<std::mutex> lock(mutex_);
    for (i64 f = first; f <= last; ++f) {
        const u64 dep = roto_dependency(strokes, f);
        if (!dep) continue;
        const auto it = cache_.find(key_of(track, f));
        if (it == cache_.end() || it->second.dep != dep) continue;
        if (refineKey) {
            const auto rt = refined_.find(key_of(track, f));
            if (rt == refined_.end() || rt->second.dep != refined_dep(strokes, f, refineKey)) continue;
        }
        ++n;
    }
    return n;
}

void RotoService::evict_locked(usize keepCoarse, usize keepRefined) noexcept {
    // Limite de memória: sai o usado há mais tempo (cada cache no seu teto).
    auto evict = [](std::unordered_map<u64, Entry>& map, usize& bytes, usize keep) {
        while (bytes > keep && !map.empty()) {
            auto old = std::min_element(map.begin(), map.end(),
                                        [](const auto& a, const auto& b) { return a.second.stamp < b.second.stamp; });
            bytes -= std::min(bytes, old->second.rle.size());
            map.erase(old);
        }
        if (map.empty()) bytes = 0;
    };
    evict(cache_, bytes_, keepCoarse);
    evict(refined_, refinedBytes_, keepRefined);
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
    // O recém-guardado nunca é o mais velho: o teto não o apaga (salvo um só item).
    if (bytes_ > maxBytes_ && cache_.size() > 1) evict_locked(maxBytes_, refined_cap(maxBytes_));
}

void RotoService::store_refined(u64 track, i64 frame, u64 dep, const RotoRefined& refined) {
    Entry e; e.dep = dep;
    roto_refined_pack(refined, e.rle);
    if (e.rle.empty()) return;
    std::lock_guard<std::mutex> lock(mutex_);
    e.stamp = ++stamp_;
    auto& slot = refined_[key_of(track, frame)];
    refinedBytes_ -= std::min(refinedBytes_, slot.rle.size());
    refinedBytes_ += e.rle.size();
    slot = std::move(e);
    if (refinedBytes_ > refined_cap(maxBytes_) && refined_.size() > 1) evict_locked(maxBytes_, refined_cap(maxBytes_));
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
    const usize before = bytes_ + refinedBytes_;
    evict_locked(keepBytes, std::min(keepBytes, refined_cap(maxBytes_)));
    if (!keepBytes) imageSlot_ = ImageSlot{};
    return before - (bytes_ + refinedBytes_);
}

usize RotoService::cached_bytes() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return bytes_ + refinedBytes_;
}

RotoImagePtr RotoService::image_frame(u64 key, const u8* rgba, u32 width, u32 height, u32 stride, u32 maxSide) try {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (imageSlot_.image && imageSlot_.key == key && imageSlot_.maxSide == maxSide) return imageSlot_.image;
    }
    auto image = std::make_shared<RotoImage>();
    roto_image_from(rgba, width, height, stride, 4, maxSide, *image);
    if (!image->width) return nullptr;
    std::lock_guard<std::mutex> lock(mutex_);
    imageSlot_ = ImageSlot{key, maxSide, image};
    return image;
} catch (const std::bad_alloc&) {
    return nullptr;
}

DepthMapPtr RotoService::raw(const Source& source, i64 frame, RotoImagePtr* image) {
    DepthMapPtr map;
    if (image) image->reset();
    if (!source.factory) {
        map = source.image;
        if (image) *image = source.imageFrame;
    } else if (source.fg) {
        map = source.fg->foreground_video_frame(source.factory, source.asset, source.sourceKey, frame, source.fps,
                                                image, source.refineMaxSide);
    }
    const usize n = static_cast<usize>(kSize) * kSize;
    if (!map || map->disparity.size() != n || map->foregroundRgb.size() != n * 3) {
        // Bugs 0.3.4: o motivo (rede, decoder, memória) vai para o status.
        lastFailure_.store(source.fg ? std::max<u32>(1u, source.fg->last_failure()) : 1u, std::memory_order_relaxed);
        return nullptr;
    }
    return map;
}

RotoImagePtr RotoService::frame_image(const Source& source, i64 frame) {
    if (!source.factory) return source.imageFrame;
    if (!source.fg) return nullptr;
    RotoImagePtr image = source.fg->source_video_frame(source.factory, source.asset, source.sourceKey, frame, source.fps,
                                                       source.refineMaxSide);
    if (!image) lastFailure_.store(std::max<u32>(1u, source.fg->last_failure()), std::memory_order_relaxed);
    return image;
}

RotoService::Result RotoService::compute_impl(u64 track, const RotoStrokes& strokes, const Source& source, i64 frame,
                                              u64 deadlineNs, const std::atomic<u64>* generation, u64 expected) try {
    const std::vector<i64> bases = roto_bases(strokes);
    if (bases.empty()) return {};
    const i64 last = source.factory ? std::max<i64>(0, source.frameCount - 1) : 0;
    frame = std::clamp<i64>(frame, 0, last);
    const i64 first = std::clamp<i64>(bases.front(), 0, last);
    const i64 dir = frame >= first ? 1 : -1;
    const bool refine = source.refine.on();
    const u64 refineKey = refine ? source.refine.hash() : 0;
    // Sem "reduzir trepidação" o refinado de um quadro não depende do anterior:
    // basta o recorte dele (um scrub não refaz a cadeia desde a base).
    const bool chained = refine && source.refine.chatter > 0.0f;
    // Volta pela cadeia até um quadro com tudo o que a continuação precisa
    // (o recorte e, com refino encadeado, o refinado), ou a primeira base.
    i64 k = frame;
    MattePtr hit;
    RefinedPtr rhit;
    for (;;) {
        hit = cached(track, strokes, k);
        rhit = hit && refine ? cached_refined_dep(track, k, refined_dep(strokes, k, refineKey)) : nullptr;
        if ((hit && (!refine || rhit || !chained)) || k == first) break;
        k -= dir;
    }
    if (hit && (!refine || rhit) && k == frame) return {hit, rhit};
    auto stop = [&] {
        return (generation && generation->load(std::memory_order_relaxed) != expected)
            || (deadlineNs && monotonic_ns() > deadlineNs);
    };
    // Um passo do refino: o raio vem em px da fonte; o quadro do refino pode
    // ser menor (4K → 1920). Com trepidação, o refinado anterior levado pelo
    // fluxo entre as grades dos dois quadros.
    std::vector<u8> gridPrev, gridCur;
    std::vector<Vec2> flow;
    auto refine_step = [&](i64 m, const std::vector<f32>& matteM, const RotoImagePtr& img, const RotoRefined* prev,
                           const RotoImage* prevImg) -> RefinedPtr {
        if (!img) return nullptr;
        RotoRefineSettings s = source.refine;
        const f32 sourceSide = std::max(source.layerW, source.layerH);
        if (sourceSide > 1.0f) s.radius *= static_cast<f32>(std::max(img->width, img->height)) / sourceSide;
        s.radius = std::max(s.radius, 0.5f);
        RotoTemporal t;
        const bool temporal = s.chatter > 0.0f && prev && prevImg && prev->width == img->width && prev->height == img->height;
        if (temporal) {
            roto_grid_rgb(*prevImg, kSize, gridPrev);
            roto_grid_rgb(*img, kSize, gridCur);
            roto_flow(gridPrev.data(), gridCur.data(), kSize, flow);
            t.prev = prev; t.flow = flow.data(); t.n = kSize;
        }
        auto out = std::make_shared<RotoRefined>();
        roto_refine(matteM.data(), kSize, *img, s, temporal ? &t : nullptr, *out);
        store_refined(track, m, refined_dep(strokes, m, refineKey), *out);
        return out;
    };

    std::vector<f32> matte;
    std::vector<u8> labels;
    DepthMapPtr prev;          // mapa cru do quadro k (só se o recorte for calculado)
    RotoImagePtr prevImg;      // quadro do refino de k
    RefinedPtr prevRefined;
    if (hit && (!refine || rhit || !chained) && !(refine && !rhit && k == frame)) {
        matte = *hit;
        prevRefined = rhit;
    } else {
        // k = a base (ou, sem trepidação, o próprio quadro): o recorte do cache
        // ou da segmentação; o refinado sem anterior.
        if (hit) {
            matte = *hit;
            if (refine) prevImg = frame_image(source, k);
        } else {
            prev = raw(source, first, refine ? &prevImg : nullptr);
            if (!prev) return {};
            roto_rasterize(strokes, first, kSize, source.layerW, source.layerH, labels);
            roto_segment(prev->disparity.data(), prev->foregroundRgb.data(), labels.data(), nullptr, kSize, matte);
            store(track, first, roto_dependency(strokes, first), matte);
            (void)roto_unpack([&] { std::vector<u8> p; roto_pack(matte, p); return p; }(), matte.size(), matte);
            computed_.fetch_add(1, std::memory_order_relaxed);
        }
        if (refine) {
            if (stop()) return {};
            prevRefined = refine_step(k, matte, prevImg, nullptr, nullptr);
            if (!prevRefined) return {};
        }
    }
    while (k != frame) {
        if (stop()) return {};
        const i64 m = k + dir;
        // O recorte de m: do cache (um refino novo não refaz a rede) ou propagado.
        // Sem trepidação, só o quadro pedido precisa do refinado.
        const bool refineHere = refine && (chained || m == frame);
        MattePtr cachedM = cached(track, strokes, m);
        RotoImagePtr curImg;
        if (cachedM) {
            matte = *cachedM;
            prev.reset();
            if (refineHere) curImg = frame_image(source, m);
        } else {
            if (!prev) { prev = raw(source, k); if (!prev) return {}; }
            const DepthMapPtr cur = raw(source, m, refineHere ? &curImg : nullptr);
            if (!cur) return {};
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
        }
        if (refineHere) {
            // O quadro anterior só faz falta para o fluxo da trepidação.
            if (!prevImg && source.refine.chatter > 0.0f) prevImg = frame_image(source, k);
            if (stop()) return {};
            RefinedPtr r = refine_step(m, matte, curImg, prevRefined.get(), prevImg.get());
            if (!r) return {};
            prevRefined = std::move(r);
            prevImg = std::move(curImg);
        }
        k = m;
    }
    return {std::make_shared<const std::vector<f32>>(std::move(matte)), prevRefined};
} catch (const std::bad_alloc&) {
    AUREA_LOG_WARN("Roto: memoria insuficiente");
    lastFailure_.store(DepthMapService::kFailMemory, std::memory_order_relaxed);
    return {};
}

RotoService::MattePtr RotoService::compute(u64 track, const RotoStrokes& strokes, const Source& source, i64 frame,
                                           u64 deadlineNs) {
    return compute_impl(track, strokes, source, frame, deadlineNs, nullptr, 0).matte;
}

RotoService::Result RotoService::compute_all(u64 track, const RotoStrokes& strokes, const Source& source, i64 frame,
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
    auto job = std::make_unique<Job>();
    job->track = track; job->strokes = strokes; job->source = source; job->first = job->last = frame;
    if ((progress_.running && progress_.track == track) || (pending_ && pending_->range)) {
        // A propagação segue, mas este quadro (o que o usuário olha/pinta) vai
        // na frente: antes o pedido era descartado e o palco esperava a
        // propagação chegar nele - "o quadro congela".
        focus_ = std::move(job);
        ensure_thread();
        wake_.notify_one();
        return;
    }
    // Um pedido de um quadro em curso (scrub antigo) para: o mais novo vence.
    if (runningSingle_) generation_.fetch_add(1, std::memory_order_relaxed);
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
    focus_.reset();
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
            wake_.wait(lock, [&] { return !running_ || pending_ != nullptr || focus_ != nullptr; });
            if (!running_) break;
            // Sem propagação na fila, o foco é um pedido comum de um quadro.
            job = pending_ ? std::move(pending_) : std::move(focus_);
            gen = generation_.load(std::memory_order_relaxed);
            runningSingle_ = !job->range;
        }
        auto notify = [&] {
            void (*fn)(void*) = nullptr; void* ctx = nullptr;
            { std::lock_guard<std::mutex> lock(queueMutex_); fn = readyFn_; ctx = readyCtx_; }
            if (fn) fn(ctx);
        };
        bool ok = true;
        if (!job->range) {
            ok = compute_impl(job->track, job->strokes, job->source, job->first, 0, &generation_, gen).matte != nullptr;
            { std::lock_guard<std::mutex> lock(queueMutex_); runningSingle_ = false; }
            // Acorda a prévia também na falha/interrupção: o palco não fica
            // esperando um recorte que nunca vem.
            notify();
            continue;
        }
        // Propagar: para a frente da primeira base até o fim, depois para trás.
        // Entre quadros, o FOCO (o quadro que o usuário está olhando) vai antes.
        const std::vector<i64> bases = roto_bases(job->strokes);
        const i64 base = bases.empty() ? job->first : std::clamp(bases.front(), job->first, job->last);
        i64 done = 0;
        lastFailure_.store(0, std::memory_order_relaxed);
        auto serveFocus = [&] {
            std::unique_ptr<Job> focus;
            { std::lock_guard<std::mutex> lock(queueMutex_); focus = std::move(focus_); }
            if (!focus || generation_.load(std::memory_order_relaxed) != gen) return;
            (void)compute_impl(focus->track, focus->strokes, focus->source, focus->first, 0, &generation_, gen);
            notify();
        };
        auto step = [&](i64 f) {
            if (!ok) return;
            serveFocus();
            ok = compute_impl(job->track, job->strokes, job->source, f, 0, &generation_, gen).matte != nullptr;
            std::lock_guard<std::mutex> lock(queueMutex_);
            if (generation_.load(std::memory_order_relaxed) == gen) progress_.done = ++done;
        };
        for (i64 f = base; f <= job->last && ok; ++f) { step(f); notify(); }
        // Um quadro que falhou adiante não impede os quadros antes da base
        // (só o cancelamento, que troca a geração, para tudo).
        const bool forwardOk = ok;
        if (!ok && generation_.load(std::memory_order_relaxed) == gen) ok = true;
        for (i64 f = base - 1; f >= job->first && ok; --f) { step(f); notify(); }
        ok = ok && forwardOk;
        {
            std::lock_guard<std::mutex> lock(queueMutex_);
            if (generation_.load(std::memory_order_relaxed) == gen) {
                progress_.running = false;
                progress_.failed = !ok;
                progress_.reason = ok ? 0u : std::max<u32>(1u, lastFailure_.load(std::memory_order_relaxed));
            }
        }
        serveFocus();
        notify();
    }
}

void RotoService::clear() noexcept {
    {
        std::lock_guard<std::mutex> lock(queueMutex_);
        running_ = false;
        pending_.reset();
        focus_.reset();
        generation_.fetch_add(1, std::memory_order_relaxed);
        progress_ = Progress{};
    }
    wake_.notify_all();
    if (thread_.joinable()) thread_.join();
    std::lock_guard<std::mutex> lock(mutex_);
    cache_.clear();
    refined_.clear();
    refinedBytes_ = 0;
    imageSlot_ = ImageSlot{};
    bytes_ = 0;
}

} // namespace aurea::ai
