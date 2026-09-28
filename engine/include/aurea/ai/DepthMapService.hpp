#pragma once

// =============================================================================
//  Aurea / ai / DepthMapService.hpp
//
//  Os mapas de profundidade dos QUADROS-FONTE, calculados uma vez. O preview e
//  o export pedem pela mesma chave (asset + quadro da fonte): o mesmo quadro
//  nunca roda a rede duas vezes enquanto está no cache.
//
//   - Imagem: síncrono. A rede roda na primeira vez (dezenas de ms) e o mapa
//     fica no cache — a foto não muda.
//   - Vídeo no preview: nunca bloqueia. O pedido vai para o worker (um slot:
//     o mais recente substitui o anterior, como o seek do decoder) e o render
//     é acordado quando o mapa fica pronto.
//   - Vídeo no export: bloqueia até o mapa DESTE quadro existir.
//
//  O vídeo é lido por um decoder PRÓPRIO em modo CPU (o do preview entrega
//  buffers de GPU, que a rede não lê), como as miniaturas.
// =============================================================================

#include "aurea/ai/DepthEstimator.hpp"
#include "aurea/project/Asset.hpp"

#include <atomic>
#include <condition_variable>
#include <list>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <vector>

namespace aurea {
class VideoSourceFactory;
class VideoDecoderBackend;
}

namespace aurea::ai {

/// A disparidade de UM quadro-fonte, 256×256, crua (maior = mais perto).
struct DepthMap {
    std::vector<f32> disparity;
    f32 p2 = 0.0f;       ///< percentil 2 do próprio quadro
    f32 p98 = 1.0f;      ///< percentil 98 do próprio quadro
    f32 inferenceMs = 0.0f;
};
using DepthMapPtr = std::shared_ptr<const DepthMap>;

class DepthMapService {
public:
    struct Stats {
        u64 inferences = 0;   ///< vezes que a rede rodou
        u64 hits = 0;         ///< pedidos atendidos pelo cache
        u64 failures = 0;
        f32 lastInferenceMs = 0.0f;
        DepthEstimator::Backend backend = DepthEstimator::Backend::Cpu;
        bool modelLoaded = false;
    };

    DepthMapService();
    ~DepthMapService();
    DepthMapService(const DepthMapService&) = delete;
    DepthMapService& operator=(const DepthMapService&) = delete;

    /// Imagem RGB8/RGBA8 na CPU. Síncrono; nulo se a rede não carregou.
    [[nodiscard]] DepthMapPtr image(u64 key, const u8* pixels, u32 width, u32 height, u32 stride, u32 channels);

    /// Quadro de vídeo no instante `targetUs` DA MÍDIA (já na grade da fonte;
    /// `frameUs` = duração de um quadro). `wait` (export) decodifica e infere
    /// aqui mesmo; sem ele, agenda no worker e devolve o que o cache tiver.
    [[nodiscard]] DepthMapPtr video(u64 key, VideoSourceFactory* factory, const Asset& asset, u64 sourceKey,
                                    i64 targetUs, i64 frameUs, bool wait);

    /// O que já está no cache, sem pedir nada.
    [[nodiscard]] DepthMapPtr cached(u64 key);

    /// Chamado (na thread do worker) quando um mapa agendado fica pronto.
    void set_ready_callback(void (*fn)(void*), void* ctx) noexcept;

    /// Projeto fechado: cache, fila, decoder e rede fora.
    void clear() noexcept;
    /// Pressão de memória: a rede (~66 MB em fp32) e o decoder saem; o cache fica.
    void trim() noexcept;

    [[nodiscard]] Stats stats() const;

    static constexpr usize kMaxCached = 48;          ///< ~12 MB de mapas
    static constexpr u32 kIdleReleaseMs = 30'000;    ///< rede ociosa sai da memória

private:
    struct Job {
        u64 key = 0;
        Asset asset;          ///< cópia: o modelo pode mudar enquanto decodifica
        u64 sourceKey = 0;
        i64 targetUs = 0;
        i64 frameUs = 0;
        VideoSourceFactory* factory = nullptr;
    };

    void thread_main() noexcept;
    /// Com `work_` travado: decodifica, infere e guarda. Nulo em falha.
    DepthMapPtr run_video_locked(const Job& job);
    DepthMapPtr run_pixels_locked(u64 key, const u8* pixels, u32 width, u32 height, u32 stride, u32 channels);
    [[nodiscard]] bool ensure_model_locked();
    void insert(u64 key, DepthMapPtr map);
    [[nodiscard]] DepthMapPtr find(u64 key);

    // Cache (mutex_): LRU por chave.
    mutable std::mutex mutex_;
    std::unordered_map<u64, std::list<std::pair<u64, DepthMapPtr>>::iterator> index_;
    std::list<std::pair<u64, DepthMapPtr>> lru_;
    Stats stats_{};

    // Trabalho (work_): a rede e o decoder só são usados sob este lock.
    std::mutex work_;
    DepthEstimator estimator_;
    bool modelFailed_ = false;
    std::unique_ptr<VideoDecoderBackend> decoder_;
    u64 decoderAsset_ = 0;
    VideoSourceFactory* decoderFactory_ = nullptr;
    i64 decoderPts_ = -1;       ///< último quadro que o decoder entregou/pulou

    // Worker (queueMutex_): um slot — o pedido mais recente substitui o anterior.
    std::mutex queueMutex_;
    std::condition_variable wake_;
    Job pending_;
    bool hasPending_ = false;
    bool running_ = false;
    std::thread thread_;
    std::atomic<bool> cancel_{false};
    void (*readyFn_)(void*) = nullptr;
    void* readyCtx_ = nullptr;
};

} // namespace aurea::ai
