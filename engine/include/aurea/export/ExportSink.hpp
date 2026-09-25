// =============================================================================
//  Aurea / export / ExportSink.hpp
//
//  A fronteira entre o export do motor e o encoder da plataforma.
//
//  O motor faz tudo que é igual em todo aparelho: percorre a timeline no
//  tempo de SAÍDA, renderiza cada quadro com o MESMO renderer do preview,
//  converte para Y'CbCr 4:2:0 na GPU e mixa o áudio. A plataforma só recebe
//  planos prontos e PCM e os entrega ao hardware:
//
//    Android  MediaCodec (H.264/HEVC/AAC) + AMediaMuxer
//    iOS      VideoToolbox/AVAssetWriter (codifica e multiplexa juntos — por
//             isso a fronteira é "sink", e não "encoder" + "muxer")
//
//  Tempo: tudo em microssegundos a partir do zero do vídeo; o motor já manda
//  os carimbos monotônicos e exatos (quadro i → i·10⁶/fps).
// =============================================================================
#pragma once

#include <memory>

#include "aurea/core/Result.hpp"
#include "aurea/core/Types.hpp"

namespace aurea {

/// Metadados de cor gravados no arquivo. O motor sempre exporta SDR BT.709 de
/// faixa limitada; o campo existe para o arquivo DIZER isso (sem a etiqueta,
/// player e galeria chutam, e um vídeo de celular vira BT.601 lavado).
struct ExportColorTags {
    u8 matrix = 1;     ///< 1 = BT.709
    u8 primaries = 1;  ///< 1 = BT.709
    u8 transfer = 1;   ///< 1 = BT.709 (SDR)
    bool fullRange = false;
};

struct VideoStreamConfig {
    u32 width = 0;             ///< par
    u32 height = 0;            ///< par
    f64 fps = 30.0;
    ExportCodec codec = ExportCodec::H264;
    u32 bitrateBps = 0;
    u32 keyframeIntervalFrames = 0;   ///< 0 = 2 s
    ExportColorTags color{};
};

struct AudioStreamConfig {
    u32 sampleRate = 48000;
    u32 channels = 2;
    u32 bitrateBps = 192000;
};

class ExportSink {
public:
    virtual ~ExportSink() = default;

    /// Abre o arquivo e configura os encoders. `audio` nulo = vídeo sem som.
    [[nodiscard]] virtual Status open(const char* outputPath, const VideoStreamConfig& video,
                                      const AudioStreamConfig* audio) noexcept = 0;

    /// Um quadro NV12: plano Y (largura × altura) e plano CbCr intercalado
    /// (largura/2 × altura/2 pares). Pode bloquear enquanto o encoder não tem
    /// buffer livre — é o que segura o ritmo do export.
    [[nodiscard]] virtual Status write_video(const u8* y, u32 yStride, const u8* uv, u32 uvStride,
                                             i64 ptsUs) noexcept = 0;
    /// Exact source presentation duration for VFR proxy generation. Containers
    /// driven by PTS derive intermediate durations; VT also accepts them explicitly.
    [[nodiscard]] virtual Status write_video_timed(const u8* y, u32 yStride, const u8* uv, u32 uvStride,
                                                  i64 ptsUs, i64 durationUs) noexcept {
        (void)durationUs;
        return write_video(y, yStride, uv, uvStride, ptsUs);
    }

    /// PCM 16 bits intercalado, `frames` amostras por canal.
    [[nodiscard]] virtual Status write_audio(const i16* interleaved, u32 frames, i64 ptsUs) noexcept = 0;

    /// Fecha os fluxos (EOS), esvazia os encoders e finaliza o contêiner.
    [[nodiscard]] virtual Status finish() noexcept = 0;

    /// Cancelamento: solta tudo e apaga o arquivo parcial.
    virtual void abort() noexcept = 0;

    /// O encoder que o `open` REALMENTE conseguiu — para o log e para a UI
    /// avisar quando o aparelho caiu para software (nada de export várias
    /// vezes mais lento em silêncio).
    enum class Acceleration : u8 { Unknown = 0, Hardware, Software };
    struct EncoderInfo {
        char name[64]{};
        Acceleration acceleration = Acceleration::Unknown;
    };
    [[nodiscard]] virtual EncoderInfo encoder_info() const noexcept { return EncoderInfo{}; }
};

/// A plataforma registra a fábrica em `EngineConfig`. Nula = export indisponível
/// (o motor recusa com NotSupported em vez de fingir).
using ExportSinkFactory = std::unique_ptr<ExportSink> (*)(void* user);

} // namespace aurea
