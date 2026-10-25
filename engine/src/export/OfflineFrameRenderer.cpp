#include "aurea/export/OfflineFrameRenderer.hpp"
#include "aurea/project/Project.hpp"
#include "aurea/core/Log.hpp"
#include "aurea/core/Time.hpp"
#include <chrono>

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
Status OfflineFrameRenderer::wait_gpu(u64 frame, const std::atomic<bool>* cancel, std::mutex* pollingMutex,
                                      const std::atomic<bool>* additionalCancel) noexcept {
    if (!frame) return OkStatus;
    const u64 begin = monotonic_ns();
    for (;;) {
        if ((cancel && cancel->load(std::memory_order_acquire))
            || (additionalCancel && additionalCancel->load(std::memory_order_acquire))) return Errc::Cancelled;
        Status ready;
        {
            std::unique_lock<std::mutex> gpuLock;
            if (pollingMutex) gpuLock = std::unique_lock<std::mutex>(*pollingMutex);
            ready = gpu_->wait_frame(frame, pollingMutex ? 0 : 100'000'000ull);
        }
        if (ready.ok() || ready.code() != Errc::Timeout) return ready;
        if (monotonic_ns() - begin >= fenceTimeoutNs_)
            return Status{Errc::Timeout, "GPU sem resposta no renderizador offline"};
        if (pollingMutex) {
            std::unique_lock lock(wakeMutex_);
            wake_.wait_for(lock, std::chrono::milliseconds(2));
        }
    }
}
Status OfflineFrameRenderer::render(const Project& project, CompositionId composition,
    FrameIndex time, const OffscreenTarget& target,
    const ImagePixels* (*images)(void*, AssetId), void* imageContext,
    const RenderSettings& requested, u64& submittedFrame, std::mutex* pollingMutex,
    const std::atomic<bool>* additionalCancel) noexcept {
    submittedFrame = 0;
    if (!gpu_ || !renderer_.ready()) return Errc::InvalidState;
    const auto* comp = project.timeline().composition(composition);
    if (!comp) return Errc::NotFound;
    RenderSettings settings = requested;
    settings.finalQuality = true;
    settings.dither = false;
    const auto cancelled = [&] {
        return (settings.cancelFlag && settings.cancelFlag->load(std::memory_order_acquire))
            || (additionalCancel && additionalCancel->load(std::memory_order_acquire));
    };
    const u64 start = monotonic_ns();
    u64 lastProgress = start, lastWork = media_.decode_work(), lastGeneration = generation_.load();
    // 0.3.2: "sem progresso" conta só o tempo ESPERANDO (GPU e fontes), não o
    // trabalho síncrono do quadro — um Roto/IA calculado aqui dentro (até 90 s
    // no export) fazia o quadro parecer travado e a escada descer como se a
    // GPU tivesse estourado o prazo (docs/diagnostico/export-engine-2.0.md, 1.4).
    u64 idleSinceProgress = 0;
    bool reclaimed = false, reopened = false;
    for (;;) {
        if (cancelled()) return Errc::Cancelled;
        // Opt-in PreRender ownership: GPU recording stays serialized, but
        // decoder/fence polling never holds the editing/surface mutex.
        std::unique_lock<std::mutex> gpuLock;
        if (pollingMutex) gpuLock = std::unique_lock<std::mutex>(*pollingMutex);
        if (cancelled()) return Errc::Cancelled;
        const u64 observed = generation_.load(std::memory_order_acquire);
        // O prazo do quadro exato já passou (ver abaixo): a fonte pode
        // oferecer o quadro do mesmo trecho que estiver mais perto.
        const bool afterFirst = approximateFrames_ > 0;
        const u64 checkNs = monotonic_ns();
        const bool late = approximateRun_ < kMaxApproximateRun
            && (checkNs - lastProgress >= (afterFirst ? deadlines_.approximateAfterNs : deadlines_.approximateNs)
                || checkNs - start >= (afterFirst ? deadlines_.approximateTotalAfterNs : deadlines_.approximateTotalNs));
        settings.approximateAhead = late;
        (void)renderer_.take_incomplete();
        renderer_.prepare(*comp, project, time, &media_, images, imageContext,
                          settings, ++sequence_, 1, DecodeMode::Playback, 1.0f, snapshot_);
        comp->layers().for_each([&](LayerId id, const Layer& layer) {
            if (layer.kind == LayerKind::Video && layer.contains_time(time)) media_.touch(id, sequence_);
        });
        media_.collect(sequence_, 2);
        const bool exact = !snapshot_.missingVideoFrames && !snapshot_.staleVideoFrames;
        // 0.3.0: o quadro exato que NUNCA chega (decoder do aparelho preso num
        // ponto do clipe: "trava sempre no quadro 302", 1080p60 de celular)
        // parava o export e todo degrau da escada parava igual, porque trocar
        // encoder ou resolução não muda o decoder. Depois de reabrir a fonte e
        // esperar, sai o quadro mais próximo que a fonte tem — só do MESMO
        // trecho do clipe (Renderer: video_fallback_allowed descarta o do outro
        // lado do corte) — e as telas avisam (kExportFrameFallback).
        const bool approximate = late && !exact && !snapshot_.missingVideoFrames && snapshot_.staleVideoFrames;
        if (exact || approximate) {
            FrameStats stats;
            RenderTimings timings;
            const Status rendered = renderer_.render(snapshot_, settings, &target, stats, timings);
            submittedFrame = gpu_->last_submitted_frame();
            if (rendered.code() == Errc::OutOfDeviceMemory && !reclaimed) {
                snapshot_.release_video_frames();
                if (gpuLock.owns_lock()) gpuLock.unlock();
                if (const Status ready = wait_gpu(submittedFrame, settings.cancelFlag, pollingMutex, additionalCancel); !ready.ok()) return ready;
                if (pollingMutex) gpuLock.lock();
                if (const Status ready = renderer_.reclaim_failed_transients(100'000'000ull); !ready.ok()) return ready;
                reclaimed = true;
                continue;
            }
            if (!rendered.ok()) return rendered;
            if (approximate) {
                (void)renderer_.take_incomplete();
                approximate_ = true;
                ++approximateFrames_;
                ++approximateRun_;
                AUREA_LOG_WARN("export: quadro %lld sem o quadro exato da fonte em %llu ms; usado o mais proximo do trecho (%u na sessao)",
                               static_cast<long long>(time.value), static_cast<unsigned long long>((checkNs - start) / 1'000'000ull),
                               approximateFrames_);
                return OkStatus;
            }
            if (!renderer_.take_incomplete()) { approximateRun_ = 0; return OkStatus; }
        }
        snapshot_.release_video_frames();
        const u64 pending = gpu_->last_submitted_frame();
        if (gpuLock.owns_lock()) gpuLock.unlock();
        const u64 waitBegin = monotonic_ns();
        if (const Status ready = wait_gpu(pending, settings.cancelFlag, pollingMutex, additionalCancel); !ready.ok()) return ready;
        const u64 now = monotonic_ns(), work = media_.decode_work();
        idleSinceProgress += now - waitBegin;
        const u64 resources = generation_.load(std::memory_order_acquire)
            + (static_cast<u64>(renderer_.shaders().compiles_since_mark()) << 32)
            + (static_cast<u64>(renderer_.pending_uploads()) << 48)
            + (renderer_.environment_pending() ? 1ull << 63 : 0ull);
        if (work != lastWork || resources != lastGeneration) {
            lastProgress = now; lastWork = work; lastGeneration = resources;
            idleSinceProgress = 0;
        }
        // Decoder parado sem entregar o quadro: reabre a fonte de quem espera
        // (uma vez por quadro). Um codec do aparelho preso depois de um seek
        // volta a andar com outro codec; reabrir também refaz o seek.
        if (!exact && !reopened
            && now - lastProgress >= (approximateFrames_ ? deadlines_.reopenAfterNs : deadlines_.reopenNs)) {
            reopened = true;
            u32 closed = 0;
            for (const FrameSnapshot::VideoSourceWait& w : snapshot_.videoSourceWaits) {
                if (w.opening || (!w.missing && !w.stale)) continue;
                media_.close_layer(w.layer);
                ++closed;
            }
            if (closed) {
                AUREA_LOG_WARN("export: quadro %lld sem o quadro exato ha %llu ms; %u fonte(s) de video reaberta(s)",
                               static_cast<long long>(time.value),
                               static_cast<unsigned long long>((now - lastProgress) / 1'000'000ull), closed);
                lastProgress = now;
                idleSinceProgress = 0;
            }
        }
        if (now - start > fenceTimeoutNs_ || idleSinceProgress > 30'000'000'000ull)
            return Status{exact ? Errc::Timeout : Errc::DecodeFailed,
                exact ? "recurso de composicao indisponivel; quadro offline nao foi codificado"
                      : "a fonte nao entregou o quadro exato; exportacao interrompida"};
        std::unique_lock lock(wakeMutex_);
        const u64 sleepBegin = monotonic_ns();
        wake_.wait_for(lock, std::chrono::milliseconds(10), [&] {
            return generation_.load(std::memory_order_acquire) != observed || cancelled();
        });
        idleSinceProgress += monotonic_ns() - sleepBegin;
    }
}
void OfflineFrameRenderer::shutdown() noexcept {
    snapshot_ = FrameSnapshot{};
    media_.close_all();
    renderer_.shutdown();
    gpu_ = nullptr;
}
bool OfflineFrameRenderer::retire_sources() noexcept {
    snapshot_ = FrameSnapshot{};
    return media_.release_preview_for_export(nullptr, 1).ok();
}
void OfflineFrameRenderer::retire_gpu_after_done() noexcept {
    snapshot_ = FrameSnapshot{};
    renderer_.shutdown_after_gpu_done();
    gpu_ = nullptr;
}
void OfflineFrameRenderer::forget_device() noexcept {
    detach_device();
    media_.close_all();
}
void OfflineFrameRenderer::detach_device() noexcept {
    snapshot_ = FrameSnapshot{};
    renderer_.forget_device();
    gpu_ = nullptr;
}
} // namespace aurea
