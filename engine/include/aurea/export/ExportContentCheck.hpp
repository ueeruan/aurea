// =============================================================================
//  Aurea / export / ExportContentCheck.hpp
//
//  Conferência do CONTEÚDO do arquivo final. A validação independente já prova
//  que o MP4 abre, decodifica e tem o relógio certo, mas um vídeo verde ou
//  preto também passa nisso. Aqui, uma amostra dos quadros entregues ao encoder
//  vira uma grade de médias, e o quadro decodificado do arquivo tem de bater.
//
//  Os limites são folgados de propósito: compressão forte muda pouco a média
//  de um bloco; plano zerado (verde), quadro preto ou cor trocada mudam muito.
// =============================================================================
#pragma once

#include "aurea/core/Types.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace aurea::export_content {

inline constexpr u32 kGridX = 16, kGridY = 9, kGridCells = kGridX * kGridY;
inline constexpr u32 kMaxSamples = 240;       ///< cerca de 100 KB no pior caso
inline constexpr u32 kFirstInterval = 15;     ///< quadros entre amostras no começo
inline constexpr f64 kLumaLimit = 24.0;       ///< média do erro por bloco, 8 bits
inline constexpr f64 kChromaLimit = 16.0;
inline constexpr f64 kSilenceLevel = 16.0;    ///< média de |PCM| 16 bits (cerca de -66 dBFS)
inline constexpr f64 kLostAudioRatio = 0.2;

struct Fingerprint {
    u32 frame = 0;
    u8 y[kGridCells]{}, cb[kGridCells]{}, cr[kGridCells]{};
};

/// NV12: `cb` e `cr` apontam para o mesmo plano, deslocados em 1, com passo 2.
/// Planar: um plano para cada, com passo 1.
struct Planes {
    const u8* y = nullptr;
    const u8* cb = nullptr;
    const u8* cr = nullptr;
    i32 yStride = 0, cbStride = 0, crStride = 0;
    u32 chromaStep = 1;
};

struct Difference {
    f64 meanY = 0.0, meanC = 0.0, worstY = 0.0, worstC = 0.0;
};

inline void compute(const Planes& p, u32 width, u32 height, Fingerprint& out) noexcept {
    const u32 w = std::max<u32>(width, 2), h = std::max<u32>(height, 2);
    for (u32 gy = 0; gy < kGridY; ++gy) {
        const u32 y0 = gy * h / kGridY, y1 = std::max(y0 + 1, (gy + 1) * h / kGridY);
        for (u32 gx = 0; gx < kGridX; ++gx) {
            const u32 x0 = gx * w / kGridX, x1 = std::max(x0 + 1, (gx + 1) * w / kGridX);
            u32 sumY = 0, sumCb = 0, sumCr = 0, luma = 0, chroma = 0;
            for (u32 y = y0; y < y1 && y < h; y += 3) {
                const u8* row = p.y + static_cast<i64>(y) * p.yStride;
                for (u32 x = x0; x < x1 && x < w; x += 3) { sumY += row[x]; ++luma; }
                const u8* cb = p.cb + static_cast<i64>(y / 2) * p.cbStride;
                const u8* cr = p.cr + static_cast<i64>(y / 2) * p.crStride;
                for (u32 x = x0 / 2; x <= (x1 - 1) / 2 && x < w / 2; x += 2) {
                    sumCb += cb[x * p.chromaStep];
                    sumCr += cr[x * p.chromaStep];
                    ++chroma;
                }
            }
            const u32 cell = gy * kGridX + gx;
            out.y[cell] = static_cast<u8>(luma ? sumY / luma : 0);
            out.cb[cell] = static_cast<u8>(chroma ? sumCb / chroma : 128);
            out.cr[cell] = static_cast<u8>(chroma ? sumCr / chroma : 128);
        }
    }
}

[[nodiscard]] inline Difference compare(const Fingerprint& a, const Fingerprint& b) noexcept {
    Difference d;
    u32 sumY = 0, sumC = 0, worstY = 0, worstC = 0;
    for (u32 i = 0; i < kGridCells; ++i) {
        const u32 ey = static_cast<u32>(std::abs(static_cast<int>(a.y[i]) - b.y[i]));
        const u32 ec = static_cast<u32>(std::max(std::abs(static_cast<int>(a.cb[i]) - b.cb[i]),
                                                 std::abs(static_cast<int>(a.cr[i]) - b.cr[i])));
        sumY += ey; sumC += ec;
        worstY = std::max(worstY, ey); worstC = std::max(worstC, ec);
    }
    d.meanY = static_cast<f64>(sumY) / kGridCells;
    d.meanC = static_cast<f64>(sumC) / kGridCells;
    d.worstY = worstY; d.worstC = worstC;
    return d;
}

[[nodiscard]] constexpr bool damaged(const Difference& d) noexcept {
    return d.meanY > kLumaLimit || d.meanC > kChromaLimit;
}

/// O som entregue ao encoder tinha nível e o arquivo saiu mudo.
[[nodiscard]] constexpr bool audio_lost(f64 sourceLevel, f64 decodedLevel) noexcept {
    return sourceLevel > kSilenceLevel && decodedLevel < sourceLevel * kLostAudioRatio;
}

} // namespace aurea::export_content
