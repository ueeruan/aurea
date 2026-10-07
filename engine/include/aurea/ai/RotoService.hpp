#pragma once

// =============================================================================
//  Aurea / ai / RotoService.hpp
//
//  Os recortes do Roto Brush por quadro da fonte, por trilha (camada + efeito).
//  Cada recorte guarda a DEPENDÊNCIA dos traços que o decidem
//  (`roto_dependency`): traço novo num quadro refaz só o que depende dele
//  ("atualizar outros quadros"). Depois de propagado, preview e export leem o
//  cache sem recalcular ("congelar"). Cache em RLE, limitado em bytes.
//
//  O cálculo pesado (rede + fluxo + geodésica) roda no worker próprio
//  ("aurea-roto"), que usa a rede do Rotobrush pelo `DepthMapService` (o mesmo
//  lock de trabalho do worker de IA). O export calcula a cadeia na própria
//  thread, com prazo.
// =============================================================================

#include "aurea/ai/DepthMapService.hpp"
#include "aurea/ai/RotoMatte.hpp"

#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>

namespace aurea::ai {

class RotoService {
public:
    using MattePtr = std::shared_ptr<const std::vector<f32>>;
    static constexpr u32 kSize = ForegroundEstimator::kSize;
    static constexpr usize kMaxBytes = 48u * 1024u * 1024u;
    /// Classe de memória LOW (até ~4 GB de RAM): metade do teto.
    static constexpr usize kLowMemoryMaxBytes = kMaxBytes / 2;

    /// De onde vêm os quadros (cópia: o modelo pode mudar durante o cálculo).
    struct Source {
        DepthMapService* fg = nullptr;
        VideoSourceFactory* factory = nullptr;   ///< nulo = imagem
        Asset asset;
        u64 sourceKey = 0;
        f64 fps = 30.0;
        i64 frameCount = 1;
        DepthMapPtr image;                       ///< imagem: o mapa cru pronto
        f32 layerW = 1.0f, layerH = 1.0f;
    };
    struct Progress {
        u64 track = 0;
        i64 done = 0;
        i64 total = 0;
        bool running = false;
        bool failed = false;
    };

    RotoService() = default;
    ~RotoService();
    RotoService(const RotoService&) = delete;
    RotoService& operator=(const RotoService&) = delete;

    /// O recorte pronto e válido para estes traços, sem calcular nada.
    [[nodiscard]] MattePtr cached(u64 track, const RotoStrokes& strokes, i64 frame);
    /// Calcula a cadeia até `frame` aqui mesmo (export). Nulo em falha/prazo.
    [[nodiscard]] MattePtr compute(u64 track, const RotoStrokes& strokes, const Source& source, i64 frame,
                                   u64 deadlineNs = 0);
    /// Preview: agenda a cadeia até `frame` no worker (o pedido mais novo vence).
    void request(u64 track, const RotoStrokes& strokes, const Source& source, i64 frame);
    /// "Propagar clipe": todos os quadros [first, last] no worker, com progresso.
    void propagate(u64 track, const RotoStrokes& strokes, const Source& source, i64 first, i64 last);
    [[nodiscard]] Progress progress() const;
    /// Para a propagação em curso (o que já foi calculado fica).
    void cancel() noexcept;
    void set_ready_callback(void (*fn)(void*), void* ctx) noexcept;
    /// Projeto fechado: worker, fila e cache fora.
    void clear() noexcept;
    [[nodiscard]] usize cached_bytes() const;
    /// Teto do cache em bytes (kMaxBytes; kLowMemoryMaxBytes na classe LOW).
    /// Encolher despeja já os recortes usados há mais tempo.
    void set_max_bytes(usize bytes) noexcept;
    /// Pressão de memória: despeja os recortes usados há mais tempo até sobrar
    /// no máximo `keepBytes`. O worker segue; devolve os bytes soltos.
    usize trim_to(usize keepBytes) noexcept;
    [[nodiscard]] u64 computed_frames() const noexcept { return computed_.load(std::memory_order_relaxed); }

private:
    struct Entry { u64 dep = 0; std::vector<u8> rle; u64 stamp = 0; };
    struct Job {
        u64 track = 0;
        RotoStrokes strokes;
        Source source;
        i64 first = 0, last = 0;
        bool range = false;
    };
    [[nodiscard]] static u64 key_of(u64 track, i64 frame) noexcept;
    void store(u64 track, i64 frame, u64 dep, const std::vector<f32>& matte);
    [[nodiscard]] DepthMapPtr raw(const Source& source, i64 frame);
    MattePtr compute_impl(u64 track, const RotoStrokes& strokes, const Source& source, i64 frame, u64 deadlineNs,
                          const std::atomic<u64>* generation, u64 expected);
    void thread_main() noexcept;
    void ensure_thread();

    mutable std::mutex mutex_;
    std::unordered_map<u64, Entry> cache_;
    usize bytes_ = 0;
    usize maxBytes_ = kMaxBytes;
    u64 stamp_ = 0;

    mutable std::mutex queueMutex_;
    std::condition_variable wake_;
    std::unique_ptr<Job> pending_;
    bool running_ = false;
    std::thread thread_;
    std::atomic<u64> generation_{0};
    std::atomic<u64> computed_{0};
    Progress progress_{};
    void (*readyFn_)(void*) = nullptr;
    void* readyCtx_ = nullptr;
};

} // namespace aurea::ai
