#pragma once
#include "aurea/render/Renderer.hpp"
#include <condition_variable>

namespace aurea {
// A session owns every temporal render history and source decoder. Only the
// immutable composition and the existing compositor implementation are shared.
class OfflineFrameRenderer final {
public:
    ~OfflineFrameRenderer() { shutdown(); }
    Status initialize(GPUBackend& gpu, const EffectRegistry& effects,
                      VideoSourceFactory* sources, MemoryManager* memory,
                      u64 transientBudget, u64 fenceTimeoutNs,
                      const std::atomic<bool>* cancel = nullptr) noexcept;
    Renderer& compositor() noexcept { return renderer_; }
    Status render(const Project& project, CompositionId composition,
                  FrameIndex time, const OffscreenTarget& target,
                  const ImagePixels* (*images)(void*, AssetId), void* imageContext,
                  const RenderSettings& settings, u64& submittedFrame,
                  std::mutex* pollingMutex = nullptr,
                  const std::atomic<bool>* additionalCancel = nullptr) noexcept;
    /// O último render() saiu com algum quadro de vídeo aproximado (o mais
    /// próximo do MESMO trecho, depois de reabrir a fonte): o motor marca
    /// kExportFrameFallback e as telas avisam. Lê e zera.
    [[nodiscard]] bool take_approximate() noexcept { const bool a = approximate_; approximate_ = false; return a; }
    /// Quadros aproximados nesta sessão (diagnóstico).
    [[nodiscard]] u32 approximate_frames() const noexcept { return approximateFrames_; }
    /// 0.5.2 (E1): algum quadro desde a última leitura saiu sem um recurso que
    /// não chega (Renderer::take_unavailable, ou um ponto declarado sem
    /// solução abaixo): o motor marca kExportElementsOmitted e as telas avisam.
    [[nodiscard]] bool take_omitted() noexcept { const bool o = omitted_; omitted_ = false; return o; }
    /// Pontos (arquivo:linha) declarados sem solução nesta sessão.
    [[nodiscard]] u32 unresolved_sites() const noexcept { return unresolvedCount_; }
    /// O último ponto que deixou um quadro incompleto (diagnóstico do export).
    [[nodiscard]] const char* last_incomplete_file() const noexcept { return siteFile_; }
    [[nodiscard]] u32 last_incomplete_line() const noexcept { return siteLine_; }
    /// 0.5.2 (E1): um quadro EXATO que ficou incompleto pelo prazo inteiro, sem
    /// nada a caminho (upload pendente, ambiente carregando), tem o ponto que o
    /// segurou declarado sem solução: ele sai como a prévia mostra (sem aquilo)
    /// e o export segue com aviso, em vez de falhar no mesmo quadro em todo
    /// degrau. Os quadros seguintes presos SÓ por esse ponto saem depois desta
    /// espera curta — o custo do prazo é pago uma vez por ponto, não por quadro.
    static constexpr u64 kUnresolvedSiteWaitNs = 150'000'000ull;
    static constexpr u32 kMaxUnresolvedSites = 8;
    /// Prazos do quadro exato (0.3.0). Sem avanço do decoder por `reopenNs`, a
    /// fonte que espera é reaberta (uma vez por quadro); sem o exato depois de
    /// `approximateNs` sem avanço (ou `approximateTotalNs` no total), sai o mais
    /// próximo do mesmo trecho. Depois do 1o aproximado, os prazos `…After…`.
    /// Aproximados seguidos que ainda valem. Um decoder que morreu de vez
    /// (a fonte não anda mais) não pode virar imagem congelada no arquivo:
    /// passou disto, o export falha como antes (DecodeFailed).
    static constexpr u32 kMaxApproximateRun = 3;
    struct ExactFrameDeadlines {
        u64 reopenNs = 8'000'000'000ull, approximateNs = 16'000'000'000ull, approximateTotalNs = 45'000'000'000ull;
        u64 reopenAfterNs = 3'000'000'000ull, approximateAfterNs = 6'000'000'000ull, approximateTotalAfterNs = 15'000'000'000ull;
    };
    void set_exact_frame_deadlines(const ExactFrameDeadlines& d) noexcept { deadlines_ = d; }
    /// 0.4.6: decodificação compatível (MediaPriority::ExportSoftwareDecode)
    /// para os decoders abertos daqui em diante — o degrau da escada que pede
    /// (ExportSettings::softwareDecode), ou o próprio render ao reabrir uma
    /// fonte que o decoder do aparelho nunca entregou.
    void set_software_decode(bool enabled) noexcept {
        softwareDecode_ = enabled;
        media_.set_open_priority(enabled ? MediaPriority::ExportSoftwareDecode : MediaPriority::Preview);
    }
    /// Os decoders desta sessão abrem no modo compatível (pedido ou adotado).
    [[nodiscard]] bool software_decode() const noexcept { return softwareDecode_; }
    void shutdown() noexcept;
    // Engine proved all normal/auxiliary GPU work; no renderer command may be
    // submitted after this. Source callbacks remain CPU-only until retired.
    void retire_gpu_after_done() noexcept;
    // Transfer opens/decoders to their owner retirement worker and poll only.
    // The caller must retain this renderer/factory until this returns true.
    [[nodiscard]] bool retire_sources() noexcept;
    void detach_device() noexcept; // Invalidates handles without joining a platform opener.
    void forget_device() noexcept;
private:
    Status wait_gpu(u64 frame, const std::atomic<bool>* cancel, std::mutex* pollingMutex = nullptr,
                    const std::atomic<bool>* additionalCancel = nullptr) noexcept;
    static void source_ready(void* context) noexcept;
    GPUBackend* gpu_ = nullptr;
    Renderer renderer_;
    MediaManager media_;
    FrameSnapshot snapshot_;
    u64 sequence_ = 0, fenceTimeoutNs_ = 120'000'000'000ull;
    std::atomic<u64> generation_{0};
    std::mutex wakeMutex_;
    std::condition_variable wake_;
    ExactFrameDeadlines deadlines_{};
    bool approximate_ = false;
    bool omitted_ = false;          ///< take_omitted
    struct Site { const char* file = nullptr; u32 line = 0; };
    Site unresolved_[kMaxUnresolvedSites]{};
    u32 unresolvedCount_ = 0;
    const char* siteFile_ = nullptr;
    u32 siteLine_ = 0;
    char failure_[192]{};           ///< o detalhe da falha (o Status guarda o ponteiro)
    [[nodiscard]] bool site_unresolved(const char* file, u32 line) const noexcept;
    void remember_unresolved(const char* file, u32 line) noexcept;
    bool softwareDecode_ = false;
    u32 approximateFrames_ = 0;
    u32 approximateRun_ = 0;   ///< aproximados SEGUIDOS (zera no próximo exato)
};
} // namespace aurea
