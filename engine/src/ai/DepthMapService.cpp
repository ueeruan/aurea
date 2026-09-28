#include "aurea/ai/DepthMapService.hpp"

#include "aurea/core/Log.hpp"
#include "aurea/core/Thread.hpp"
#include "aurea/media/MediaManager.hpp"
#include "aurea/media/ThumbnailService.hpp"

#include <algorithm>
#include <chrono>

namespace aurea::ai {

DepthMapService::DepthMapService() = default;

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

void DepthMapService::insert(u64 key, DepthMapPtr map) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (auto it = index_.find(key); it != index_.end()) {
        it->second->second = std::move(map);
        lru_.splice(lru_.begin(), lru_, it->second);
        return;
    }
    lru_.emplace_front(key, std::move(map));
    index_[key] = lru_.begin();
    while (lru_.size() > kMaxCached) {
        index_.erase(lru_.back().first);
        lru_.pop_back();
    }
}

DepthMapService::Stats DepthMapService::stats() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return stats_;
}

bool DepthMapService::ensure_model_locked() {
    if (estimator_.loaded()) return true;
    if (modelFailed_) return false;
    const Status s = estimator_.load(DepthEstimator::Backend::Auto);
    std::lock_guard<std::mutex> lock(mutex_);
    stats_.modelLoaded = s.ok();
    stats_.backend = estimator_.backend();
    if (!s.ok()) {
        // Não tenta de novo a cada quadro: só depois de `clear` (outro projeto).
        modelFailed_ = true;
        ++stats_.failures;
        AUREA_LOG_WARN("profundidade: modelo nao carregou (%s)", s.message().data());
    }
    return s.ok();
}

DepthMapPtr DepthMapService::run_pixels_locked(u64 key, const u8* pixels, u32 width, u32 height, u32 stride,
                                               u32 channels) {
    if (!ensure_model_locked()) return nullptr;
    auto map = std::make_shared<DepthMap>();
    map->disparity.resize(DepthEstimator::kPixels);
    const Status s = estimator_.run(pixels, width, height, stride, channels, cancel_, map->disparity.data());
    if (!s.ok()) {
        std::lock_guard<std::mutex> lock(mutex_);
        ++stats_.failures;
        if (s.code() != Errc::Cancelled) AUREA_LOG_WARN("profundidade: inferencia falhou (%s)", s.message().data());
        return nullptr;
    }
    depth_percentiles(map->disparity.data(), DepthEstimator::kPixels, map->p2, map->p98);
    map->inferenceMs = estimator_.last_inference_ms();
    {
        std::lock_guard<std::mutex> lock(mutex_);
        ++stats_.inferences;
        stats_.lastInferenceMs = map->inferenceMs;
        stats_.backend = estimator_.backend();
    }
    DepthMapPtr done = std::move(map);
    insert(key, done);
    return done;
}

DepthMapPtr DepthMapService::image(u64 key, const u8* pixels, u32 width, u32 height, u32 stride, u32 channels) {
    if (DepthMapPtr hit = find(key)) return hit;
    if (!pixels || !width || !height) return nullptr;
    std::lock_guard<std::mutex> work(work_);
    if (DepthMapPtr hit = cached(key)) return hit;   // outra thread terminou enquanto esperávamos
    return run_pixels_locked(key, pixels, width, height, stride, channels);
}

DepthMapPtr DepthMapService::run_video_locked(const Job& job) {
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
                                   i64 targetUs, i64 frameUs, bool wait) {
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
        return run_video_locked(job);
    }
    {
        std::lock_guard<std::mutex> lock(queueMutex_);
        if (hasPending_ && pending_.key == key) return nullptr;
        pending_ = std::move(job);
        hasPending_ = true;
        cancel_.store(false, std::memory_order_relaxed);
        if (!running_) {
            running_ = true;
            thread_ = std::thread([this] { thread_main(); });
        }
    }
    wake_.notify_one();
    return nullptr;
}

void DepthMapService::thread_main() noexcept {
    set_current_thread_name("aurea-depth");
    set_current_thread_priority(ThreadPriority::Background);
    for (;;) {
        Job job;
        {
            std::unique_lock<std::mutex> lock(queueMutex_);
            const auto ready = [&] { return !running_ || hasPending_; };
            if (!wake_.wait_for(lock, std::chrono::milliseconds(kIdleReleaseMs), ready)) {
                // Ocioso: a rede e o decoder saem da memória; voltam no próximo pedido.
                lock.unlock();
                trim();
                continue;
            }
            if (!running_) break;
            job = std::move(pending_);
            hasPending_ = false;
        }
        DepthMapPtr done;
        {
            std::lock_guard<std::mutex> work(work_);
            done = cached(job.key);
            if (!done) done = run_video_locked(job);
        }
        void (*fn)(void*) = nullptr;
        void* ctx = nullptr;
        {
            std::lock_guard<std::mutex> lock(queueMutex_);
            fn = readyFn_;
            ctx = readyCtx_;
        }
        if (done && fn) fn(ctx);
    }
}

void DepthMapService::trim() noexcept {
    std::lock_guard<std::mutex> work(work_);
    estimator_.unload();
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
        hasPending_ = false;
        cancel_.store(true, std::memory_order_relaxed);
    }
    wake_.notify_all();
    if (thread_.joinable()) thread_.join();
    trim();
    std::lock_guard<std::mutex> work(work_);
    modelFailed_ = false;
    cancel_.store(false, std::memory_order_relaxed);
    std::lock_guard<std::mutex> lock(mutex_);
    index_.clear();
    lru_.clear();
}

} // namespace aurea::ai
