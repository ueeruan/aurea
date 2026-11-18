#pragma once
// =============================================================================
//  Aurea / render / PreRenderPlan.hpp
//
//  Pre Render & Pre Export (1.0.2): as contas puras do painel — tamanho de
//  cada qualidade (Inteira, 1/2, 1/4, 1/8), quanto cabe no cache ("cabe ~Xs")
//  e o livro de edições por trecho que diz quais quadros de um PreRender ou de
//  um pré-export continuam valendo depois de uma edição. Sem E/S, sem lock:
//  o motor (EnginePreRenderRange.cpp) junta isto ao cache e ao export, e os
//  testes de host conferem cada conta.
// =============================================================================
#include "aurea/core/Types.hpp"

#include <utility>
#include <vector>

namespace aurea::prerender {

/// Divisores aceitos (Inteira, 1/2, 1/4, 1/8). Qualquer outro vira 1/2.
inline constexpr u32 kQualityDivisors[4] = {1u, 2u, 4u, 8u};
inline constexpr u32 kDefaultDivisor = 2u;
[[nodiscard]] constexpr bool valid_divisor(u32 d) noexcept { return d == 1u || d == 2u || d == 4u || d == 8u; }
[[nodiscard]] constexpr u32 normalize_divisor(u32 d) noexcept { return valid_divisor(d) ? d : kDefaultDivisor; }

/// O que o painel faz com o trecho.
enum class Mode : u32 {
    PreRender = 0,   ///< quadros RGBA16F comprimidos no cache privado (PreRenderCache)
    PreExport = 1,   ///< um MP4 H.264 do trecho no cache (a reprodução toca o vídeo)
};

struct ScaledSize {
    u32 width = 0, height = 0;
};
/// Tamanho da composição dividido pela qualidade; lados pares (o encoder e o
/// NV12 pedem) e nunca menores que 16.
[[nodiscard]] ScaledSize scaled_size(u32 compWidth, u32 compHeight, u32 divisor) noexcept;

/// Bytes por pixel que o cache grava por quadro (RGBA16F = 8 brutos, zlib
/// rápido). Sem medida do aparelho vale esta estimativa conservadora.
inline constexpr f64 kDefaultCacheBytesPerPixel = 5.0;
inline constexpr u64 kCacheFrameOverheadBytes = 96 + 12 * 64;   ///< cabeçalho + blocos do quadro
/// Um quadro do PreRender no disco. `measuredBytesPerPixel` > 0 = a medida da
/// última sessão neste aparelho (vale mais que a estimativa).
[[nodiscard]] u64 cache_frame_bytes(u32 width, u32 height, f64 measuredBytesPerPixel = 0.0) noexcept;
/// Bytes por segundo do pré-export (H.264 na taxa automática "Alta" do
/// export + o AAC de 192 kb/s).
[[nodiscard]] u64 video_bytes_per_second(u32 width, u32 height, f64 fps) noexcept;

/// O espaço que o painel pode usar. `capBytes`: teto do cache privado;
/// `reclaimableBytes`: o que o trabalho anterior do mesmo tipo ocupa (sai
/// quando o novo começa); `freeDiskBytes`: livre no volume do cache (0 com
/// `freeDiskKnown` falso = sem medida, só o teto vale); `reserveBytes`: o que
/// sempre fica livre para o sistema. Como o PreRenderCache: no máximo metade do
/// que sobra acima da reserva.
struct Budget {
    u64 capBytes = 0;
    u64 reclaimableBytes = 0;
    u64 freeDiskBytes = 0;
    bool freeDiskKnown = true;
    u64 reserveBytes = 0;
};
[[nodiscard]] u64 available_bytes(const Budget& budget) noexcept;

/// "Cabe ~Xs": segundos de vídeo que cabem em `availableBytes` a `bytesPerSecond`.
[[nodiscard]] f64 fits_seconds(u64 availableBytes, u64 bytesPerSecond) noexcept;

struct Fit {
    u32 width = 0, height = 0;   ///< tamanho dos quadros nesta qualidade
    u32 divisor = kDefaultDivisor;
    f64 fps = 0.0;
    u64 bytesPerSecond = 0;
    u64 availableBytes = 0;
    f64 fitsSeconds = 0.0;       ///< quanto cabe
    f64 rangeSeconds = 0.0;      ///< duração do trecho escolhido
    u64 rangeBytes = 0;          ///< quanto o trecho ocuparia
    bool fits = false;           ///< o trecho inteiro cabe
};
[[nodiscard]] Fit plan_fit(u32 compWidth, u32 compHeight, f64 fps, u32 divisor, Mode mode, const Budget& budget,
                           i64 rangeFrames, f64 measuredBytesPerPixel = 0.0) noexcept;

// -----------------------------------------------------------------------------
// Livro de edições por trecho.
//
// A composição é dividida em trechos de `kSegmentFrames` quadros; cada trecho
// tem um hash de edição. Uma edição que o motor sabe localizar (uma camada sem
// ninguém olhando para ela em outro instante, preview_local_range) mexe só no
// hash dos trechos que toca; qualquer outra mexe em todos (`touch_all`). Um
// PreRender/pré-export guarda o retrato dos hashes do trecho dele na partida:
// um quadro continua valendo enquanto o hash do trecho dele não mudou.
// -----------------------------------------------------------------------------
class EditLedger {
public:
    static constexpr i64 kSegmentFrames = 8;
    struct Snapshot {
        i64 in = 0, out = 0;              ///< [in, out) em quadros da composição
        u64 epoch = 0;                    ///< touch_all do momento
        std::vector<u64> segments;        ///< hash de cada trecho que cruza [in, out)
        [[nodiscard]] bool empty() const noexcept { return out <= in; }
    };

    /// Esquece tudo (projeto novo). Retratos antigos deixam de valer.
    void reset() noexcept;
    /// Uma edição nos quadros [from, to). from < 0 vira 0; to <= from = nada.
    void touch(i64 from, i64 to) noexcept;
    /// Uma edição que pode mudar qualquer quadro.
    void touch_all() noexcept;
    [[nodiscard]] Snapshot capture(i64 in, i64 out) const;
    /// O quadro `frame` (da composição) ainda é o do retrato?
    [[nodiscard]] bool frame_clean(const Snapshot& snapshot, i64 frame) const noexcept;
    /// Quantos trechos do retrato mudaram.
    [[nodiscard]] u32 dirty_segments(const Snapshot& snapshot) const noexcept;
    /// Algum quadro do retrato ainda vale?
    [[nodiscard]] bool any_clean(const Snapshot& snapshot) const noexcept;
    [[nodiscard]] bool all_clean(const Snapshot& snapshot) const noexcept { return !snapshot.empty() && dirty_segments(snapshot) == 0; }
    /// Os trechos que mudaram, já juntos e recortados a [in, out) — a régua
    /// pinta estes de outra cor.
    [[nodiscard]] std::vector<std::pair<i64, i64>> dirty_ranges(const Snapshot& snapshot) const;
    [[nodiscard]] u64 epoch() const noexcept { return epoch_; }

private:
    [[nodiscard]] u64 segment_hash(i64 segment) const noexcept;
    u64 epoch_ = 1;
    u64 serial_ = 0;
    std::vector<u64> stamps_;   ///< hash próprio de cada trecho (0 = nunca editado)
};

/// O trecho do painel: o escolhido (Início/Fim), senão a área de trabalho,
/// senão do começo ao fim do conteúdo. Sempre dentro de [0, duration).
struct RangeChoice {
    i64 in = 0, out = 0;
    bool explicitRange = false;   ///< veio de Início/Fim (não da área de trabalho/inteiro)
};
[[nodiscard]] RangeChoice resolve_range(i64 selectedIn, i64 selectedOut, bool hasWorkArea, i64 workIn, i64 workOut,
                                        i64 contentEnd, i64 duration) noexcept;
/// Início no cabeçote: o fim fica se ainda estiver depois; senão vai ao fim do conteúdo.
[[nodiscard]] std::pair<i64, i64> range_with_start(i64 playhead, i64 in, i64 out, i64 contentEnd, i64 duration) noexcept;
/// Fim no cabeçote (o quadro do cabeçote entra): o início fica se estiver antes; senão 0.
[[nodiscard]] std::pair<i64, i64> range_with_end(i64 playhead, i64 in, i64 out, i64 duration) noexcept;

} // namespace aurea::prerender
