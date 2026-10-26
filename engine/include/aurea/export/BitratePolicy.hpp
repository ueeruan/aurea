// =============================================================================
//  Aurea / export / BitratePolicy.hpp
//
//  A taxa de bits do export — UMA regra, usada pelo motor (encoder de verdade)
//  e pelas duas interfaces (tamanho estimado na tela Exportar). Antes cada
//  lado tinha a sua conta (0,2 bit por pixel·s, ×1,6 na "Alta", sem teto
//  depois do fator): 4K60 "Alta" pedia 160 Mbps — 1,2 GB por minuto.
//
//  Referência (H.264, qualidade Normal, 30 fps), na faixa dos editores comuns:
//      480p ≈ 2,5 Mbps · 720p ≈ 5 · 1080p ≈ 8 · 1440p ≈ 16 · 4K ≈ 32
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

/// Limites do perfil automático e da taxa explicitamente escolhida (bps).
/// A taxa manual continua disponível para projetos que precisam de mais bits.
inline constexpr f64 kExportMaxAutoVideoBps = 60.0e6;
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

// --- Omega adicionar (área 08, P1-9): qualidade em porcentagem ---------------
// A tela da Omega usa um controle deslizante de 20 a 100 (o da referência). O
// fator é do AUREA (a referência só dá o intervalo), linear por partes e
// casado com as três fichas antigas: 20 → 0,6 (Baixa) · 60 → 1,0 (Normal) ·
// 100 → 1,5 (Alta). A taxa manual do "Avançado" continua vencendo.
inline constexpr u32 kExportQualityPercentMin = 20;
inline constexpr u32 kExportQualityPercentMax = 100;
inline constexpr u32 kExportQualityPercentDefault = 60;

[[nodiscard]] inline f64 export_quality_factor_percent(u32 percent) noexcept {
    const f64 q = std::clamp(static_cast<f64>(percent), static_cast<f64>(kExportQualityPercentMin),
                             static_cast<f64>(kExportQualityPercentMax));
    return q <= 60.0 ? 0.6 + (q - 20.0) * (0.4 / 40.0) : 1.0 + (q - 60.0) * (0.5 / 40.0);
}

/// O código de qualidade da sessão (`ExportSettings::quality`, que viaja pelas
/// pontes e pelo checkpoint como u32): 0, 1, 2 = Baixa, Normal, Alta (as fichas
/// antigas); 3 ou mais = porcentagem (presa em 20..100, a tela da Omega).
[[nodiscard]] inline f64 export_quality_factor_code(u32 code) noexcept {
    return code <= 2 ? export_quality_factor(static_cast<ExportQuality>(code)) : export_quality_factor_percent(code);
}

/// A conta da taxa automática com o fator de qualidade já resolvido.
[[nodiscard]] inline u32 export_video_bitrate_bps_factor(u32 width, u32 height, f64 fps, ExportCodec codec,
                                                         f64 qualityFactor, u32 customMbps = 0) noexcept;

/// Taxa de vídeo recomendada (bps) para a saída `width`×`height` a `fps`.
/// `customMbps` > 0 = o usuário escolheu à mão (respeitado, só com teto).
[[nodiscard]] inline u32 export_video_bitrate_bps(u32 width, u32 height, f64 fps, ExportCodec codec,
                                                  ExportQuality quality, u32 customMbps = 0) noexcept {
    return export_video_bitrate_bps_factor(width, height, fps, codec, export_quality_factor(quality), customMbps);
}

/// Omega adicionar: o mesmo, com o código de qualidade (0..2 ou porcentagem).
[[nodiscard]] inline u32 export_video_bitrate_bps_code(u32 width, u32 height, f64 fps, ExportCodec codec,
                                                       u32 qualityCode, u32 customMbps = 0) noexcept {
    return export_video_bitrate_bps_factor(width, height, fps, codec, export_quality_factor_code(qualityCode), customMbps);
}

[[nodiscard]] inline u32 export_video_bitrate_bps_factor(u32 width, u32 height, f64 fps, ExportCodec codec,
                                                         f64 qualityFactor, u32 customMbps) noexcept {
    if (customMbps > 0) {
        return static_cast<u32>(std::clamp(static_cast<f64>(customMbps) * 1.0e6, kExportMinVideoBps, kExportMaxVideoBps));
    }
    struct Anchor { f64 pixels, mbps; };
    static constexpr Anchor kAnchors[] = {
        {854.0 * 480.0, 2.5}, {1280.0 * 720.0, 5.0}, {1920.0 * 1080.0, 8.0},
        {2560.0 * 1440.0, 16.0}, {3840.0 * 2160.0, 32.0},
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
    mbps *= qualityFactor > 0.0 ? qualityFactor : 1.0;
    if (codec == ExportCodec::HEVC || codec == ExportCodec::AV1) mbps *= 0.65;
    return static_cast<u32>(std::clamp(mbps * 1.0e6, kExportMinVideoBps, kExportMaxAutoVideoBps));
}

/// Tamanho estimado do arquivo (bytes): vídeo + áudio + ~1,5% de contêiner.
[[nodiscard]] inline u64 export_estimated_bytes(u32 videoBps, u32 audioBps, f64 seconds) noexcept {
    if (!(seconds > 0.0)) return 0;
    const f64 bits = (static_cast<f64>(videoBps) + static_cast<f64>(audioBps)) * seconds;
    return static_cast<u64>(bits / 8.0 * 1.015);
}

} // namespace aurea
