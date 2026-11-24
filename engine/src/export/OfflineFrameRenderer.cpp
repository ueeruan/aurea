#include "aurea/export/OfflineFrameRenderer.hpp"
#include "aurea/export/ExportDiagnostics.hpp"
#include "aurea/export/ExportMemory.hpp"
#include "aurea/project/Project.hpp"
#include "aurea/core/Log.hpp"
#include "aurea/core/Time.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace aurea {
namespace {
// Scope accounting survives every return without changing wait/error handling.
struct ScopedElapsedTime {
    u64& accumulated;
    u64 begin = monotonic_ns();
    ~ScopedElapsedTime() { accumulated += monotonic_ns() - begin; }
};

/// Só o nome do arquivo do ponto (o caminho de compilação não interessa).
const char* site_name(const char* file) noexcept {
    if (!file) return "?";
    const char* base = file;
    for (const char* c = file; *c; ++c) if (*c == '/' || *c == '\\') base = c + 1;
    return base;
}
} // namespace
bool OfflineFrameRenderer::site_unresolved(const char* file, u32 line) const noexcept {
    for (u32 i = 0; i < unresolvedCount_; ++i)
        if (unresolved_[i].line == line && unresolved_[i].file && file && std::strcmp(unresolved_[i].file, file) == 0)
            return true;
    return false;
}
void OfflineFrameRenderer::remember_unresolved(const char* file, u32 line) noexcept {
    if (site_unresolved(file, line)) return;
    if (unresolvedCount_ < kMaxUnresolvedSites) unresolved_[unresolvedCount_++] = Site{file, line};
}
void OfflineFrameRenderer::source_ready(void* p) noexcept {
    auto& self = *static_cast<OfflineFrameRenderer*>(p);
    self.generation_.fetch_add(1, std::memory_order_release);
    self.wake_.notify_all();
}
Status OfflineFrameRenderer::initialize(GPUBackend& gpu, const EffectRegistry& effects,
    VideoSourceFactory* sources, MemoryManager* memory, u64 budget, u64 timeout,
    const std::atomic<bool>* cancel) noexcept {
    if (gpu_) return Errc::InvalidState;
    gpu_ = &gpu;
    driverBudgetSampleNs_ = 0;
    driverHeadroom_.reset();
    fenceTimeoutNs_ = timeout ? timeout : 120'000'000'000ull;
    media_.set_factory(sources);
    media_.set_memory(memory);
    media_.set_ready_callback(&OfflineFrameRenderer::source_ready, this);
    renderer_.set_transient_cache_budget(budget);
    // Leave room for decoded media, scene targets and the encoder. Changing
    // the batch size does not change the number or order of shutter samples.
    if (budget) sceneExposureBudgetBytes_ = std::clamp<u64>(budget / 8, 1ull << 20, 32ull << 20);
    renderer_.set_preview_cache_budget(0);
    renderer_.set_flow_cache_budget(16ull << 20);
    // 0.5.3: as faixas do TimeSlice dividem o orçamento do export (metade dele,
    // até os 384 MB da prévia); antes ficavam fora de toda conta e esgotavam a
    // GPU do aparelho em 1080p.
    renderer_.set_slice_cache_budget(std::min<u64>(384ull << 20, budget / 2));
    // 0.5.2 (E4): a abertura do renderizador do export não pré-aquece a lista
    // base (compila no 1o quadro, com o vigia do render) e todo
    // pré-aquecimento dele para no cancelar do export.
    renderer_.set_startup_prewarm(false);
    renderer_.shaders().set_abort(cancel);
    const Status status = renderer_.initialize(gpu, effects);
    // Engine owns fence-bounded cleanup even when initialization fails after
    // a resource upload. Do not enter Renderer::shutdown's idle wait here.
    if (!status.ok()) return status;
    return OkStatus;
}
Status OfflineFrameRenderer::wait_gpu(u64 frame, const std::atomic<bool>* cancel, std::mutex* pollingMutex,
                                      const std::atomic<bool>* additionalCancel) noexcept {
    if (!frame) return OkStatus;
    ScopedElapsedTime measured{measurements_.fenceWaitNs};
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
    const std::atomic<bool>* additionalCancel, ExportDiagnostics* diagnostics) noexcept {
    measurements_ = {};
    ScopedElapsedTime measured{measurements_.elapsedNs};
    submittedFrame = 0;
    if (!gpu_ || !renderer_.ready()) return Errc::InvalidState;
    const auto* comp = project.timeline().composition(composition);
    if (!comp) return Errc::NotFound;
    RenderSettings settings = requested;
    settings.finalQuality = true;
    settings.warmupRevision = project.revision();
    settings.dither = false;
    settings.sceneExposureBatchBudgetBytes = serialSceneExposure_
        ? 1 : std::min(requested.sceneExposureBatchBudgetBytes, sceneExposureBudgetBytes_);
    settings.sceneExposureFenceTimeoutNs = std::min(settings.sceneExposureFenceTimeoutNs, fenceTimeoutNs_);
    settings.stageUnblurredScenes |= stageUnblurredScenes_;
    const auto cancelled = [&] {
        settings.beat();
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
    bool reclaimed = false, trimmed = false, staged = false, reopened = false;
    // 0.5.2 (E1): o ponto que deixou ESTE quadro incompleto (exato), e se há
    // algo a caminho que ainda pode resolvê-lo.
    const char* waitFile = nullptr;
    u32 waitLine = 0;
    const auto in_flight = [&]() noexcept { return renderer_.pending_uploads() != 0 || renderer_.environment_pending(); };
    for (;;) {
        if (cancelled()) return Errc::Cancelled;
        // Opt-in PreRender ownership: GPU recording stays serialized, but
        // decoder/fence polling never holds the editing/surface mutex.
        std::unique_lock<std::mutex> gpuLock;
        if (pollingMutex) gpuLock = std::unique_lock<std::mutex>(*pollingMutex);
        if (cancelled()) return Errc::Cancelled;
        if (!measurements_.attempts && settings.boundedSceneExposure && !serialSceneExposure_) {
            if (!driverBudgetSampleNs_ || start - driverBudgetSampleNs_ >= kExportGpuBudgetSampleIntervalNs) {
                driverHeadroom_ = gpu_->memory_budget().largest_device_local_headroom();
                driverBudgetSampleNs_ = start;
            }
            settings.sceneExposureBatchBudgetBytes = export_exposure_batch_budget_bytes(
                settings.sceneExposureBatchBudgetBytes, driverHeadroom_);
        }
        ++measurements_.attempts;
        const u64 observed = generation_.load(std::memory_order_acquire);
        // O prazo do quadro exato já passou (ver abaixo): a fonte pode
        // oferecer o quadro do mesmo trecho que estiver mais perto.
        const bool afterFirst = approximateFrames_ > 0;
        const u64 checkNs = monotonic_ns();
        const bool late = !requireCompleteFrames_ && approximateRun_ < kMaxApproximateRun
            && (checkNs - lastProgress >= (afterFirst ? deadlines_.approximateAfterNs : deadlines_.approximateNs)
                || checkNs - start >= (afterFirst ? deadlines_.approximateTotalAfterNs : deadlines_.approximateTotalNs));
        settings.approximateAhead = late;
        (void)renderer_.take_incomplete();
        (void)renderer_.take_memory_short();
        {
            ScopedElapsedTime preparation{measurements_.prepareNs};
            renderer_.prepare(*comp, project, time, &media_, images, imageContext,
                              settings, ++sequence_, 1, DecodeMode::Playback, 1.0f, snapshot_);
            comp->layers().for_each([&](LayerId id, const Layer& layer) {
                if (layer.kind == LayerKind::Video && layer.contains_time(time)) media_.touch(id, sequence_);
            });
            media_.collect(sequence_, 2);
        }
        const bool exact = !snapshot_.missingVideoFrames && !snapshot_.staleVideoFrames;
        if (diagnostics) diagnostics->stage(ExportDiagnostics::Lane::Producer,
            exact ? ExportDiagnostics::Stage::Render : ExportDiagnostics::Stage::Decode);
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
            ++measurements_.recordAttempts;
            u64 recordedNs = 0;
            Status rendered;
            {
                ScopedElapsedTime recording{recordedNs};
                rendered = renderer_.render(snapshot_, settings, &target, stats, timings);
            }
            // Renderer reports wall-clock acquire/exposure waits even when
            // GPU timestamp queries are unavailable. Clamp float rounding to
            // the measured enclosing call; never infer GPU execution time.
            const f64 waitNs = (static_cast<f64>(timings.acquireWaitMs) + timings.sceneExposureWaitMs) * 1e6;
            const u64 internalWaitNs = std::isfinite(waitNs)
                ? static_cast<u64>(std::clamp(waitNs, 0.0, static_cast<f64>(recordedNs))) : 0;
            measurements_.recordNs += recordedNs - internalWaitNs;
            measurements_.fenceWaitNs += internalWaitNs;
            submittedFrame = gpu_->last_submitted_frame();
            if (const bool memoryShort = renderer_.take_memory_short(); rendered.ok() && memoryShort)
                rendered = Status{Errc::BudgetExceeded, "final frame exceeds its resource budget"};
            const bool memoryFailure = rendered.code() == Errc::OutOfDeviceMemory
                || rendered.code() == Errc::OutOfMemory || rendered.code() == Errc::BudgetExceeded;
            if (memoryFailure) {
                // 1.0.7.1: tudo já foi liberado e repartido e o quadro ainda não
                // coube: antes de falhar, a cena 3D usa sombra 2048 e MSAA 2×
                // (o resto do quadro e as amostras de motion blur não mudam).
                const bool exhausted = trimmed && (settings.stageUnblurredScenes || !target.texture.valid()
                    || !Renderer::has_unblurred_scene_targets(snapshot_));
                if (exhausted && renderer_.export_scene_memory_reduced()) return rendered;
                snapshot_.release_video_frames();
                if (gpuLock.owns_lock()) gpuLock.unlock();
                if (const Status ready = wait_gpu(submittedFrame, settings.cancelFlag, pollingMutex, additionalCancel); !ready.ok()) return ready;
                if (pollingMutex) gpuLock.lock();
                if (cancelled()) return Errc::Cancelled;
                if (const Status ready = renderer_.reclaim_failed_transients(); !ready.ok()) return ready;
                if (!reclaimed && rendered.code() == Errc::OutOfDeviceMemory) {
                    reclaimed = true;
                    continue;
                }
                if (exhausted) {
                    (void)renderer_.reduce_export_scene_memory();
                    (void)renderer_.trim_memory(4, ~0ull);
                    AUREA_LOG_WARN("export: quadro %lld sem memoria mesmo em etapas; cena 3D com sombra 2048 e MSAA 2x daqui em diante",
                                   static_cast<long long>(time.value));
                    continue;
                }
                const auto before = gpu_->memory_stats();
                const u32 released = renderer_.trim_memory(4, ~0ull);
                const usize prefetchBytes = media_.reclaim_idle_prefetch_frames();
                // Keep the leaner shutter plan for following frames. The
                // same original samples still contribute to the same image.
                if (settings.boundedSceneExposure) {
                    serialSceneExposure_ = true;
                    settings.sceneExposureBatchBudgetBytes = 1;
                }
                if (trimmed) {
                    settings.stageUnblurredScenes = true;
                    staged = true;
                }
                trimmed = reclaimed = true;
                const auto after = gpu_->memory_stats();
                AUREA_LOG_WARN("export: quadro %lld refeito por pressao de memoria (codigo=%d, etapas_3d=%u, lote_obturador=%llu B, recursos_liberados=%u, prefetch=%llu KB, GPU=%llu -> %llu KB)",
                    static_cast<long long>(time.value), rendered.raw(), staged ? 1u : 0u,
                    static_cast<unsigned long long>(settings.sceneExposureBatchBudgetBytes), released,
                    static_cast<unsigned long long>(prefetchBytes / 1024),
                    static_cast<unsigned long long>(before.usedBytes / 1024),
                    static_cast<unsigned long long>(after.usedBytes / 1024));
                continue;
            }
            if (!rendered.ok()) return rendered;
            // 0.5.2 (E1): o quadro deixou de fora algo que nunca chega (modelo que
            // falhou ao subir): ele vale como está, como a prévia mostra; avisa.
            if (renderer_.take_unavailable()) {
                if (requireCompleteFrames_) {
                    std::snprintf(failure_, sizeof(failure_), "recurso de composicao indisponivel (%s:%u)",
                        site_name(renderer_.unavailable_file()), renderer_.unavailable_line());
                    return {Errc::NotFound, failure_};
                }
                if (!omitted_ && !site_unresolved(renderer_.unavailable_file(), renderer_.unavailable_line()))
                    AUREA_LOG_WARN("export: quadro %lld sem um recurso indisponivel (%s:%u); sai como a previa",
                                   static_cast<long long>(time.value), site_name(renderer_.unavailable_file()), renderer_.unavailable_line());
                remember_unresolved(renderer_.unavailable_file(), renderer_.unavailable_line());
                omitted_ = true;
            }
            // 0.4.6: o aproximado cobre só a FONTE de vídeo atrasada. Se o
            // quadro também ficou incompleto por algo que ainda vai chegar
            // (modelo/textura subindo, ambiente HDRI), ele
            // NÃO vai para o arquivo: espera e tenta de novo, como o exato.
            // Antes o aviso era jogado fora e o MP4 saía sem essas partes.
            const bool incomplete = approximate && renderer_.take_incomplete();
            const bool resolvable = incomplete && (renderer_.pending_uploads() != 0 || renderer_.environment_pending());
            if (approximate && !resolvable) {
                if (incomplete) AUREA_LOG_WARN("export: quadro %lld aproximado ainda incompleto (%s:%u)",
                                               static_cast<long long>(time.value), renderer_.incomplete_file() ? renderer_.incomplete_file() : "?", renderer_.incomplete_line());
                approximate_ = true;
                ++approximateFrames_;
                ++approximateRun_;
                AUREA_LOG_WARN("export: quadro %lld sem o quadro exato da fonte em %llu ms; usado o mais proximo do trecho (%u na sessao)",
                               static_cast<long long>(time.value), static_cast<unsigned long long>((checkNs - start) / 1'000'000ull),
                               approximateFrames_);
                return OkStatus;
            }
            if (!approximate) {
                if (!renderer_.take_incomplete()) {
                    if (staged && timings.sceneExposureBatches) stageUnblurredScenes_ = true;
                    approximateRun_ = 0;
                    return OkStatus;
                }
                waitFile = renderer_.incomplete_file();
                waitLine = renderer_.incomplete_line();
                siteFile_ = waitFile; siteLine_ = waitLine;
                // Ponto já declarado sem solução nesta sessão (abaixo): sem nada a
                // caminho, o quadro sai depois da espera curta, como o primeiro.
                if (site_unresolved(waitFile, waitLine) && !in_flight() && monotonic_ns() - start >= kUnresolvedSiteWaitNs) {
                    omitted_ = true;
                    approximateRun_ = 0;
                    return OkStatus;
                }
            }
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
        const bool exceededSourceWait = now - lastProgress >= (approximateFrames_ ? deadlines_.reopenAfterNs : deadlines_.reopenNs)
            || (requireCompleteFrames_ && !softwareDecode_ && now - start >= deadlines_.reopenNs);
        if (!exact && !reopened && exceededSourceWait) {
            u32 closed = 0;
            bool neverDelivered = false;
            for (const FrameSnapshot::VideoSourceWait& w : snapshot_.videoSourceWaits) {
                if (w.opening || (!w.missing && !w.stale)) continue;
                neverDelivered |= w.missing;
                media_.close_layer(w.layer);
                ++closed;
            }
            // 0.4.6: a fonte não tem NENHUM quadro deste trecho (o clipe que
            // entra na transição, no Galaxy A23): não há "mais próximo" para
            // aproximar, e reabrir no mesmo decoder do aparelho dava no mesmo —
            // o quadro falhava, a escada recomeçava do zero e todo degrau parava
            // no mesmo ponto. Daqui em diante esta sessão abre a mídia no
            // decoder compatível, e a fonte reaberta já sai por ele.
            if (closed && (neverDelivered || requireCompleteFrames_) && !softwareDecode_) {
                set_software_decode(true);
                AUREA_LOG_WARN("export: quadro %lld sem amostra exata da fonte; decodificacao compativel daqui em diante",
                               static_cast<long long>(time.value));
            }
            if (closed) {
                reopened = true;
                AUREA_LOG_WARN("export: quadro %lld sem o quadro exato ha %llu ms; %u fonte(s) de video reaberta(s)",
                               static_cast<long long>(time.value),
                               static_cast<unsigned long long>((now - lastProgress) / 1'000'000ull), closed);
                lastProgress = now;
                idleSinceProgress = 0;
            }
        }
        if (now - start > fenceTimeoutNs_ || idleSinceProgress > 30'000'000'000ull) {
            // 0.5.2 (E1): o quadro exato foi renderizado e ficou incompleto o
            // prazo inteiro sem nada a caminho: o ponto não se resolve nesta
            // sessão. O alvo já tem o quadro como a prévia mostra (sem aquilo):
            // ele sai, o ponto entra na lista e o export avisa. Com upload ou
            // ambiente ainda a caminho (transitório), falha como antes.
            // Memory failures were handled above without encoding partial output.
            if (!requireCompleteFrames_ && exact && waitFile && !in_flight()) {
                remember_unresolved(waitFile, waitLine);
                omitted_ = true;
                approximateRun_ = 0;
                AUREA_LOG_WARN("export: quadro %lld incompleto por %llu ms em %s:%u sem nada a caminho; ponto sem solucao, o quadro sai como a previa (%u na sessao)",
                               static_cast<long long>(time.value), static_cast<unsigned long long>((now - start) / 1'000'000ull),
                               site_name(waitFile), waitLine, unresolvedCount_);
                return OkStatus;
            }
            // O ponto vai no detalhe (e dali para o relatório): antes só o
            // caminho aproximado dizia QUAL recurso faltou.
            if (exact) {
                std::snprintf(failure_, sizeof(failure_), "recurso de composicao indisponivel (%s:%u); quadro offline nao foi codificado",
                              site_name(waitFile), waitLine);
                AUREA_LOG_ERROR("export: quadro %lld incompleto em %s:%u (%s); exportacao interrompida",
                                static_cast<long long>(time.value), waitFile ? waitFile : "?", waitLine,
                                in_flight() ? "recurso ainda a caminho" : "sem ponto");
            }
            return Status{exact ? Errc::Timeout : Errc::DecodeFailed,
                exact ? failure_ : "a fonte nao entregou o quadro exato; exportacao interrompida"};
        }
        std::unique_lock lock(wakeMutex_);
        const u64 sleepBegin = monotonic_ns();
        {
            ScopedElapsedTime sleeping{exact ? measurements_.resourceWaitNs : measurements_.mediaWaitNs};
            wake_.wait_for(lock, std::chrono::milliseconds(10), [&] {
                return generation_.load(std::memory_order_acquire) != observed || cancelled();
            });
        }
        idleSinceProgress += monotonic_ns() - sleepBegin;
    }
}
void OfflineFrameRenderer::shutdown() noexcept {
    snapshot_ = FrameSnapshot{};
    media_.close_all();
    renderer_.shutdown();
    gpu_ = nullptr;
}
bool OfflineFrameRenderer::retire_sources(u64 timeoutNs) noexcept {
    snapshot_ = FrameSnapshot{};
    return media_.release_preview_for_export(nullptr, timeoutNs ? timeoutNs : 1).ok();
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
