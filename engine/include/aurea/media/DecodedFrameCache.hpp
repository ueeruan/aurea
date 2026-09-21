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
// =============================================================================
#pragma once

#include "aurea/media/VideoTypes.hpp"

#include <mutex>
#include <vector>

namespace aurea {

class DecodedFrameCache {
public:
    struct Config {
        u32 maxFrames = 6;
        u64 maxBytes = 192ull * 1024 * 1024;
    };

    struct Stats {
        u32 frames = 0;
        u64 bytes = 0;
        u64 hits = 0;
        u64 misses = 0;
        u64 evictions = 0;
    };

    void configure(const Config& c) noexcept;
    [[nodiscard]] Config config() const noexcept;

    /// Onde está o playhead e para onde ele vai. Orienta o despejo.
    void set_focus(i64 playheadUs, i32 direction) noexcept;

    /// Guarda o frame. Devolve false quando ele próprio seria o primeiro a sair
    /// (longe demais do playhead para valer um lugar).
    bool insert(FrameRef frame) noexcept;

    /// Melhor frame para `targetUs`: o exato (|pts - alvo| ≤ meio frame) ou, se
    /// não houver, o mais próximo ANTERIOR (o que um player mostraria), e na
    /// falta dele o mais próximo de qualquer lado. `exact` diz qual foi.
    [[nodiscard]] FrameRef find(i64 targetUs, i64 halfFrameUs, bool* exact) noexcept;

    [[nodiscard]] bool contains(i64 targetUs, i64 halfFrameUs) const noexcept;

    /// Maior pts guardado que ainda está à frente de `fromUs` de forma
    /// contígua (sem buraco maior que um frame). O decoder usa para saber até
    /// onde já pré-carregou.
    [[nodiscard]] i64 contiguous_end(i64 fromUs, i64 frameUs) const noexcept;

    void clear() noexcept;
    [[nodiscard]] Stats stats() const noexcept;

private:
    void evict_locked() noexcept;
    [[nodiscard]] f64 cost_locked(i64 ptsUs) const noexcept;

    mutable std::mutex mutex_;
    std::vector<FrameRef> frames_;   ///< ordenado por pts
    Config config_{};
    Stats  stats_{};
    i64    focusUs_ = 0;
    i32    direction_ = 0;
};

} // namespace aurea
