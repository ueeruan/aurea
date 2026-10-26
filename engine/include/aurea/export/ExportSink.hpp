// =============================================================================
//  Aurea / export / ExportSink.hpp
//
//  A fronteira entre a composicao offline e os codecs FFmpeg compartilhados.
//
//  O motor faz tudo que é igual em todo aparelho: percorre a timeline no
//  tempo de SAIDA, usa uma instancia isolada do compositor existente, converte
//  para Y'CbCr 4:2:0 na GPU e mixa o audio. FFmpeg recebe planos e PCM:
//  libavcodec codifica, libswscale/libswresample convertem e libavformat
//  multiplexa. Android e iOS registram a mesma fabrica nativa.
//
//  Tempo: tudo em microssegundos a partir do zero do vídeo; o motor já manda
//  os carimbos monotônicos e exatos (quadro i → i·10⁶/fps).
// =============================================================================
#pragma once

#include <memory>
#include <atomic>

#include "aurea/core/Result.hpp"
#include "aurea/core/Types.hpp"
#include "aurea/export/ExportOutputValidation.hpp"
#include "aurea/export/ExportDiagnostics.hpp"

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
    /// Controle de taxa pedido: 0 = CBR, 1 = VBR (com pico limitado). Nunca
    /// qualidade constante: CQ ignora `bitrateBps` e o arquivo explode.
    u32 rateMode = 1;
    ExportColorTags color{};
    /// Perfil (ExportWatchdog.hpp): 0 = o padrão do encoder, 1 = Baseline (modo
    /// de segurança — sem B-quadros, o que todo encoder de fabricante faz).
    u32 profile = 0;
    /// Abrir diretamente o codec FFmpeg por software, preservando formato,
    /// resolução e relógio do fluxo solicitado.
    bool preferSoftware = false;
    /// O motor vai entregar, junto de cada quadro NV12, o mesmo quadro em
    /// R'G'B' num AHardwareBuffer (set_direct_surface_frame): a escada do
    /// Android tenta o degrau surface-direct antes do surface.
    bool directSurface = false;
    /// finish closes the temporary file; independent validation must
    /// succeed before the startup gate atomically replaces the destination.
    bool validateBeforePublish = false;
    /// Validação feita dentro de `finish` (sem validateBeforePublish): estrita
    /// ou tolerante (ExportOutputValidation::strict).
    bool strictValidation = true;
    /// Registro de pacotes (export/ExportPacketLog.hpp) dentro do sink. Nulo =
    /// MP4 direto. O motor liga com EngineConfig::enableResumableExport
    /// (export/ExportCheckpoint.hpp decide o caminho e a retomada).
    const char* packetLogPath = nullptr;
    /// Retomada pelo registro do sink: quadros 0..resumeFrame−1 já gravados.
    u32 resumeFrame = 0;
    /// Identidade gravada no registro (um registro de outro export nunca continua).
    u64 sessionKey = 0;
    /// Quadros do export inteiro (vai para o registro).
    u32 totalFrames = 0;
};

struct AudioStreamConfig {
    u32 sampleRate = 48000;
    u32 channels = 2;
    u32 bitrateBps = 192000;
};

class ExportSink {
public:
    virtual ~ExportSink() = default;

    /// Optional session-owned cancellation flag. Native backpressure waits
    /// poll it; it remains alive until the sink is destroyed. This permits
    /// slower software encoders without blocking cancellation/suspension.
    virtual void set_cancel_flag(const std::atomic<bool>* flag) noexcept { (void)flag; }

    /// Batida de vida (export/ExportWatchdog.hpp): o sink grava o relógio
    /// monotônico (ns) a cada chamada da plataforma que RETORNA — inclusive nos
    /// laços de espera. Assim o motor distingue "esperando o encoder num laço
    /// com prazo" de "preso dentro do encoder do aparelho" e desiste do segundo
    /// em vez de esperar para sempre. Vive até o sink ser destruído.
    virtual void set_heartbeat(std::atomic<u64>* beatNs) noexcept { (void)beatNs; }

    virtual void set_diagnostics(std::shared_ptr<ExportDiagnostics> diagnostics) noexcept { (void)diagnostics; }

    /// surface-direct: o AHardwareBuffer com o R'G'B' do PRÓXIMO write_video
    /// (nulo = não há). Pertence ao motor; vale até a GPU reusar o slot, ou
    /// seja, só durante essa chamada.
    virtual void set_direct_surface_frame(void* nativeBuffer) noexcept { (void)nativeBuffer; }
    /// surface-direct passou no 1o pacote: o motor pode parar de gerar os
    /// planos NV12 e o readback. Chamado na thread do encoder, depois de um
    /// write_video.
    [[nodiscard]] virtual bool direct_surface_confirmed() const noexcept { return false; }

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

    /// Registro de pacotes do sink (só com `packetLogPath`): EOS do vídeo e
    /// registro fechado; depois `begin_assembly`, o áudio e `finish`.
    [[nodiscard]] virtual Status end_video() noexcept { return Status{Errc::NotSupported, "sink sem registro de pacotes"}; }
    [[nodiscard]] virtual Status begin_assembly(const AudioStreamConfig* audio) noexcept {
        (void)audio;
        return Status{Errc::NotSupported, "sink sem registro de pacotes"};
    }
    [[nodiscard]] virtual Status replay_spilled_audio() noexcept {
        return Status{Errc::NotSupported, "sink sem registro de pacotes"};
    }
    /// Montagem do MP4 a partir do registro: quadros já copiados para o
    /// arquivo final (o motor mostra "montando n/N"; a tela não confunde a
    /// montagem de um vídeo longo com travamento). Vive até o sink ser destruído.
    virtual void set_assembly_counter(std::atomic<u64>* frames) noexcept { (void)frames; }
    /// Registro de pacotes: o último keyframe que esta sessão gravou no
    /// registro — de onde a próxima sessão continua (o GOP que ele abre pode
    /// estar incompleto e é renderizado de novo). Sem registro, 0. O motor lê
    /// no fim da sessão, antes de abortar, sem varrer o arquivo inteiro.
    [[nodiscard]] virtual u32 checkpoint_frame() const noexcept { return 0; }
    /// Fecha (fsync) o registro e o PCM do render AGORA, na thread do worker,
    /// antes do abort: o motor decide em seguida apagar ou guardar o checkpoint
    /// e o arquivo não pode continuar aberto por um encerramento nativo tardio.
    virtual void close_checkpoint() noexcept {}

    [[nodiscard]] virtual Status validate_output(const ExportOutputValidation&) noexcept {
        return Status{Errc::NotSupported, "plataforma sem validacao independente de exportacao"};
    }
    /// Avisos da última validação tolerante ("" = nenhum).
    [[nodiscard]] virtual const char* validation_warnings() const noexcept { return ""; }

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
