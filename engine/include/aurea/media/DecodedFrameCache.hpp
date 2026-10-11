// =============================================================================
//  Aurea / media / DecodedFrameCache.hpp
//
//  Frames decodificados de UMA fonte, com orçamento.
//
//  Não é cache do vídeo inteiro — é uma janela em volta do playhead. A política
//  de despejo é TEMPORAL, feita para timeline, e não LRU puro:
//
//    custo(frame) = distância ao playhead × (3 se o frame está "para trás" na
//                   direção do movimento, 1 se está à frente)
//
//  Tocando para a frente, os frames já passados saem primeiro; o que está logo
//  adiante fica. Arrastando para trás, inverte. Parado, a janela é simétrica. É
//  o que faz o scrub para trás cair no cache sem que o playback encha a memória.
//
//  No caminho zero-copy, cada frame guardado segura um buffer do decoder
//  (ImageReader). Por isso o limite em QUANTIDADE existe além do de bytes: se o
//  cache segurasse todos os buffers, o decoder travaria sem onde escrever.
//
//  Fase 8 (§12, §14–16): o cache se liga ao MemoryManager (`attach`). Cada
//  byte guardado entra na categoria DecodedFrames, e o orçamento dela é
//  COMPARTILHADO entre todas as fontes abertas: quem insere acima do teto
//  despeja os próprios piores frames. Sob pressão do sistema (trim), fica só
//  o frame do playhead. Métricas e versão (muda a cada `clear`) publicadas.
// =============================================================================
#pragma once

#include "aurea/media/VideoTypes.hpp"
#include "aurea/memory/MemoryManager.hpp"

#include <mutex>
#include <array>
#include <vector>

namespace aurea {

class DecodedFrameCache final : public IMemoryReclaimable {
public:
    struct Config {
        u32 maxFrames = 6;
        u64 maxBytes = 192ull * 1024 * 1024;
        /// Decoder-safe ceiling for explicitly required temporal samples.
        /// Zero keeps maxFrames as a hard limit; ordinary prefetch never grows.
        u32 requiredFrameLimit = 0;
    };

    struct Stats {
        u32 frames = 0;
        u64 bytes = 0;
        u64 hits = 0;
        u64 misses = 0;
        u64 evictions = 0;
        u32 version = 0;
    };

    DecodedFrameCache() = default;
    ~DecodedFrameCache() override;
    DecodedFrameCache(const DecodedFrameCache&) = delete;
    DecodedFrameCache& operator=(const DecodedFrameCache&) = delete;

    void configure(const Config& c) noexcept;
    [[nodiscard]] Config config() const noexcept;
    /// Number of future frames that can coexist with the current display frame.
    [[nodiscard]] u32 prefetch_capacity() const noexcept;

    /// Liga ao orçamento do motor (categoria DecodedFrames). Idempotente;
    /// nulo desliga. Os bytes já guardados passam a contar na hora.
    void attach(MemoryManager* memory) noexcept;

    /// Onde está o playhead e para onde ele vai. Orienta o despejo.
    void set_focus(i64 playheadUs, i32 direction) noexcept;
    /// Bounded working set for one render: base, blend neighbor and RGB samples.
    /// Byte budgets evict optional prefetch first; decoder buffer limits remain hard.
    void set_required_times(const i64* times, u32 count, i64 toleranceUs) noexcept;

    /// Guarda o frame. Devolve false quando ele próprio seria o primeiro a sair
    /// (longe demais do playhead para valer um lugar).
    bool insert(FrameRef frame) noexcept;

    /// Melhor frame para `targetUs`: o exato (|pts - alvo| ≤ meio frame) ou, se
    /// não houver, o mais próximo ANTERIOR (o que um player mostraria), e na
    /// falta dele o mais próximo de qualquer lado. `exact` diz qual foi.
    /// During reverse playback, missing targets may only fall back to later
    /// frames. Earlier preroll is decoded forward and must not be displayed.
    [[nodiscard]] FrameRef find(i64 targetUs, i64 halfFrameUs, bool* exact, bool reverseFallback = false) noexcept;

    [[nodiscard]] bool contains(i64 targetUs, i64 halfFrameUs) const noexcept;

    /// Maior pts guardado que ainda está à frente de `fromUs` de forma
    /// contígua (sem buraco maior que um frame). O decoder usa para saber até
    /// onde já pré-carregou.
    [[nodiscard]] i64 contiguous_end(i64 fromUs, i64 frameUs) const noexcept;
    /// O espelho para trás (reverso): menor pts guardado contíguo a partir de
    /// `fromUs` descendo. `fromUs + frameUs` quando nem `fromUs` está lá.
    [[nodiscard]] i64 contiguous_begin(i64 fromUs, i64 frameUs) const noexcept;

    void clear() noexcept;
    [[nodiscard]] Stats stats() const noexcept;

    /// Retire optional CPU prefetch only when this cache owns its last reference.
    /// Required temporal samples, external leases and one display entry stay.
    /// Returns bytes after backing-frame destruction, before callers admit GPU
    /// resources; it never creates credit for storage still held by a snapshot.
    /// Providers must explicitly own CPU backing; native and reader-pool views
    /// remain because their storage can outlive the DecodedFrame owner.
    [[nodiscard]] usize reclaim_unused() noexcept;

    struct ExclusivePruneResult {
        usize freed = 0;
        u32 versionBefore = 0;
        u32 versionAfter = 0;
        bool retainedTarget = false;
    };
    /// Atomic cache transition for a settled source's pressure pruning. The
    /// current presentation interval must exist and remains protected even
    /// when no snapshot lease or required-times registration is present.
    [[nodiscard]] ExclusivePruneResult reclaim_unused_for(i64 targetUs, i64 halfFrameUs) noexcept;

    // IMemoryReclaimable
    [[nodiscard]] MemoryClass memory_class() const noexcept override { return MemoryClass::DecodedFrames; }
    [[nodiscard]] usize reclaim(usize targetBytes) noexcept override;
    [[nodiscard]] const char* debug_name() const noexcept override { return "quadros-decodificados"; }
    [[nodiscard]] bool accounts_itself() const noexcept override { return true; }
    [[nodiscard]] bool metrics(CacheMetrics& out) const noexcept override;

private:
    void evict_locked() noexcept;
    [[nodiscard]] bool contains_locked(i64 targetUs, i64 halfFrameUs) const noexcept;
    [[nodiscard]] ExclusivePruneResult reclaim_unused_locked(bool preserveTarget, i64 targetUs, i64 halfFrameUs) noexcept;
    /// Tira o frame `i` e desconta os bytes (cache e orçamento).
    void erase_locked(usize i) noexcept;
    [[nodiscard]] usize worst_locked() const noexcept;
    [[nodiscard]] f64 cost_locked(i64 ptsUs, i64 durationUs = 0) const noexcept;
    [[nodiscard]] bool over_shared_budget_locked() const noexcept;
    [[nodiscard]] bool required_locked(i64 pts, i64 duration) const noexcept;
    [[nodiscard]] u32 frame_limit_locked() const noexcept;

    mutable std::mutex mutex_;
    std::vector<FrameRef> frames_;   ///< ordenado por pts
    Config config_{};
    Stats  stats_{};
    i64    focusUs_ = 0;
    i32    direction_ = 0;
    std::array<i64, 5> requiredTimes_{};
    u32 requiredCount_ = 0;
    i64 requiredTolerance_ = 0;
    MemoryManager* memory_ = nullptr;
};

} // namespace aurea
