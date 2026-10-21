#pragma once

// =============================================================================
//  Aurea / ai / RotoRefine.hpp
//
//  Refinar borda do Roto Brush na resolução da FONTE (referência de
//  comportamento: Refine Edge do After Effects — raio da borda, reduzir
//  trepidação, descontaminar cores). O recorte da grade 320² (RotoMatte) dá
//  a forma; aqui só a FAIXA em volta da borda dele é decidida de novo pelos
//  pixels do quadro:
//
//   - Trimapa: distância (px) até a borda 0,5 do recorte ampliado, estimada
//     pelo gradiente bilinear. |d| < raio = desconhecido; o resto é objeto ou
//     fundo certo. Só os blocos 64×64 que cortam a faixa são processados.
//   - Cor local: médias do objeto certo e do fundo certo numa janela de ~2×
//     o raio; o alfa sai da projeção da cor do pixel na reta fundo→objeto,
//     com confiança pela separação das duas cores e pelo resíduo.
//   - Filtro guiado rápido (He, Sun e Tang, "Guided Image Filtering" e "Fast
//     Guided Filter", publicados): a mesma projeção, suavizada respeitando as
//     arestas do quadro (cor como guia), em grade reduzida s = 1/2/4 conforme
//     o raio; os coeficientes voltam à resolução cheia — o detalhe (cabelo)
//     vem da guia.
//   - Reduzir trepidação: o alfa do quadro anterior da cadeia, levado pelo
//     fluxo óptico, entra na faixa só onde a diferença é pequena (ruído). Onde
//     muda muito (movimento real, fluxo errado) vale o quadro atual: não atrasa.
//   - Descontaminar: cor do objeto na faixa por Blur-Fusion (Forte e Pitié,
//     "Approximate Fast Foreground Colour Estimation", publicado):
//     F = F̂ + α(I − αF̂ − (1−α)B̂), F̂/B̂ médias ponderadas pelo alfa.
//
//  Tudo CPU, em blocos (memória limitada ao bloco), determinístico: a cadeia
//  de propagação (RotoService) chega sempre ao mesmo valor.
// =============================================================================

#include "aurea/core/Math.hpp"
#include "aurea/core/Types.hpp"

#include <vector>

namespace aurea::ai {

/// Quadro da fonte na resolução do refino (RGB8 intercalado, sem preenchimento).
struct RotoImage {
    u32 width = 0, height = 0;
    std::vector<u8> rgb;
};

struct RotoRefineSettings {
    f32 radius = 0.0f;        ///< px do quadro do refino, de cada lado da borda; 0 = sem refino
    f32 chatter = 0.0f;       ///< 0..1: peso do quadro anterior levado pelo fluxo (só na faixa)
    bool decontaminate = false;
    [[nodiscard]] bool on() const noexcept { return radius >= 0.5f; }
    /// Muda quando muda o resultado (chave do cache).
    [[nodiscard]] u64 hash() const noexcept;
};

/// O recorte refinado de um quadro.
struct RotoRefined {
    u32 width = 0, height = 0;
    std::vector<u8> alpha;    ///< width·height
    std::vector<u8> fg;       ///< vazio, ou width·height·3: cor do objeto (vale onde 0 < alfa < 255)
};

/// O passo anterior da cadeia, para reduzir trepidação.
struct RotoTemporal {
    const RotoRefined* prev = nullptr;   ///< mesmo tamanho do quadro
    const Vec2* flow = nullptr;          ///< n·n, quadro atual → anterior, px da grade
    u32 n = 0;
};

/// Maior lado do quadro do refino (4K refina em 1920: a borda continua 6× mais fina que a grade).
inline constexpr u32 kRotoRefineMaxSide = 1920;

/// Tamanho do quadro do refino para uma fonte w×h (proporção mantida).
void roto_refine_size(u32 w, u32 h, u32 maxSide, u32& outW, u32& outH) noexcept;

/// RGB8/RGBA8 (stride em bytes) → quadro do refino com redução por área.
void roto_image_from(const u8* pixels, u32 width, u32 height, u32 stride, u32 channels, u32 maxSide, RotoImage& out);

/// Média por área do quadro numa grade n×n (o fluxo da trepidação).
void roto_grid_rgb(const RotoImage& image, u32 n, std::vector<u8>& out);

/// Threads do refino (0 = metade dos núcleos, até 4). Os blocos são
/// independentes: o resultado não muda com o número de threads.
extern u32 g_rotoRefineThreads;

/// Refina o recorte `coarse` (n×n, 0..1, o quadro inteiro) no quadro `frame`.
void roto_refine(const f32* coarse, u32 n, const RotoImage& frame, const RotoRefineSettings& settings,
                 const RotoTemporal* temporal, RotoRefined& out);

/// Compacto (PackBits do alfa + as cores só dos pixels de borda).
void roto_refined_pack(const RotoRefined& refined, std::vector<u8>& out);
[[nodiscard]] bool roto_refined_unpack(const std::vector<u8>& packed, RotoRefined& out);

} // namespace aurea::ai
