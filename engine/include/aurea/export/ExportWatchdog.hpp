// =============================================================================
//  Aurea / export / ExportWatchdog.hpp
//
//  "A exportação para num percentual e nunca termina" (beta, vários Android).
//  O export não pode esperar para sempre por nada. Aqui ficam as regras de
//  tempo e de recuperação que o motor e as duas plataformas seguem:
//
//  1) PRAZO SEM PROGRESSO (ProgressDeadline). Toda espera do export conta o
//     tempo SEM PROGRESSO (o prazo renova a cada sinal de vida) e tem um teto
//     absoluto. Esperar não é erro; esperar sem fim é.
//
//  2) PACIÊNCIA COM A FONTE (export_source_wait). O quadro EXATO de vídeo pode
//     não vir (fim de arquivo quebrado, decoder do fabricante que morre). Antes,
//     depois do primeiro quadro aproximado a espera ainda esticava até 60 s a
//     cada quadro que OUTRO decoder entregasse algo — um quadro por minuto, o
//     export "parado" num percentual. Agora a paciência cai com a sequência de
//     quadros aproximados e o teto também cai junto.
//
//  3) WORKER TRAVADO (export_worker_hung). O encoder do aparelho pode prender a
//     thread DENTRO de uma chamada da plataforma (HAL travado: o próprio timeout
//     do MediaCodec não volta, o stop() não volta). O sink bate o coração a cada
//     chamada da plataforma que RETORNA; sem batida por `limite`, o motor desiste
//     daquele worker em vez de esperar o join para sempre.
//
//  4) MODO DE SEGURANÇA (export_retry_safe_mode e afins). Quando o encoder
//     trava ou recusa no meio, a tela refaz o export num modo mais compatível:
//     nível 1 = H.264 Baseline, lados em múltiplos de 16, bitrate preservado; nível 2 =
//     o mesmo no encoder de SOFTWARE (Android). Quem decide o próximo nível é o
//     motor (vai nos bits 16..17 de `flags` do progresso); as telas só obedecem.
//
//  5) FIM SEM EOS (export_*_complete_without_eos). Encoder que entrega todos os
//     quadros mas perde a marca de fim do fluxo não pode custar o arquivo.
//
//  Header-only (sem .cpp novo para os três sistemas de build).
// =============================================================================
#pragma once

#include <algorithm>

#include "aurea/core/Types.hpp"
#include "aurea/export/ExportRules.hpp"

namespace aurea {

// -----------------------------------------------------------------------------
// 1) Prazo sem progresso
// -----------------------------------------------------------------------------
/// Prazo que renova a cada progresso, nunca além do teto absoluto.
class ProgressDeadline {
public:
    ProgressDeadline() noexcept = default;
    /// `hardCapNs` 0 = sem teto.
    ProgressDeadline(u64 nowNs, u64 patienceNs, u64 hardCapNs) noexcept
        : patience_(patienceNs), hard_(hardCapNs ? nowNs + hardCapNs : kNever) {
        deadline_ = std::min(nowNs + patienceNs, hard_);
    }
    /// Sinal de vida: volta a contar a paciência inteira (limitada ao teto).
    void progress(u64 nowNs) noexcept { deadline_ = std::min(nowNs + patience_, hard_); }
    /// Tempo que não conta (ex.: GPU do quadro anterior): empurra prazo e teto.
    void exclude(u64 ns) noexcept {
        deadline_ += ns;
        if (hard_ != kNever) hard_ += ns;
    }
    [[nodiscard]] bool expired(u64 nowNs) const noexcept { return nowNs > deadline_; }
    [[nodiscard]] u64 deadline() const noexcept { return deadline_; }

private:
    static constexpr u64 kNever = ~0ull;
    u64 patience_ = 0;
    u64 hard_ = kNever;
    u64 deadline_ = 0;
};

// -----------------------------------------------------------------------------
// 2) Paciência com a fonte de vídeo (render_export_frame)
// -----------------------------------------------------------------------------
struct ExportSourceWait {
    u64 patienceNs = 0;   ///< sem nenhum decoder entregar nada
    u64 hardCapNs = 0;    ///< teto do quadro, mesmo com decoders entregando
};

/// Quadros aproximados SEGUIDOS a partir dos quais a paciência cai de novo.
inline constexpr u32 kExportFallbackStreak = 8;

/// 0 aproximados: 4 s sem progresso, teto de 60 s (efeito que lê vários
/// instantes com GOP longo é legítimo). Depois do 1º: 1 s / 4 s. Depois de
/// `kExportFallbackStreak` seguidos: 250 ms / 1 s — uma fonte quebrada no fim
/// do clipe custa segundos no export inteiro, não um minuto por quadro.
[[nodiscard]] inline ExportSourceWait export_source_wait(u32 consecutiveFallbacks) noexcept {
    if (consecutiveFallbacks == 0) return {4'000'000'000ull, 60'000'000'000ull};
    if (consecutiveFallbacks < kExportFallbackStreak) return {1'000'000'000ull, 4'000'000'000ull};
    return {250'000'000ull, 1'000'000'000ull};
}

/// Camada SEM NENHUMA imagem (nem aproximada): não há o que gravar — mas
/// decoder LENTO não é mídia quebrada (beta 0.0.2/0.0.3: "não consigo
/// exportar" com vídeo). A paciência conta tempo SEM TRABALHO do decoder
/// (quadro entregue OU decodificado e descartado a caminho do alvo, codec que
/// terminou de abrir) e o teto é o da GPU — nunca o teto curto dos aproximados
/// de `export_source_wait`: antes, depois de alguns quadros aproximados no fim
/// de um clipe o teto caía para 1 s e o clipe SEGUINTE (codec abrindo, seek
/// num GOP longo) falhava como "mídia indisponível".
[[nodiscard]] inline ExportSourceWait export_missing_source_wait() noexcept {
    return {4'000'000'000ull, 120'000'000'000ull};
}

/// Recurso de GPU do quadro pendente (Renderer::take_incomplete): espera sem
/// progresso por 4 s, renovando enquanto decoders/serviços entregam algo, com
/// teto de 30 s. Persistente sem nenhum progresso continua falhando em 4 s.
[[nodiscard]] inline ExportSourceWait export_resource_wait() noexcept {
    return {4'000'000'000ull, 30'000'000'000ull};
}

/// Sinal de vida da UI enquanto o produtor espera o decoder trabalhar: a
/// mensagem do progresso muda (no máximo 1×/s) SÓ quando houve trabalho real —
/// o vigia da tela (kExportUiStallSeconds) não cancela um decoder lento, e
/// continua vendo o travamento de verdade (nada muda).
inline constexpr u64 kExportLivenessIntervalNs = 1'000'000'000ull;
[[nodiscard]] inline bool export_liveness_due(u64 nowNs, u64 lastNoteNs, u64 work, u64 notedWork) noexcept {
    return work != notedWork && nowNs >= lastNoteNs + kExportLivenessIntervalNs;
}

// -----------------------------------------------------------------------------
// 3) Worker do encoder travado
// -----------------------------------------------------------------------------
/// Onde o worker do encoder está (o motivo, se travar).
enum class ExportWorkerPhase : u32 {
    Idle = 0,
    Video = 1,      ///< sink.write_video
    AudioMix = 2,   ///< decodificar + mixar o áudio (decoder da plataforma)
    AudioWrite = 3, ///< sink.write_audio
    Upscale = 4,    ///< IA (CPU; bate a cada bloco)
    Finish = 5,     ///< sink.finish (EOS, esvaziar, fechar o MP4)
    Abort = 6,      ///< sink.abort (soltar o encoder)
};

/// Sem batida por isto, o worker está preso dentro da plataforma. Os laços dos
/// sinks voltam em milissegundos (dequeue de 2 a 10 ms) e têm prazo próprio de
/// 10 s (hardware) / 30 s (software); uma única chamada que segura 45 s não
/// volta mais.
inline constexpr u64 kExportWorkerHangNs = 45'000'000'000ull;
/// Soltar o encoder (abort) não codifica nada: 10 s bastam.
inline constexpr u64 kExportWorkerAbortHangNs = 10'000'000'000ull;
/// Cancelado, todo laço de sink volta em milissegundos (eles olham o
/// cancelamento a cada volta): 3 s sem batida = preso na plataforma.
inline constexpr u64 kExportWorkerCancelHangNs = 3'000'000'000ull;

[[nodiscard]] inline u64 export_worker_hang_limit_ns(ExportWorkerPhase phase, u64 baseNs, bool cancelled = false) noexcept {
    if (baseNs == 0) baseNs = kExportWorkerHangNs;
    if (cancelled) baseNs = std::min(baseNs, kExportWorkerCancelHangNs);
    return phase == ExportWorkerPhase::Abort ? std::min(baseNs, kExportWorkerAbortHangNs) : baseNs;
}

/// `busy` = o worker está fora da fila (dentro do sink, do mix ou da IA).
/// Parado na fila esperando quadro não é travar: é o produtor que está lento.
[[nodiscard]] inline bool export_worker_hung(bool busy, u64 nowNs, u64 lastBeatNs, u64 limitNs) noexcept {
    return busy && limitNs > 0 && nowNs > lastBeatNs && nowNs - lastBeatNs > limitNs;
}

/// O motivo de um worker abandonado: travar no mix é a MÍDIA (decoder de
/// áudio); no resto, o encoder.
[[nodiscard]] inline Errc export_worker_hang_code(ExportWorkerPhase phase) noexcept {
    return phase == ExportWorkerPhase::AudioMix ? Errc::DecodeFailed : Errc::Timeout;
}

/// A tela é a última rede: sem nenhuma mudança no progresso por isto, ela
/// cancela; sem conclusão `kExportUiGiveUpSeconds` depois, desiste e libera a
/// tela. Acima de todos os prazos do motor (GPU 120 s, quadro 60 s, worker 45 s).
/// Exporter.kt e AureaModel.swift repetem os dois números.
inline constexpr u32 kExportUiStallSeconds = 180;
inline constexpr u32 kExportUiGiveUpSeconds = 15;

// -----------------------------------------------------------------------------
// 4) Modo de segurança
// -----------------------------------------------------------------------------
inline constexpr u32 kExportSafeModeMax = 2;
/// Próximo nível sugerido nos bits 16..17 de `flags` do progresso (0 = não
/// refazer). Abaixo dos bits do motivo (24..31, kExportFailureShift).
inline constexpr u32 kExportRetryShift = 16;
inline constexpr u32 kExportRetryMask = 0x3u << kExportRetryShift;

/// Encoder travou ou recusou no meio: refazer no próximo nível (até o 2).
/// Qualquer outro motivo (mídia, GPU, disco, cancelamento) não muda refazendo.
[[nodiscard]] inline u32 export_retry_safe_mode(ExportFailure failure, u32 currentLevel) noexcept {
    if (failure != ExportFailure::EncoderStalled && failure != ExportFailure::Encoder) return 0;
    return currentLevel < kExportSafeModeMax ? currentLevel + 1 : 0;
}

[[nodiscard]] inline u32 export_retry_from_flags(u32 flags) noexcept {
    return (flags & kExportRetryMask) >> kExportRetryShift;
}

/// Perfil pedido ao encoder (VideoStreamConfig::profile).
inline constexpr u32 kExportProfileDefault = 0;   ///< High (H.264) / Main (HEVC), o padrão do encoder
inline constexpr u32 kExportProfileBaseline = 1;  ///< sem B-quadros nem CABAC: o que todo encoder faz

/// No modo de segurança é sempre H.264: o HEVC dos aparelhos de entrada é o
/// encoder que mais trava.
[[nodiscard]] inline ExportCodec export_safe_codec(ExportCodec requested, u32 level) noexcept {
    return level > 0 ? ExportCodec::H264 : requested;
}

/// Compatibility retries change the encoder/profile, preserving the requested
/// bitrate. A stalled device must not silently turn a High export into Low.
[[nodiscard]] inline u32 export_safe_bitrate_bps(u32 bps, u32 level) noexcept {
    (void)level;
    return bps;
}

/// Tamanho no modo de segurança: a regra de sempre e depois OS DOIS lados para
/// baixo em múltiplo de 16 (1080 → 1072: nada passa do teto do aparelho e a
/// proporção muda menos de 1%). Encoder de fabricante que trava com altura que
/// não é múltipla do macrobloco recebe o tamanho que todos testam.
[[nodiscard]] inline ExportFrameSize export_safe_frame_size(u32 compW, u32 compH, u32 wantShort) noexcept {
    ExportFrameSize s = export_frame_size(compW, compH, wantShort);
    if (s.width == 0 || s.height == 0) return s;
    s.width = std::max<u32>(16, s.width & ~15u);
    s.height = std::max<u32>(16, s.height & ~15u);
    return s;
}

// -----------------------------------------------------------------------------
// 5) Fim do fluxo sem a marca de EOS
// -----------------------------------------------------------------------------
/// Vídeo: todo quadro entregue já saiu do encoder (um pacote por quadro).
[[nodiscard]] inline bool export_video_complete_without_eos(u64 queuedFrames, u64 producedPackets) noexcept {
    return queuedFrames > 0 && producedPackets >= queuedFrames;
}

/// Áudio: a última saída chegou a menos de 250 ms do último PCM entregue (o
/// encoder AAC segura no máximo um bloco de 1024 amostras sem o EOS).
[[nodiscard]] inline bool export_audio_complete_without_eos(i64 lastInputPtsUs, i64 lastOutputPtsUs) noexcept {
    if (lastInputPtsUs < 0) return true;    // nenhum PCM entrou
    return lastOutputPtsUs + 250'000 >= lastInputPtsUs;
}

} // namespace aurea
