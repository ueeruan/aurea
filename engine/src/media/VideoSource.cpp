#include "aurea/media/VideoSource.hpp"
#include "aurea/core/Log.hpp"
#include "aurea/core/Thread.hpp"
#include "aurea/core/Time.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>

namespace aurea {

VideoSource::VideoSource(std::unique_ptr<VideoDecoderBackend> backend, MediaPriority priority)
    : backend_(std::move(backend)), priority_(priority) {
    const f64 fps = backend_->info().fps > 0.0 ? backend_->info().fps : 30.0;
    frameUs_ = static_cast<i64>(std::llround(1'000'000.0 / fps));

    // O cache nunca segura todos os buffers do decoder: sobram os frames em voo
    // na GPU (até 3) e um de folga para o decoder escrever. Sem essa folga o
    // MediaCodec trava esperando buffer, e o preview congela.
    DecodedFrameCache::Config cfg;
    const u32 live = backend_->max_live_frames();
    cfg.maxFrames = live > 5 ? live - 5 : 1;
    cache_.configure(cfg);
}

VideoSource::~VideoSource() { stop(); }

void VideoSource::start() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (running_) return;
    running_ = true;
    thread_ = std::thread([this] { thread_main(); });
}

void VideoSource::stop() noexcept {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!running_) return;
        running_ = false;
    }
    wake_.notify_all();
    if (thread_.joinable()) thread_.join();
    cache_.clear();
}

void VideoSource::request(const DecodeRequest& r) noexcept {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        // Mesmo pedido de novo (o render pede a cada vsync): não acorda ninguém.
        if (requestGen_ != 0 && r.targetUs == request_.targetUs && r.mode == request_.mode
            && r.direction == request_.direction) {
            return;
        }
        if (requestGen_ != handledGen_) {
            std::lock_guard<std::mutex> s(statsMutex_);
            ++stats_.coalesced;
        }
        request_ = r;
        ++requestGen_;
        requestTimeNs_ = monotonic_ns();
        std::lock_guard<std::mutex> s(statsMutex_);
        ++stats_.requests;
    }
    wake_.notify_one();
}

FrameRef VideoSource::frame_for(i64 targetUs, bool* exact) noexcept {
    return cache_.find(targetUs, frameUs_ / 2, exact);
}

void VideoSource::set_ready_callback(void (*fn)(void*), void* ctx) noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    readyFn_ = fn;
    readyCtx_ = ctx;
}

bool VideoSource::wait_for(i64 targetUs, u32 timeoutMs) noexcept {
    std::unique_lock<std::mutex> lock(mutex_);
    return delivered_.wait_for(lock, std::chrono::milliseconds(timeoutMs), [&] {
        return cache_.contains(targetUs, frameUs_ / 2) || !running_;
    });
}

void VideoSource::suspend() noexcept {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        suspended_ = true;
    }
    wake_.notify_all();
}

void VideoSource::resume() noexcept {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        suspended_ = false;
        ++requestGen_;   // refaz o último pedido depois de recriar o codec
    }
    wake_.notify_all();
}

VideoSource::Stats VideoSource::stats() const noexcept {
    std::lock_guard<std::mutex> lock(statsMutex_);
    Stats s = stats_;
    s.cache = cache_.stats();
    return s;
}

bool VideoSource::reachable_forward(i64 needUs) const noexcept {
    if (!decoderValid_ || eos_) return false;
    const i64 half = frameUs_ / 2;
    if (needUs <= decoderPosUs_ - half) return false;               // já passou
    return needUs - decoderPosUs_ <= backend_->keyframe_interval_us();   // andar custa menos que seek
}

void VideoSource::deliver(FrameRef frame) noexcept {
    (void)cache_.insert(std::move(frame));
    void (*fn)(void*) = nullptr;
    void* ctx = nullptr;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        fn = readyFn_;
        ctx = readyCtx_;
    }
    delivered_.notify_all();
    if (fn) fn(ctx);
}

void VideoSource::thread_main() noexcept {
    set_current_thread_name(priority_ == MediaPriority::Thumbnail ? "aurea-thumb" : "aurea-decode");
    set_current_thread_priority(priority_ == MediaPriority::Thumbnail ? ThreadPriority::Background
                                                                       : ThreadPriority::Decode);
    const i64 half = frameUs_ / 2;
    const i64 durationUs = backend_->info().durationUs;

    for (;;) {
        DecodeRequest req;
        u64 gen = 0;
        u64 requestNs = 0;
        bool applySuspend = false, applyResume = false;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            auto prefetch_pending = [&] {
                if (request_.mode != DecodeMode::Playback || eos_ || suspended_) return false;
                const i64 ahead = frameUs_ * (request_.speed > 1.5f ? 4 : 3);
                return cache_.contiguous_end(request_.targetUs, frameUs_) < request_.targetUs + ahead - half;
            };
            wake_.wait(lock, [&] {
                return !running_ || requestGen_ != handledGen_ || suspended_ != suspendApplied_
                    || prefetch_pending();
            });
            if (!running_) break;
            if (suspended_ != suspendApplied_) {
                applySuspend = suspended_;
                applyResume = !suspended_;
                suspendApplied_ = suspended_;
            }
            req = request_;
            gen = requestGen_;
            handledGen_ = gen;
            requestNs = requestTimeNs_;
        }

        if (applySuspend) {
            // Segundo plano: o codec de hardware é devolvido ao sistema e os
            // buffers do ImageReader são soltos (o cache os segurava).
            backend_->suspend();
            cache_.clear();
            decoderValid_ = false;
            continue;
        }
        if (applyResume) {
            if (const Status s = backend_->resume(); !s.ok()) {
                AUREA_LOG_ERROR("decoder nao voltou do segundo plano: %s", s.message().data());
            }
            decoderValid_ = false;
            eos_ = false;
        }
        if (suspendApplied_ || req.mode == DecodeMode::Idle) continue;

        cache_.set_focus(req.targetUs, req.direction);

        // Qual frame falta, e até onde ir.
        i64 need = req.targetUs;
        i64 limit = req.targetUs;
        if (req.mode == DecodeMode::Playback) {
            const i64 ahead = frameUs_ * (req.speed > 1.5f ? 4 : 3);
            const i64 end = cache_.contiguous_end(req.targetUs, frameUs_);
            if (end >= req.targetUs + ahead - half) continue;   // já está adiantado
            need = end >= req.targetUs - half ? end + frameUs_ : req.targetUs;
            limit = req.targetUs + ahead;
        } else {
            if (cache_.contains(req.targetUs, half)) continue;
            // Arrastando para a frente, o decoder já está andando: deixa dois
            // frames prontos adiante. Para trás, não há embalo a aproveitar.
            if (req.mode == DecodeMode::Scrub && req.direction > 0) limit = need + 2 * frameUs_;
        }
        if (durationUs > 0 && need > durationUs - half) need = std::max<i64>(0, durationUs - frameUs_);
        if (eos_ && need > decoderPosUs_) continue;   // pedido além do fim: o último frame já está no cache

        if (!reachable_forward(need)) {
            if (const Status s = backend_->seek_to_keyframe(need); !s.ok()) {
                AUREA_LOG_ERROR("seek falhou em %lld us", static_cast<long long>(need));
                decoderValid_ = false;
                continue;
            }
            decoderValid_ = true;
            decoderPosUs_ = -1;   // desconhecido até o primeiro frame sair
            eos_ = false;
            std::lock_guard<std::mutex> s(statsMutex_);
            ++stats_.seeks;
        }
        const i64 seekTarget = need;

        for (;;) {
            // COALESCÊNCIA: chegou pedido novo enquanto decodificava?
            {
                std::lock_guard<std::mutex> lock(mutex_);
                if (!running_ || suspended_) break;
                if (requestGen_ != gen) {
                    const DecodeRequest nr = request_;
                    const bool sameKind = (nr.mode == DecodeMode::Playback) == (req.mode == DecodeMode::Playback);
                    const i64 pos = decoderPosUs_ >= 0 ? decoderPosUs_ : seekTarget - half;
                    const bool ahead = nr.targetUs > pos - half
                                    && (decoderPosUs_ < 0 ? nr.targetUs >= seekTarget
                                                          : nr.targetUs - pos <= backend_->keyframe_interval_us());
                    if (!sameKind || !ahead) break;   // o laço externo decide (talvez um seek)
                    // O alvo novo está à frente e ao alcance: segue andando.
                    req = nr;
                    gen = requestGen_;
                    handledGen_ = gen;
                    requestNs = requestTimeNs_;
                    need = nr.targetUs;
                    limit = (nr.mode == DecodeMode::Playback)
                          ? nr.targetUs + frameUs_ * (nr.speed > 1.5f ? 4 : 3)
                          : (nr.mode == DecodeMode::Scrub && nr.direction > 0 ? need + 2 * frameUs_ : need);
                    std::lock_guard<std::mutex> s(statsMutex_);
                    ++stats_.forwardRetargets;
                }
            }

            const i64 deliverFrom = (req.mode == DecodeMode::Playback) ? req.targetUs - half : need - half;
            FrameRef frame;
            i64 pts = 0;
            bool eos = false;
            const u64 t0 = monotonic_ns();
            const Status s = backend_->next_frame(deliverFrom, frame, pts, eos);
            const f32 ms = static_cast<f32>(static_cast<f64>(monotonic_ns() - t0) * 1e-6);
            if (!s.ok()) {
                AUREA_LOG_ERROR("decode falhou: %s", s.message().data());
                decoderValid_ = false;
                // Sem esta pausa, um arquivo corrompido faria a thread girar
                // em seek+erro sem parar, queimando CPU que o render precisa.
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
                break;
            }
            decoderPosUs_ = pts;
            {
                std::lock_guard<std::mutex> st(statsMutex_);
                stats_.decodeMsAvg = stats_.decodeMsAvg == 0.0f ? ms : stats_.decodeMsAvg * 0.9f + ms * 0.1f;
                if (frame) ++stats_.framesDelivered;
                else ++stats_.framesDiscarded;
                if (frame && std::llabs(pts - need) <= half && requestNs) {
                    stats_.lastSeekMs = static_cast<f32>(static_cast<f64>(monotonic_ns() - requestNs) * 1e-6);
                }
                stats_.endOfStream = eos;
            }
            if (frame) deliver(std::move(frame));
            if (eos) { eos_ = true; break; }
            if (pts >= limit - half) break;
        }
    }
}

} // namespace aurea
