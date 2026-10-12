#include "aurea/export/OfflineFrameRenderer.hpp"
#include "aurea/project/Project.hpp"
#include "aurea/core/Time.hpp"
#include <chrono>
#include <cstdio>
#include "aurea/core/Log.hpp"

namespace aurea {
void OfflineFrameRenderer::source_ready(void* p) noexcept {
    auto& self = *static_cast<OfflineFrameRenderer*>(p);
    self.generation_.fetch_add(1, std::memory_order_release);
    self.wake_.notify_all();
}
Status OfflineFrameRenderer::initialize(GPUBackend& gpu, const EffectRegistry& effects,
    VideoSourceFactory* sources, MemoryManager* memory, u64 budget, u64 timeout) noexcept {
    if (gpu_) return Errc::InvalidState;
    gpu_ = &gpu;
    fenceTimeoutNs_ = timeout ? timeout : 120'000'000'000ull;
    media_.set_factory(sources);
    media_.set_memory(memory);
    media_.set_ready_callback(&OfflineFrameRenderer::source_ready, this);
    renderer_.set_transient_cache_budget(budget);
    renderer_.set_preview_cache_budget(0);
    renderer_.set_flow_cache_budget(16ull << 20);
    const Status status = renderer_.initialize(gpu, effects);
    // Engine owns fence-bounded cleanup even when initialization fails after
    // a resource upload. Do not enter Renderer::shutdown's idle wait here.
    if (!status.ok()) return status;
    return OkStatus;
}
Status OfflineFrameRenderer::wait_gpu(u64 frame, const std::atomic<bool>* cancel) noexcept {
    if (!frame) return OkStatus;
    const u64 begin = monotonic_ns();
    for (;;) {
        if (cancel && cancel->load(std::memory_order_acquire)) return Errc::Cancelled;
        const Status ready = gpu_->wait_frame(frame, 100'000'000ull);
        if (ready.ok() || ready.code() != Errc::Timeout) return ready;
        if (monotonic_ns() - begin >= fenceTimeoutNs_)
            return Status{Errc::Timeout, "GPU sem resposta no renderizador offline"};
    }
}
Status OfflineFrameRenderer::render(const Project& project, CompositionId composition,
    FrameIndex time, const OffscreenTarget& target,
    const ImagePixels* (*images)(void*, AssetId), void* imageContext,
    const RenderSettings& requested, u64& submittedFrame) noexcept {
    submittedFrame = 0;
    if (!gpu_ || !renderer_.ready()) return Errc::InvalidState;
    const auto* comp = project.timeline().composition(composition);
    if (!comp) return Errc::NotFound;
    RenderSettings settings = requested;
    settings.finalQuality = true;
    settings.dither = false;
    const auto cancelled = [&] { return settings.cancelFlag && settings.cancelFlag->load(std::memory_order_acquire); };
    const u64 start = monotonic_ns();
    u64 lastProgress = start, lastWork = media_.decode_work(), lastGeneration = generation_.load();
    bool reclaimed = false;
    for (;;) {
        if (cancelled()) return Errc::Cancelled;
        const u64 observed = generation_.load(std::memory_order_acquire);
        (void)renderer_.take_incomplete();
        renderer_.prepare(*comp, project, time, &media_, images, imageContext,
                          settings, ++sequence_, 1, DecodeMode::Playback, 1.0f, snapshot_);
        comp->layers().for_each([&](LayerId id, const Layer& layer) {
            if (layer.kind == LayerKind::Video && layer.contains_time(time)) media_.touch(id, sequence_);
        });
        media_.collect(sequence_, 2);
        const bool exact = !snapshot_.missingVideoFrames && !snapshot_.staleVideoFrames;
        if (exact) {
            FrameStats stats;
            RenderTimings timings;
            const Status rendered = renderer_.render(snapshot_, settings, &target, stats, timings);
            submittedFrame = gpu_->last_submitted_frame();
            if (rendered.code() == Errc::OutOfDeviceMemory && !reclaimed) {
                snapshot_.release_video_frames();
                if (const Status ready = wait_gpu(submittedFrame, settings.cancelFlag); !ready.ok()) return ready;
                if (const Status ready = renderer_.reclaim_failed_transients(100'000'000ull); !ready.ok()) return ready;
                reclaimed = true;
                continue;
            }
            if (!rendered.ok()) return rendered;
            if (!renderer_.take_incomplete()) return OkStatus;
        }
        snapshot_.release_video_frames();
        if (const Status ready = wait_gpu(gpu_->last_submitted_frame(), settings.cancelFlag); !ready.ok()) return ready;
        const u64 now = monotonic_ns(), work = media_.decode_work();
        const u64 resources = generation_.load(std::memory_order_acquire)
            + (static_cast<u64>(renderer_.shaders().compiles_since_mark()) << 32)
            + (static_cast<u64>(renderer_.pending_uploads()) << 48)
            + (renderer_.environment_pending() ? 1ull << 63 : 0ull);
        if (work != lastWork || resources != lastGeneration) {
            lastProgress = now; lastWork = work; lastGeneration = resources;
        }
        if (now - start > fenceTimeoutNs_ || now - lastProgress > 30'000'000'000ull) {
            if (!exact) return Status{Errc::DecodeFailed, "a fonte nao entregou o quadro exato; exportacao interrompida"};
            // O motivo leva o ponto exato do motor que deixou o quadro incompleto.
            static thread_local char detail[200];
            const char* file = renderer_.incomplete_file();
            const char* base = file ? file : "?";
            for (const char* c = base; c && *c; ++c) if (*c == '/' || *c == '\\') base = c + 1;
            std::snprintf(detail, sizeof(detail), "recurso de composicao indisponivel (%s:%u); quadro offline nao foi codificado",
                          base, renderer_.incomplete_line());
            AUREA_LOG_ERROR("export offline: quadro %lld incompleto por 30 s em %s:%u", static_cast<long long>(time.value),
                            base, renderer_.incomplete_line());
            return Status{Errc::Timeout, detail};
        }
        std::unique_lock lock(wakeMutex_);
        wake_.wait_for(lock, std::chrono::milliseconds(10), [&] {
            return generation_.load(std::memory_order_acquire) != observed || cancelled();
        });
    }
}
void OfflineFrameRenderer::shutdown() noexcept {
    snapshot_ = FrameSnapshot{};
    media_.close_all();
    renderer_.shutdown();
    gpu_ = nullptr;
}
bool OfflineFrameRenderer::release(bool deviceLost, u64 timeoutNs) noexcept {
    if (deviceLost) lostDevice_ = true;
    snapshot_ = FrameSnapshot{};
    if (!media_.close_all_for(timeoutNs)) return false;
    if (gpu_) {
        if (lostDevice_) renderer_.forget_device();
        else renderer_.shutdown();
    }
    gpu_ = nullptr;
    return true;
}

void OfflineFrameRenderer::forget_device() noexcept {
    snapshot_ = FrameSnapshot{};
    media_.close_all();
    renderer_.forget_device();
    gpu_ = nullptr;
}
} // namespace aurea
