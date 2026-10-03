// =============================================================================
//  Aurea / export / ExportRules.hpp
//
//  Duas regras do export de vídeo que as DUAS interfaces precisam repetir
//  igual ao motor:
//
//  1) O TAMANHO do quadro. "480p" de uma composição 16:9 dava 854×480 — par,
//     mas não múltiplo de 16 (nem de 4: a croma ficava com 427 de largura). O
//     encoder MediaTek do Vivo Y30 (Helio P35) declara passo alinhado em 16
//     (864) e entrega um buffer calculado com a largura crua: o quadro não
//     cabia e o export morria em "buffer do encoder menor que o quadro". O
//     lado MAIOR sai em múltiplo de 16 e o MENOR só par, como antes (1080
//     continua 1080, 1080×1920 continua 1080×1920; altura e largura de
//     retrato são as que todo encoder testa). A proporção muda no
//     máximo meio bloco de 16 — 854→848 é 0,6%, invisível. Composição
//     quadrada fica quadrada (os dois lados pares). O que um encoder ainda
//     recusar o sink do Android resolve trocando para o de software.
//
//  2) O MOTIVO de uma falha, num código estável. A mensagem do motor/sink é
//     diagnóstico em português para o log; a tela mostra o texto do catálogo
//     de cada idioma pelo código (vai nos bits altos de `flags` do progresso,
//     sem mudar o tamanho do POD).
//
//  Header-only: sem .cpp novo para os três sistemas de build.
// =============================================================================
#pragma once

#include <algorithm>
#include <cmath>

#include "aurea/core/Result.hpp"
#include "aurea/core/Types.hpp"

namespace aurea {

/// Alinhamento do lado maior / menor do quadro exportado.
inline constexpr u32 kExportLongSideAlign = 16;
inline constexpr u32 kExportShortSideAlign = 2;

struct ExportFrameSize {
    u32 width = 0;
    u32 height = 0;
};

/// `v` no múltiplo de `a` mais próximo (empate sobe), nunca abaixo de `a`.
[[nodiscard]] inline u32 export_align_nearest(f64 v, u32 a) noexcept {
    if (a == 0) a = 2;
    if (!(v > 0.0) || !std::isfinite(v)) return a;
    const f64 blocks = std::floor(v / static_cast<f64>(a) + 0.5);
    return std::max<u32>(a, static_cast<u32>(blocks) * a);
}

/// Tamanho do vídeo exportado: o lado menor pedido (`wantShort`, 0 = o da
/// composição) e o maior pela proporção da composição, com os alinhamentos
/// acima. É a regra do motor (Engine::start_export) e o que as telas mostram.
[[nodiscard]] inline ExportFrameSize export_frame_size(u32 compW, u32 compH, u32 wantShort) noexcept {
    ExportFrameSize out;
    if (compW == 0 || compH == 0) return out;
    const u32 compShort = std::min(compW, compH);
    const u32 shortSide = wantShort > 0 ? wantShort : compShort;
    const f64 k = static_cast<f64>(shortSide) / static_cast<f64>(compShort);
    const f64 w = static_cast<f64>(compW) * k;
    const f64 h = static_cast<f64>(compH) * k;
    if (compW == compH) {
        out.width = out.height = export_align_nearest(w, kExportShortSideAlign);
    } else if (compW > compH) {
        out.width = export_align_nearest(w, kExportLongSideAlign);
        out.height = export_align_nearest(h, kExportShortSideAlign);
    } else {
        out.width = export_align_nearest(w, kExportShortSideAlign);
        out.height = export_align_nearest(h, kExportLongSideAlign);
    }
    return out;
}

/// Bytes de um quadro NV12/I420 contíguo de `width`×`height` (passo = largura).
[[nodiscard]] inline usize export_yuv420_bytes(u32 width, u32 height) noexcept {
    return static_cast<usize>(width) * height + 2u * static_cast<usize>((width + 1) / 2) * ((height + 1) / 2);
}

// -----------------------------------------------------------------------------
// Motivo da falha (código estável; o MESMO número nas duas interfaces).
// -----------------------------------------------------------------------------
enum class ExportFailure : u32 {
    None = 0,
    Encoder = 1,          ///< o encoder do aparelho recusou/quebrou (formato, buffer, erro do codec)
    EncoderStalled = 2,   ///< o encoder parou de aceitar quadros (tempo esgotado)
    Render = 3,           ///< a GPU não renderizou o quadro
    GpuMemory = 4,        ///< sem memória (GPU ou RAM) para o export
    Media = 5,            ///< uma mídia do projeto não pôde ser lida
    File = 6,             ///< o arquivo de saída não pôde ser gravado
    Storage = 7,          ///< armazenamento cheio
    Unsupported = 8,      ///< resolução/codec acima do que o aparelho exporta
    Other = 15,
};

/// Onde a falha aconteceu (o motor sabe; a mensagem do sink não é contrato).
enum class ExportStage : u32 { Open = 0, Render = 1, Encode = 2, Finish = 3 };

/// Bits de `flags` do progresso que levam o motivo (24..31).
inline constexpr u32 kExportFailureShift = 24;

[[nodiscard]] inline ExportFailure export_failure_reason(ExportStage stage, Errc code) noexcept {
    switch (code) {
        case Errc::Ok:
        case Errc::Cancelled: return ExportFailure::None;
        case Errc::StorageFull: return ExportFailure::Storage;
        case Errc::OutOfMemory:
        case Errc::BudgetExceeded:
        case Errc::OutOfDeviceMemory: return ExportFailure::GpuMemory;
        case Errc::DecodeFailed:
        case Errc::MediaSourceMissing:
        case Errc::AssetCorrupted: return ExportFailure::Media;
        case Errc::DeviceLost:
        case Errc::PipelineCompileFailed:
        case Errc::ShaderCompileFailed:
        case Errc::SurfaceLost: return ExportFailure::Render;
        default: break;
    }
    switch (stage) {
        case ExportStage::Open:
            return code == Errc::NotSupported || code == Errc::UnsupportedCodec || code == Errc::UnsupportedFormat
                       ? ExportFailure::Unsupported
                   : code == Errc::IoError ? ExportFailure::File : ExportFailure::Encoder;
        case ExportStage::Render:
            return ExportFailure::Render;
        case ExportStage::Encode:
            return code == Errc::Timeout ? ExportFailure::EncoderStalled : ExportFailure::Encoder;
        case ExportStage::Finish:
            return code == Errc::Timeout ? ExportFailure::EncoderStalled
                   : code == Errc::IoError ? ExportFailure::File : ExportFailure::Encoder;
    }
    return ExportFailure::Other;
}

} // namespace aurea
