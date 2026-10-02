// =============================================================================
//  Aurea / export / BitratePolicy.hpp
//
//  A taxa de bits do export — UMA regra, usada pelo motor (encoder de verdade)
//  e pelas duas interfaces (tamanho estimado na tela Exportar). Antes cada
//  lado tinha a sua conta (0,2 bit por pixel·s, ×1,6 na "Alta", sem teto
//  depois do fator): 4K60 "Alta" pedia 160 Mbps — 1,2 GB por minuto.
//
//  Referência (H.264, qualidade Normal, 30 fps), na faixa dos editores comuns:
//      480p ≈ 3,5 Mbps · 720p ≈ 7 · 1080p ≈ 14 · 1440p ≈ 22 · 4K ≈ 40
//  Entre os pontos, interpolação log-log pela quantidade de pixels. A taxa de
//  quadros entra com expoente 0,6 (60 fps ≈ ×1,5, não ×2: quadros vizinhos
//  se parecem mais). Qualidade: Baixa ×0,6, Normal ×1, Alta ×1,5. HEVC ×0,65
//  (mesma qualidade visual com ~35% menos bits).
//
//  Header-only: sem .cpp novo para os três sistemas de build.
// =============================================================================
#pragma once

#include <algorithm>
#include <cmath>

#include "aurea/core/Types.hpp"

namespace aurea {

enum class ExportQuality : u32 { Low = 0, Normal = 1, High = 2 };

/// Teto de qualquer export automático ou manual (bps). Acima disso nenhum
/// aparelho de celular ganha qualidade visível — só arquivo gigante.
inline constexpr f64 kExportMaxVideoBps = 100.0e6;
inline constexpr f64 kExportMinVideoBps = 0.5e6;
/// Áudio AAC-LC estéreo do export.
inline constexpr u32 kExportAudioKbps = 192;

/// Multiplicador da qualidade escolhida.
[[nodiscard]] inline f64 export_quality_factor(ExportQuality q) noexcept {
    switch (q) {
        case ExportQuality::Low: return 0.6;
        case ExportQuality::High: return 1.5;
        default: return 1.0;
    }
}

/// Taxa de vídeo recomendada (bps) para a saída `width`×`height` a `fps`.
/// `customMbps` > 0 = o usuário escolheu à mão (respeitado, só com teto).
[[nodiscard]] inline u32 export_video_bitrate_bps(u32 width, u32 height, f64 fps, ExportCodec codec,
                                                  ExportQuality quality, u32 customMbps = 0) noexcept {
    if (customMbps > 0) {
        return static_cast<u32>(std::clamp(static_cast<f64>(customMbps) * 1.0e6, kExportMinVideoBps, kExportMaxVideoBps));
    }
    struct Anchor { f64 pixels, mbps; };
    static constexpr Anchor kAnchors[] = {
        {854.0 * 480.0, 3.5}, {1280.0 * 720.0, 7.0}, {1920.0 * 1080.0, 14.0},
        {2560.0 * 1440.0, 22.0}, {3840.0 * 2160.0, 40.0},
    };
    constexpr usize n = sizeof(kAnchors) / sizeof(kAnchors[0]);
    const f64 px = std::max(1.0, static_cast<f64>(width) * static_cast<f64>(height));
    // Segmento log-log (fora da tabela, extrapola com a inclinação da ponta).
    usize i = 0;
    while (i + 2 < n && px > kAnchors[i + 1].pixels) ++i;
    const Anchor a = kAnchors[i], b = kAnchors[i + 1];
    const f64 slope = std::log(b.mbps / a.mbps) / std::log(b.pixels / a.pixels);
    f64 mbps = a.mbps * std::pow(px / a.pixels, slope);
    const f64 f = fps > 0.0 ? fps : 30.0;
    mbps *= std::pow(std::clamp(f, 1.0, 240.0) / 30.0, 0.6);
    mbps *= export_quality_factor(quality);
    if (codec == ExportCodec::HEVC || codec == ExportCodec::AV1) mbps *= 0.65;
    return static_cast<u32>(std::clamp(mbps * 1.0e6, kExportMinVideoBps, kExportMaxVideoBps));
}

/// Tamanho estimado do arquivo (bytes): vídeo + áudio + ~1,5% de contêiner.
[[nodiscard]] inline u64 export_estimated_bytes(u32 videoBps, u32 audioBps, f64 seconds) noexcept {
    if (!(seconds > 0.0)) return 0;
    const f64 bits = (static_cast<f64>(videoBps) + static_cast<f64>(audioBps)) * seconds;
    return static_cast<u64>(bits / 8.0 * 1.015);
}

} // namespace aurea
