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
                  const RenderSettings& settings, u64& submittedFrame) noexcept;
    void shutdown() noexcept;
    void forget_device() noexcept;
    /// Fim da sessão sem espera infinita: fecha os decoders esperando no
    /// máximo `timeoutNs` e só então solta o renderer (`deviceLost`: larga os
    /// recursos de GPU sem tocar no aparelho; fica marcado para as próximas
    /// chamadas). false = um decoder ficou preso dentro da plataforma — nada
    /// além dos decoders foi solto e a sessão tem de continuar viva (ela é o
    /// destino do aviso "fonte pronta"); chame de novo depois (0 = só confere).
    [[nodiscard]] bool release(bool deviceLost, u64 timeoutNs) noexcept;
    /// Um abridor de vídeo da plataforma ainda preso (fechar esperaria por ele).
    [[nodiscard]] bool opening_in_flight() noexcept { return media_.opening_in_flight(); }
    /// Decodificação compatível (MediaPriority::ExportSoftwareDecode) para os
    /// decoders abertos daqui em diante.
    void set_software_decode(bool enabled) noexcept {
        media_.set_open_priority(enabled ? MediaPriority::ExportSoftwareDecode : MediaPriority::Preview);
    }
    /// Paciência SEM nenhum trabalho do decoder nem recurso novo antes de dar a
    /// fonte como presa (export_watch_deadline_ns: preparação/regime). 0 = 30 s.
    void set_stall_patience_ns(u64 ns) noexcept { stallPatienceNs_ = ns; }
    /// O último `render` desistiu porque a fonte não entregou o quadro exato
    /// (decoder da mídia preso), não por recurso de GPU/composição.
    [[nodiscard]] bool source_stalled() const noexcept { return sourceStalled_; }
private:
    u64 stallPatienceNs_ = 0;
    bool sourceStalled_ = false;
    bool lostDevice_ = false;
    Status wait_gpu(u64 frame, const std::atomic<bool>* cancel) noexcept;
    static void source_ready(void* context) noexcept;
    GPUBackend* gpu_ = nullptr;
    Renderer renderer_;
    MediaManager media_;
    FrameSnapshot snapshot_;
    u64 sequence_ = 0, fenceTimeoutNs_ = 120'000'000'000ull;
    std::atomic<u64> generation_{0};
    std::mutex wakeMutex_;
    std::condition_variable wake_;
};
} // namespace aurea
