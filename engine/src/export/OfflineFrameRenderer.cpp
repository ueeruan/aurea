#include "aurea/export/OfflineFrameRenderer.hpp"
#include "aurea/project/Project.hpp"
#include "aurea/core/Log.hpp"
#include "aurea/core/Time.hpp"
#include <chrono>
#include <cstdio>
#include <cstring>

namespace aurea {
namespace {
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
    fenceTimeoutNs_ = timeout ? timeout : 120'000'000'000ull;
    media_.set_factory(sources);
    media_.set_memory(memory);
    media_.set_ready_callback(&OfflineFrameRenderer::source_ready, this);
    renderer_.set_transient_cache_budget(budget);
    renderer_.set_preview_cache_budget(0);
    renderer_.set_flow_cache_budget(16ull << 20);
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
    bool reclaimed = false, trimmed = false, reopened = false;
    // 0.5.2 (E1): o ponto que deixou ESTE quadro incompleto (exato), e se há
    // algo a caminho que ainda pode resolvê-lo.
    const char* waitFile = nullptr;
    u32 waitLine = 0;
    bool waitMemory = false;   ///< algum render deste quadro faltou memória (não entra na rede)
    const auto in_flight = [&]() noexcept { return renderer_.pending_uploads() != 0 || renderer_.environment_pending(); };
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
            if (rendered.code() == Errc::OutOfDeviceMemory && !trimmed) {
                // 0.5.2 (E1b): soltar só os transitórios do grafo que falhou não
                // bastou ("textura transitoria indisponivel" no mesmo quadro em
                // todo degrau, Galaxy A23/M21, vivo V2120). Antes de desistir do
                // quadro, solta TUDO o que se refaz: o pool inteiro (entradas
                // ociosas de quadros anteriores), caches de imagem, planos,
                // fluxo, máscara, LUT e profundidade fora do quadro, e espera a
                // GPU aposentar o que saiu (a destruição é adiada para o fence).
                // Uma vez por quadro; falhando de novo, o quadro falha como antes.
                snapshot_.release_video_frames();
                if (gpuLock.owns_lock()) gpuLock.unlock();
                if (const Status ready = wait_gpu(gpu_->last_submitted_frame(), settings.cancelFlag, pollingMutex, additionalCancel); !ready.ok()) return ready;
                if (pollingMutex) gpuLock.lock();
                (void)renderer_.reclaim_failed_transients(100'000'000ull);
                const auto before = gpu_->memory_stats();
                const u32 released = renderer_.trim_memory(4, ~0ull);
                if (gpuLock.owns_lock()) gpuLock.unlock();
                if (const Status ready = wait_gpu(gpu_->last_submitted_frame(), settings.cancelFlag, pollingMutex, additionalCancel); !ready.ok()) return ready;
                if (pollingMutex) gpuLock.lock();
                const auto after = gpu_->memory_stats();
                AUREA_LOG_WARN("export: quadro %lld sem memoria de GPU depois de soltar os transitorios; %u recursos refeitos sob demanda saem (GPU %llu -> %llu KB) e o quadro tenta de novo",
                               static_cast<long long>(time.value), released,
                               static_cast<unsigned long long>(before.usedBytes / 1024), static_cast<unsigned long long>(after.usedBytes / 1024));
                trimmed = true;
                continue;
            }
            if (!rendered.ok()) return rendered;
            // 0.5.2 (E1): o quadro deixou de fora algo que nunca chega (modelo que
            // falhou ao subir): ele vale como está, como a prévia mostra; avisa.
            if (renderer_.take_unavailable()) {
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
            const bool memoryShort = renderer_.take_memory_short();
            if (!approximate) {
                if (!renderer_.take_incomplete()) { approximateRun_ = 0; return OkStatus; }
                waitMemory |= memoryShort;
                waitFile = renderer_.incomplete_file();
                waitLine = renderer_.incomplete_line();
                siteFile_ = waitFile; siteLine_ = waitLine;
                // Ponto já declarado sem solução nesta sessão (abaixo): sem nada a
                // caminho, o quadro sai depois da espera curta, como o primeiro.
                if (!waitMemory && site_unresolved(waitFile, waitLine) && !in_flight() && monotonic_ns() - start >= kUnresolvedSiteWaitNs) {
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
        if (!exact && !reopened
            && now - lastProgress >= (approximateFrames_ ? deadlines_.reopenAfterNs : deadlines_.reopenNs)) {
            reopened = true;
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
            if (closed && neverDelivered && !softwareDecode_) {
                set_software_decode(true);
                AUREA_LOG_WARN("export: quadro %lld sem nenhum quadro da fonte; decodificacao compativel daqui em diante",
                               static_cast<long long>(time.value));
            }
            if (closed) {
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
            // Falta de memória não entra: a escada (menos pixels) é quem resolve.
            if (exact && waitFile && !waitMemory && !in_flight()) {
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
                                in_flight() ? "recurso ainda a caminho" : waitMemory ? "sem memoria" : "sem ponto");
            }
            return Status{exact ? Errc::Timeout : Errc::DecodeFailed,
                exact ? failure_ : "a fonte nao entregou o quadro exato; exportacao interrompida"};
        }
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
