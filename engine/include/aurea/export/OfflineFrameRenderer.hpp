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
                      u64 transientBudget, u64 fenceTimeoutNs) noexcept;
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
    u32 approximateFrames_ = 0;
    u32 approximateRun_ = 0;   ///< aproximados SEGUIDOS (zera no próximo exato)
};
} // namespace aurea
