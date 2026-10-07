#include "aurea/ai/DepthMapService.hpp"
#include "aurea/ai/ForegroundMatte.hpp"

#include "aurea/core/Log.hpp"
#include "aurea/core/Thread.hpp"
#include "aurea/media/MediaManager.hpp"
#include "aurea/media/ThumbnailService.hpp"

#include <algorithm>
#include <chrono>
#include <new>
#include <system_error>

namespace aurea::ai {

DepthMapService::DepthMapService(bool foreground) : foregroundMode_(foreground) {}

DepthMapService::~DepthMapService() {
    clear();
}

void DepthMapService::set_ready_callback(void (*fn)(void*), void* ctx) noexcept {
    std::lock_guard<std::mutex> lock(queueMutex_);
    readyFn_ = fn;
    readyCtx_ = ctx;
}

DepthMapPtr DepthMapService::find(u64 key) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = index_.find(key);
    if (it == index_.end()) return nullptr;
    lru_.splice(lru_.begin(), lru_, it->second);
    ++stats_.hits;
    return it->second->second;
}

DepthMapPtr DepthMapService::cached(u64 key) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = index_.find(key);
    return it == index_.end() ? nullptr : it->second->second;
}

DepthMapPtr DepthMapService::latest_video(u64 sourceKey, u64& key, i64& targetUs, i64& frameUs) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = latest_.find(sourceKey);
    if (it == latest_.end() || !it->second.map) return nullptr;
    key = it->second.key;
    targetUs = it->second.targetUs;
    frameUs = it->second.frameUs;
    return it->second.map;
}

void DepthMapService::insert(u64 key, DepthMapPtr map) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (auto it = index_.find(key); it != index_.end()) {
        it->second->second = std::move(map);
        for (auto& [source, latest] : latest_) if (latest.key == key) latest.map = it->second->second;
        lru_.splice(lru_.begin(), lru_, it->second);
        return;
    }
    lru_.emplace_front(key, std::move(map));
    index_[key] = lru_.begin();
    while (lru_.size() > (!foregroundMode_ ? kMaxCached : 16u)) {
        const u64 expired = lru_.back().first;
        std::erase_if(latest_, [expired](const auto& item) { return item.second.key == expired; });
        index_.erase(expired);
        lru_.pop_back();
    }
}

void DepthMapService::publish_latest(u64 sourceKey, u64 key, i64 targetUs, i64 frameUs) {
    std::lock_guard<std::mutex> lock(mutex_);
    // An export/image request can evict this result between inference and
    // publication. Only the bounded LRU owns maps retained for fallback.
    const auto it = index_.find(key);
    if (it != index_.end()) latest_[sourceKey] = Latest{key, targetUs, frameUs, it->second->second};
}

DepthMapService::Stats DepthMapService::stats() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return stats_;
}

u32 DepthMapService::activity_state() noexcept {
    std::lock_guard<std::mutex> lock(queueMutex_);
    if (hasActive_ || !pending_.empty()) return 1;
    return lastJobFailed_.load(std::memory_order_relaxed) ? 2u : 0u;
}

void DepthMapService::report_failure(const char* reason) noexcept {
    lastJobFailed_.store(true, std::memory_order_relaxed);
    {
        std::lock_guard<std::mutex> lock(mutex_);
        ++stats_.failures;
    }
    AUREA_LOG_WARN("%s: %s", foregroundMode_ ? "Rotobrush" : "profundidade", reason);
}

bool DepthMapService::ensure_model_locked() {
    if (foregroundMode_ ? foreground_.loaded() : estimator_.loaded()) return true;
    if (modelFailed_) return false;
    const Status s = foregroundMode_ ? foreground_.load() : estimator_.load(DepthEstimator::Backend::Auto);
    std::lock_guard<std::mutex> lock(mutex_);
    stats_.modelLoaded = s.ok();
    stats_.backend = foregroundMode_ ? DepthEstimator::Backend::Cpu : estimator_.backend();
    if (!s.ok()) {
        // Não tenta de novo a cada quadro: só depois de `clear` (outro projeto).
        modelFailed_ = true;
        lastJobFailed_.store(true, std::memory_order_relaxed);
        ++stats_.failures;
        AUREA_LOG_WARN("%s: modelo nao carregou (%s)", foregroundMode_ ? "Rotobrush" : "profundidade", s.message().data());
    }
    return s.ok();
}

DepthMapPtr DepthMapService::run_pixels_locked(u64 key, const u8* pixels, u32 width, u32 height, u32 stride,
                                               u32 channels) {
    if (!ensure_model_locked()) return nullptr;
    auto map = std::make_shared<DepthMap>();
    const bool fg=foregroundMode_;
    map->disparity.resize(fg ? ForegroundEstimator::kPixels : DepthEstimator::kPixels);
    if (fg) foreground_rgb(pixels,width,height,stride,channels,ForegroundEstimator::kSize,map->foregroundRgb);
    const Status s = fg ? foreground_.run(pixels,width,height,stride,channels,cancel_,map->disparity.data())
                        : estimator_.run(pixels, width, height, stride, channels, cancel_, map->disparity.data());
    if (!s.ok()) {
        std::lock_guard<std::mutex> lock(mutex_);
        ++stats_.failures;
        if (s.code() != Errc::Cancelled) {
            lastJobFailed_.store(true, std::memory_order_relaxed);
            AUREA_LOG_WARN("profundidade: inferencia falhou (%s)", s.message().data());
        }
        return nullptr;
    }
    if (!fg) depth_percentiles(map->disparity.data(), DepthEstimator::kPixels, map->p2, map->p98);
    map->inferenceMs = fg ? foreground_.last_inference_ms() : estimator_.last_inference_ms();
    {
        std::lock_guard<std::mutex> lock(mutex_);
        ++stats_.inferences;
        stats_.lastInferenceMs = map->inferenceMs;
        stats_.backend = foregroundMode_ ? DepthEstimator::Backend::Cpu : estimator_.backend();
    }
    lastJobFailed_.store(false, std::memory_order_relaxed);
    DepthMapPtr done = std::move(map);
    insert(key, done);
    return done;
}

DepthMapPtr DepthMapService::image(u64 key, const u8* pixels, u32 width, u32 height, u32 stride, u32 channels, bool wait) try {
    if (DepthMapPtr hit = find(key)) return hit;
    if (!pixels || !width || !height || (channels != 3 && channels != 4) || width > 16384 || height > 16384 || stride < width * channels) return nullptr;
    if (!wait) {
        {
            std::lock_guard<std::mutex> queue(queueMutex_);
            if ((hasActive_ && activeKey_ == key) || std::any_of(pending_.begin(), pending_.end(),
                    [key](const Job& job) { return job.key == key; })) return nullptr;
        }
        Job job; job.key = key; job.sourceKey = key; job.imageSize = !foregroundMode_ ? DepthEstimator::kSize : ForegroundEstimator::kSize;
        const u32 size = job.imageSize;
        job.pixels.resize(static_cast<usize>(size) * size * 4);
        std::vector<u8> rgb;
        if (foregroundMode_) foreground_rgb(pixels,width,height,stride,channels,size,rgb);
        else {
            rgb.resize(static_cast<usize>(size) * size * 3);
            depth_input_rgb(pixels, width, height, stride, channels, rgb.data());
        }
        for (u32 y=0;y<size;++y) for (u32 x=0;x<size;++x) {
            const u32 sy=std::min(height-1,static_cast<u32>((y+.5)*height/size)),sx=std::min(width-1,static_cast<u32>((x+.5)*width/size));
            auto* dest=job.pixels.data()+(y*size+x)*4;
            std::copy_n(rgb.empty() ? pixels+static_cast<usize>(sy)*stride+sx*channels : rgb.data()+(y*size+x)*3,3,dest); dest[3]=255;
        }
        enqueue(std::move(job)); return nullptr;
    }
    std::lock_guard<std::mutex> work(work_);
    if (DepthMapPtr hit = cached(key)) return hit;   // outra thread terminou enquanto esperávamos
    return run_pixels_locked(key, pixels, width, height, stride, channels);
} catch (const std::bad_alloc&) {
    report_failure("memoria insuficiente para o mapa");
    return nullptr;
} catch (const std::system_error&) {
    report_failure("worker de inferencia indisponivel");
    return nullptr;
}

DepthMapPtr DepthMapService::run_video_locked(const Job& job) {
    if (!foregroundMode_) return run_video_raw_locked(job);
    const f64 fps = job.asset.video.fps > 0 ? job.asset.video.fps : 1e6 / job.frameUs;
    const i64 current = std::max<i64>(0, std::llround(job.targetUs * fps / 1e6));
    const i64 last = job.asset.video.frameCount.value > 0 ? job.asset.video.frameCount.value - 1 : current + 1;
    auto raw = [&](i64 index) {
        Job frame = job;
        index = std::clamp(index, i64{0}, last);
        frame.targetUs = static_cast<i64>(std::llround(index * 1e6 / fps));
        // Namespace separate from renderer cache keys, source revision included.
        u64 key = job.sourceKey ^ (static_cast<u64>(index) + 0x72E46D87FA912BC3ull);
        key = (key ^ (key >> 30)) * 0xBF58476D1CE4E5B9ull;
        frame.key = key ^ (key >> 27);
        if (auto hit = cached(frame.key)) return hit;
        return run_video_raw_locked(frame);
    };
    const auto previous = raw(current - 1);
    const auto center = raw(current);
    if (!center) return nullptr;
    const i64 durationUs = decoder_ ? decoder_->info().durationUs : 0;
    const i64 nextUs = static_cast<i64>(std::llround((current + 1) * 1e6 / fps));
    const auto next = durationUs > 0 && nextUs >= durationUs ? center : raw(current + 1);
    if (!previous || !next) return nullptr; // never export an order-dependent fallback
    const auto& a = *previous;
    const auto& b = *next;
    auto result = std::make_shared<DepthMap>();
    stabilize_foreground(center->disparity,center->foregroundRgb,a.disparity,a.foregroundRgb,
                         b.disparity,b.foregroundRgb,ForegroundEstimator::kSize,result->disparity);
    result->inferenceMs = center->inferenceMs;
    insert(job.key,result);
    return result;
}

DepthMapPtr DepthMapService::foreground_video_frame(VideoSourceFactory* factory, const Asset& asset, u64 sourceKey,
                                                    i64 index, f64 fps) try {
    if (!foregroundMode_ || !factory || !(fps > 0.0)) return nullptr;
    const i64 last = asset.video.frameCount.value > 0 ? asset.video.frameCount.value - 1 : std::max<i64>(index, 0);
    index = std::clamp(index, i64{0}, last);
    // A mesma chave de `run_video_locked` (vizinhos crus): um cache só.
    u64 key = sourceKey ^ (static_cast<u64>(index) + 0x72E46D87FA912BC3ull);
    key = (key ^ (key >> 30)) * 0xBF58476D1CE4E5B9ull;
    key ^= key >> 27;
    if (DepthMapPtr hit = find(key)) return hit;
    Job job;
    job.key = key; job.asset = asset; job.sourceKey = sourceKey; job.factory = factory;
    job.frameUs = std::max<i64>(1, static_cast<i64>(std::llround(1e6 / fps)));
    job.targetUs = static_cast<i64>(std::llround(static_cast<f64>(index) * 1e6 / fps));
    std::lock_guard<std::mutex> work(work_);
    if (DepthMapPtr hit = cached(key)) return hit;
    return run_video_raw_locked(job);
} catch (const std::bad_alloc&) {
    report_failure("memoria insuficiente para o mapa");
    return nullptr;
} catch (const std::system_error&) {
    report_failure("worker de inferencia indisponivel");
    return nullptr;
}

DepthMapPtr DepthMapService::run_video_raw_locked(const Job& job) {
    if (!job.factory) return nullptr;
    if (!ensure_model_locked()) return nullptr;
    // Um decoder por asset, reaproveitado: quadros seguidos (export, playback)
    // andam para a frente sem seek.
    if (!decoder_ || decoderAsset_ != job.sourceKey || decoderFactory_ != job.factory) {
        decoder_.reset();
        decoder_ = job.factory->open_video(job.asset, MediaPriority::Thumbnail);
        decoderAsset_ = job.sourceKey;
        decoderFactory_ = job.factory;
        decoderPts_ = -1;
        if (!decoder_) return nullptr;
    }
    VideoDecoderBackend& dec = *decoder_;
    const i64 half = std::max<i64>(1, job.frameUs / 2);
    const i64 deliverFrom = job.targetUs - half;
    const bool forward = decoderPts_ >= 0 && deliverFrom > decoderPts_
                      && job.targetUs - decoderPts_ <= std::max<i64>(dec.keyframe_interval_us(), 4 * job.frameUs);
    if (!forward) {
        if (!dec.seek_to_keyframe(job.targetUs).ok()) return nullptr;
        decoderPts_ = -1;
    }
    FrameRef frame;
    // Um GOP longo no máximo: um decoder que nunca chega ao alvo não prende o worker.
    for (u32 i = 0; i < 1200 && !frame; ++i) {
        if (cancel_.load(std::memory_order_relaxed)) return nullptr;
        i64 pts = 0;
        bool eos = false;
        if (!dec.next_frame(deliverFrom, frame, pts, eos).ok()) break;
        decoderPts_ = pts;
        if (eos && !frame) break;
    }
    if (!frame) {
        decoderPts_ = -1;   // a posição é desconhecida: o próximo pedido faz seek
        return nullptr;
    }
    // Quadro na CPU → RGBA8 com a matriz, a faixa, o recorte e a rotação do
    // próprio vídeo. 512 linhas amostradas por ponto; a redução por área até
    // 256 dá a média de 2×2 por pixel da rede.
    ThumbnailService::Image rgba;
    if (frame->format == PixelFormat::RGBA8 && frame->planes[0]) {
        const DecodedFrame& f = *frame.get();
        const u32 vw = f.visibleWidth ? f.visibleWidth : f.width;
        const u32 vh = f.visibleHeight ? f.visibleHeight : f.height;
        const u8* origin = f.planes[0] + static_cast<usize>(f.cropTop) * f.strides[0] + f.cropLeft * 4u;
        if (f.rotation == 0) return run_pixels_locked(job.key, origin, vw, vh, f.strides[0], 4);
        // Girado pelo container: a rede vê o quadro como a camada o mostra
        // (a mesma conta de `frame_to_thumbnail`), por ponto, até 512 linhas.
        const bool sideways = f.rotation == 90 || f.rotation == 270;
        const u32 dw = sideways ? vh : vw, dh = sideways ? vw : vh;
        const f32 k = std::min(1.0f, 512.0f / static_cast<f32>(std::max(dw, dh)));
        rgba.width = std::max<u32>(1, static_cast<u32>(static_cast<f32>(dw) * k));
        rgba.height = std::max<u32>(1, static_cast<u32>(static_cast<f32>(dh) * k));
        rgba.rgba.resize(static_cast<usize>(rgba.width) * rgba.height * 4);
        for (u32 oy = 0; oy < rgba.height; ++oy) for (u32 ox = 0; ox < rgba.width; ++ox) {
            const f32 u = (static_cast<f32>(ox) + 0.5f) / static_cast<f32>(rgba.width);
            const f32 v = (static_cast<f32>(oy) + 0.5f) / static_cast<f32>(rgba.height);
            f32 su = u, sv = v;
            if (f.rotation == 90) { su = v; sv = 1.0f - u; }
            else if (f.rotation == 180) { su = 1.0f - u; sv = 1.0f - v; }
            else if (f.rotation == 270) { su = 1.0f - v; sv = u; }
            const u32 sx = std::min(vw - 1, static_cast<u32>(su * static_cast<f32>(vw)));
            const u32 sy = std::min(vh - 1, static_cast<u32>(sv * static_cast<f32>(vh)));
            std::copy_n(origin + static_cast<usize>(sy) * f.strides[0] + sx * 4u, 4,
                        rgba.rgba.data() + (static_cast<usize>(oy) * rgba.width + ox) * 4);
        }
    } else if (!frame_to_thumbnail(*frame.get(), 512, rgba)) {
        return nullptr;
    }
    return run_pixels_locked(job.key, rgba.rgba.data(), rgba.width, rgba.height, rgba.width * 4, 4);
}

DepthMapPtr DepthMapService::video(u64 key, VideoSourceFactory* factory, const Asset& asset, u64 sourceKey,
                                   i64 targetUs, i64 frameUs, bool wait) try {
    if (DepthMapPtr hit = find(key)) return hit;
    if (!factory) return nullptr;
    Job job;
    job.key = key;
    job.asset = asset;
    job.sourceKey = sourceKey;
    job.targetUs = targetUs;
    job.frameUs = std::max<i64>(1, frameUs);
    job.factory = factory;
    if (wait) {
        std::lock_guard<std::mutex> work(work_);
        if (DepthMapPtr hit = cached(key)) return hit;
        const auto done = run_video_locked(job);
        if (!cancel_.load(std::memory_order_relaxed)) lastJobFailed_.store(!done, std::memory_order_relaxed);
        return done;
    }
    enqueue(std::move(job));
    return nullptr;
} catch (const std::bad_alloc&) {
    report_failure("memoria insuficiente para o mapa");
    return nullptr;
} catch (const std::system_error&) {
    report_failure("worker de inferencia indisponivel");
    return nullptr;
}

void DepthMapService::enqueue(Job job) {
    {
        std::lock_guard<std::mutex> lock(queueMutex_);
        if (hasActive_ && activeKey_ == job.key) return;
        auto same = std::find_if(pending_.begin(), pending_.end(), [&job](const Job& queued) {
            return queued.imageSize == job.imageSize && queued.sourceKey == job.sourceKey;
        });
        if (same != pending_.end()) {
            if (same->key == job.key) return;
            *same = std::move(job); // preserve this source's turn in the FIFO
        } else {
            // Retain older sources when saturated so continuous playback cannot
            // repeatedly evict the same layer before it ever gets an inference.
            if (pending_.size() >= kMaxPending) return;
            pending_.push_back(std::move(job));
        }
        cancel_.store(false, std::memory_order_relaxed);
        if (!running_) {
            try {
                thread_ = std::thread([this] { thread_main(); });
                running_ = true;
            } catch (...) {
                // A failed thread launch must not leave a phantom running
                // worker or a queued key that prevents the next retry.
                pending_.clear();
                throw;
            }
        }
    }
    wake_.notify_one();
}

void DepthMapService::thread_main() noexcept {
    set_current_thread_name("aurea-depth");
    set_current_thread_priority(ThreadPriority::Background);
    for (;;) {
        Job job;
        {
            std::unique_lock<std::mutex> lock(queueMutex_);
            const auto ready = [&] { return !running_ || !pending_.empty(); };
            if (!wake_.wait_for(lock, std::chrono::milliseconds(kIdleReleaseMs), ready)) {
                // Ocioso: a rede e o decoder saem da memória; voltam no próximo pedido.
                lock.unlock();
                trim();
                continue;
            }
            if (!running_) break;
            job = std::move(pending_.front());
            pending_.pop_front();
            activeKey_ = job.key;
            hasActive_ = true;
        }
        DepthMapPtr done;
        try {
            {
                std::lock_guard<std::mutex> work(work_);
                done = cached(job.key);
                if (!done) done = job.imageSize ? run_pixels_locked(job.key,job.pixels.data(),job.imageSize,job.imageSize,job.imageSize*4,4) : run_video_locked(job);
            }
            if (done && !job.imageSize) {
                publish_latest(job.sourceKey, job.key, job.targetUs, job.frameUs);
            }
        } catch (const std::bad_alloc&) {
            report_failure("memoria insuficiente para o mapa");
        } catch (const std::system_error&) {
            report_failure("worker de inferencia indisponivel");
        }
        if (!cancel_.load(std::memory_order_relaxed)) lastJobFailed_.store(!done, std::memory_order_relaxed);
        void (*fn)(void*) = nullptr;
        void* ctx = nullptr;
        {
            std::lock_guard<std::mutex> lock(queueMutex_);
            hasActive_ = false;
            fn = readyFn_;
            ctx = readyCtx_;
        }
        if (done && fn) fn(ctx);
    }
}

void DepthMapService::trim() noexcept {
    std::lock_guard<std::mutex> work(work_);
    estimator_.unload();
    foreground_.unload();
    decoder_.reset();
    decoderAsset_ = 0;
    decoderFactory_ = nullptr;
    decoderPts_ = -1;
    std::lock_guard<std::mutex> lock(mutex_);
    stats_.modelLoaded = false;
}

void DepthMapService::clear() noexcept {
    {
        std::lock_guard<std::mutex> lock(queueMutex_);
        running_ = false;
        pending_.clear();
        cancel_.store(true, std::memory_order_relaxed);
    }
    wake_.notify_all();
    if (thread_.joinable()) thread_.join();
    trim();
    std::lock_guard<std::mutex> work(work_);
    modelFailed_ = false;
    lastJobFailed_.store(false, std::memory_order_relaxed);
    { std::lock_guard<std::mutex> queue(queueMutex_); hasActive_ = false; }
    cancel_.store(false, std::memory_order_relaxed);
    std::lock_guard<std::mutex> lock(mutex_);
    index_.clear();
    lru_.clear();
    latest_.clear();
}

} // namespace aurea::ai
