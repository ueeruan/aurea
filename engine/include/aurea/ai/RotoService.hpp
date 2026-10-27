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
//
//  Refinar borda (RotoRefine): com raio > 0, cada quadro da cadeia ganha
//  também o recorte na resolução do quadro (até 1920 no lado maior), que
//  depende do refinado do quadro anterior (reduzir trepidação). Cache
//  próprio (o refinado é ~10× maior), chave = traços + ajustes do refino.
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
    using RefinedPtr = std::shared_ptr<const RotoRefined>;
    static constexpr u32 kSize = ForegroundEstimator::kSize;
    static constexpr usize kMaxBytes = 48u * 1024u * 1024u;
    /// Classe de memória LOW (até ~4 GB de RAM): metade do teto.
    static constexpr usize kLowMemoryMaxBytes = kMaxBytes / 2;
    /// O cache do refinado é à parte: teto = 4/3 do teto do recorte (64 MB;
    /// 32 MB na classe LOW). 1080p com cabelo: ~80-200 KB por quadro.
    static constexpr usize kRefinedShare = 4;   ///< ×teto/3

    /// De onde vêm os quadros (cópia: o modelo pode mudar durante o cálculo).
    struct Source {
        DepthMapService* fg = nullptr;
        VideoSourceFactory* factory = nullptr;   ///< nulo = imagem
        Asset asset;
        u64 sourceKey = 0;
        f64 fps = 30.0;
        i64 frameCount = 1;
        DepthMapPtr image;                       ///< imagem: o mapa cru pronto
        RotoImagePtr imageFrame;                 ///< imagem com refino: o quadro na resolução do refino
        f32 layerW = 1.0f, layerH = 1.0f;
        RotoRefineSettings refine;               ///< raio em px da FONTE (convertido para o quadro do refino)
        u32 refineMaxSide = kRotoRefineMaxSide;
    };
    struct Result {
        MattePtr matte;      ///< grade kSize²
        RefinedPtr refined;  ///< nulo sem refino
    };
    struct Progress {
        u64 track = 0;
        i64 done = 0;
        i64 total = 0;
        bool running = false;
        bool failed = false;
        /// Bugs 0.3.4: por que falhou (DepthMapService::Failure; 0 = não falhou).
        u32 reason = 0;
    };

    RotoService() = default;
    ~RotoService();
    RotoService(const RotoService&) = delete;
    RotoService& operator=(const RotoService&) = delete;

    /// O recorte pronto e válido para estes traços, sem calcular nada.
    [[nodiscard]] MattePtr cached(u64 track, const RotoStrokes& strokes, i64 frame);
    /// O refinado pronto para estes traços e ajustes (nulo sem refino ou sem cache).
    [[nodiscard]] RefinedPtr cached_refined(u64 track, const RotoStrokes& strokes, const RotoRefineSettings& refine,
                                            i64 frame);
    /// Calcula a cadeia até `frame` aqui mesmo (export). Nulo em falha/prazo.
    [[nodiscard]] MattePtr compute(u64 track, const RotoStrokes& strokes, const Source& source, i64 frame,
                                   u64 deadlineNs = 0);
    /// O mesmo, com o refinado (quando `source.refine` está ligado).
    [[nodiscard]] Result compute_all(u64 track, const RotoStrokes& strokes, const Source& source, i64 frame,
                                     u64 deadlineNs = 0);
    /// Imagem (camada de foto) na resolução do refino, guardada (uma por vez).
    [[nodiscard]] RotoImagePtr image_frame(u64 key, const u8* rgba, u32 width, u32 height, u32 stride, u32 maxSide);
    /// Preview: agenda a cadeia até `frame` no worker (o pedido mais novo vence).
    /// Durante uma propagação da mesma trilha o quadro vira o FOCO: o worker
    /// o calcula antes de seguir o trecho (o quadro pintado aparece já).
    void request(u64 track, const RotoStrokes& strokes, const Source& source, i64 frame);
    /// O recorte de `frame` está pronto para estes traços (sem descompactar).
    /// `refineKey` (RotoRefineSettings::hash) ≠ 0: o refinado também.
    [[nodiscard]] bool ready(u64 track, const RotoStrokes& strokes, i64 frame, u64 refineKey = 0) const;
    /// Quantos quadros de [first, last] já têm recorte válido para estes traços.
    [[nodiscard]] i64 ready_count(u64 track, const RotoStrokes& strokes, i64 first, i64 last, u64 refineKey = 0) const;
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
    [[nodiscard]] static u64 refined_dep(const RotoStrokes& strokes, i64 frame, u64 refineKey) noexcept;
    void store(u64 track, i64 frame, u64 dep, const std::vector<f32>& matte);
    void store_refined(u64 track, i64 frame, u64 dep, const RotoRefined& refined);
    [[nodiscard]] RefinedPtr cached_refined_dep(u64 track, i64 frame, u64 dep);
    [[nodiscard]] DepthMapPtr raw(const Source& source, i64 frame, RotoImagePtr* image = nullptr);
    [[nodiscard]] RotoImagePtr frame_image(const Source& source, i64 frame);
    Result compute_impl(u64 track, const RotoStrokes& strokes, const Source& source, i64 frame, u64 deadlineNs,
                        const std::atomic<u64>* generation, u64 expected);
    void evict_locked(usize keepCoarse, usize keepRefined) noexcept;
    void thread_main() noexcept;
    void ensure_thread();

    mutable std::mutex mutex_;
    std::unordered_map<u64, Entry> cache_;
    std::unordered_map<u64, Entry> refined_;   ///< PackBits do RotoRefined (dep = traços + ajustes)
    usize bytes_ = 0, refinedBytes_ = 0;
    usize maxBytes_ = kMaxBytes;
    u64 stamp_ = 0;
    struct ImageSlot { u64 key = 0; u32 maxSide = 0; RotoImagePtr image; };
    ImageSlot imageSlot_;

    mutable std::mutex queueMutex_;
    std::condition_variable wake_;
    std::unique_ptr<Job> pending_;
    std::unique_ptr<Job> focus_;     ///< quadro pedido pela prévia enquanto uma propagação roda
    bool runningSingle_ = false;     ///< o worker está num pedido de um quadro (interrompível)
    bool running_ = false;
    std::thread thread_;
    std::atomic<u64> generation_{0};
    std::atomic<u64> computed_{0};
    std::atomic<u32> lastFailure_{0};   ///< bugs 0.3.4: motivo da última falha de um quadro
    Progress progress_{};
    void (*readyFn_)(void*) = nullptr;
    void* readyCtx_ = nullptr;
};

} // namespace aurea::ai
