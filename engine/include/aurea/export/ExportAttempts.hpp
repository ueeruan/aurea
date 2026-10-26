// =============================================================================
//  Aurea / export / ExportAttempts.hpp
//
//  A ESCADA DE TENTATIVAS do export de vídeo (0.2.6). Exportar não pode
//  terminar em "erro N" quando outro jeito de exportar funcionaria: a tela
//  refaz sozinha, cada vez num degrau mais compatível, e só mostra falha
//  quando nada ajuda (cancelado, sem espaço, mídia que sumiu, duração acima
//  do teto) ou quando a escada acabou.
//
//    0  o pedido
//    1  H.264 Baseline no encoder do aparelho (HEVC vira H.264), até 60 qps
//    2  H.264 no encoder de software, até 60 qps
//    3  H.264 Baseline no aparelho, lado menor até 720, até 30 qps, sem upscale
//    4  o mesmo no encoder de software
//    5  software, lado menor até 480, até 30 qps, sem upscale
//    6  o mesmo, sem motion blur (0.2.7)
//
//  0.3.0: os degraus 1 e 2 também limitam a 60 qps (só muda o pedido acima
//  disso). A 200 qps nenhum encoder de hardware se anuncia e o pedido ia para o
//  software; o primeiro degrau que baixava a taxa era o 3, junto com 720p.
//
//  0.2.7: cada degrau também tem um TETO DE AMOSTRAS de motion blur (1: 32,
//  2 e 3: 16, 4: 8, 5: 4, 6: nenhuma). No 3D cada amostra é uma cena inteira
//  renderizada de novo; sem este teto todos os degraus falhavam igual
//  (lado menor e codec não mudam o custo de 128 cenas por quadro).
//
//  0.3.2: cada degrau também tem um DEGRAU DE CUSTO 3D fixo (sombra/MSAA,
//  Renderer::set_export_gpu_step; 2: 1, 3: 1, 4: 2, 5: 3, 6: 4). Antes esse
//  corte subia no meio do export quando um quadro passava de 1,5 s, e o mesmo
//  projeto saía diferente conforme a carga do aparelho. Agora vale do primeiro
//  ao último quadro da tentativa, e a tela avisa o degrau.
//
//  O degrau viaja no lugar do antigo "modo de segurança" (ExportSettings::
//  safeMode, JNI e ponte do iOS): 0, 1 e 2 continuam significando o mesmo
//  encoder de antes. O motor aplica o degrau ao iniciar (Engine.cpp,
//  start_export_checked); as duas telas só perguntam aqui qual é o próximo.
//  Funções puras: os testes do host conferem a escada inteira.
// =============================================================================
#pragma once

#include "aurea/core/Result.hpp"
#include "aurea/core/Types.hpp"
#include "aurea/export/ExportRules.hpp"

#include <algorithm>

namespace aurea {

/// Último degrau da escada.
inline constexpr u32 kExportAttemptMax = 6;
/// Bit da resposta de export_next_attempt: refazer o MESMO degrau depois de
/// esperar um pouco (o motor ainda solta o export anterior, a GPU estava
/// ocupada). As telas esperam ~1,5 s e fazem isso no máximo duas vezes.
inline constexpr u32 kExportAttemptWait = 0x100;
/// Quantas vezes as telas refazem o mesmo degrau por "ocupado".
inline constexpr u32 kExportAttemptBusyRetries = 2;

/// O que um degrau muda no pedido.
struct ExportAttemptPlan {
    u32 safeMode = 0;        ///< 0 pedido, 1 Baseline no aparelho, 2 software (ExportWatchdog.hpp)
    bool forceH264 = false;  ///< HEVC vira H.264
    u32 shortSideCap = 0;    ///< teto do lado menor (0 = o pedido)
    f64 fpsCap = 0.0;        ///< teto da taxa (0 = a pedida)
    bool noUpscale = false;  ///< sem upscale na GPU
    u32 blurCap = 0;         ///< teto de amostras de motion blur por quadro (0 = o do projeto; 1 = sem desfoque)
    u32 gpuStep = 0;         ///< degrau de custo 3D (0 = cheio; Renderer::set_export_gpu_step)
};

[[nodiscard]] constexpr ExportAttemptPlan export_attempt_plan(u32 attempt) noexcept {
    switch (attempt) {
        case 0: return {0, false, 0, 0.0, false, 0, 0};
        case 1: return {1, true, 0, 60.0, false, 32, 0};
        case 2: return {2, true, 0, 60.0, false, 16, 1};
        case 3: return {1, true, 720, 30.0, true, 16, 1};
        case 4: return {2, true, 720, 30.0, true, 8, 2};
        case 5: return {2, true, 480, 30.0, true, 4, 3};
        default: return {2, true, 480, 30.0, true, 1, 4};
    }
}

/// O lado menor que o degrau usa (`requested` 0 = o da composição, desconhecido).
[[nodiscard]] constexpr u32 export_attempt_short_side(u32 attempt, u32 requested) noexcept {
    const u32 cap = export_attempt_plan(attempt).shortSideCap;
    if (cap == 0) return requested;
    return requested == 0 ? cap : std::min(requested, cap);
}

/// Que tipo de ajuda uma falha aceita.
enum class ExportRetryKind : u32 {
    None = 0,    ///< nada ajuda: cancelado, sem espaço, mídia ausente, projeto inválido, duração acima do teto
    Busy = 1,    ///< o motor ainda está soltando algo: esperar e refazer igual
    Encoder = 2, ///< o encoder recusou/quebrou: o próximo degrau
    Memory = 3,  ///< memória/GPU/tamanho: um degrau com MENOS pixels
    Stall = 4,   ///< parou de andar (encoder, decoder ou a tela desistiu): software e menor
    GpuTime = 5, ///< a GPU estourou o prazo num quadro (0.2.7): menos amostras de motion blur, depois menos pixels
};

/// O tipo da falha a partir do que as telas recebem: o código (start ou
/// `result` do progresso), o motivo do progresso (ExportFailure, 0 = sem
/// motivo, falha no início), se a TELA cancelou por falta de progresso, e se a
/// duração passou do teto (aí nenhum degrau ajuda).
[[nodiscard]] constexpr ExportRetryKind export_retry_kind(i32 code, u32 failure, bool stalled, bool tooLong) noexcept {
    if (tooLong) return ExportRetryKind::None;
    if (stalled) return ExportRetryKind::Stall;
    // Quadro que passou do prazo da GPU (motion blur 3D: dezenas de cenas
    // inteiras por quadro): o que ajuda é menos amostras, não outro encoder.
    if (static_cast<ExportFailure>(failure) == ExportFailure::Render && code == static_cast<i32>(Errc::Timeout))
        return ExportRetryKind::GpuTime;
    // 0.3.2: o encoder pediu para refazer igual (Busy): a Surface do hardware
    // parou e o processo já pula esse caminho — o mesmo degrau, pelo buffer.
    if (code == static_cast<i32>(Errc::Busy) && (static_cast<ExportFailure>(failure) == ExportFailure::Encoder ||
                                                 static_cast<ExportFailure>(failure) == ExportFailure::EncoderStalled))
        return ExportRetryKind::Busy;
    switch (static_cast<ExportFailure>(failure)) {
        case ExportFailure::Encoder: return ExportRetryKind::Encoder;
        case ExportFailure::EncoderStalled: return ExportRetryKind::Stall;
        case ExportFailure::Render:
        case ExportFailure::GpuMemory:
        case ExportFailure::Unsupported: return ExportRetryKind::Memory;
        case ExportFailure::Media: return code == static_cast<i32>(Errc::MediaSourceMissing) ? ExportRetryKind::None
                                                                                            : ExportRetryKind::Stall;
        case ExportFailure::File:
        case ExportFailure::Storage: return ExportRetryKind::None;
        default: break;
    }
    switch (static_cast<Errc>(code)) {
        case Errc::Ok:
        case Errc::Cancelled:
        case Errc::StorageFull:
        case Errc::MediaSourceMissing:
        case Errc::NotFound:
        case Errc::ProjectCorrupted:
        case Errc::InvalidArgument:
        case Errc::IoError:
        case Errc::ShuttingDown: return ExportRetryKind::None;
        case Errc::InvalidState:
        case Errc::Busy:
        case Errc::Timeout: return ExportRetryKind::Busy;
        case Errc::OutOfMemory:
        case Errc::BudgetExceeded:
        case Errc::OutOfDeviceMemory:
        case Errc::DeviceLost:
        case Errc::SurfaceLost:
        case Errc::PipelineCompileFailed:
        case Errc::ShaderCompileFailed:
        case Errc::NotSupported:
        case Errc::UnsupportedFeature: return ExportRetryKind::Memory;
        case Errc::DecodeFailed:
        case Errc::CodecHung: return ExportRetryKind::Stall;
        default: return ExportRetryKind::Encoder;   // EncoderUnavailable, EncodeFailed, codec/formato e o resto
    }
}

/// O que o pedido é (para pular degraus que não mudariam nada).
struct ExportRequestShape {
    u32 shortSide = 0;   ///< 0 = o da composição (desconhecido aqui)
    f64 fps = 0.0;       ///< 0 = o da composição (desconhecido aqui)
    bool hevc = false;
    bool upscale = false;
};

/// O pedido efetivo de um degrau, para comparar dois degraus.
struct ExportAttemptEffect {
    u32 safeMode = 0;
    bool hevc = false;
    u32 shortSide = 0;   ///< 0 = desconhecido
    bool shortCapped = false;
    f64 fps = 0.0;
    bool fpsCapped = false;
    bool upscale = false;
    u32 blurCap = 0;     ///< 0 = o do projeto
    u32 gpuStep = 0;     ///< degrau de custo 3D
    /// `withBlur` falso: o teto de motion blur e o custo 3D não contam (falha
    /// de encoder, que menos trabalho de GPU não resolve).
    [[nodiscard]] constexpr bool same_as(const ExportAttemptEffect& o, bool withBlur = true) const noexcept {
        return safeMode == o.safeMode && hevc == o.hevc && shortSide == o.shortSide && shortCapped == o.shortCapped &&
               fps == o.fps && fpsCapped == o.fpsCapped && upscale == o.upscale &&
               (!withBlur || (blurCap == o.blurCap && gpuStep == o.gpuStep));
    }
    /// Menos trabalho de GPU por quadro que `o`: menos amostras de motion blur
    /// ou um degrau de custo 3D acima.
    [[nodiscard]] constexpr bool less_blur_than(const ExportAttemptEffect& o) const noexcept {
        return (blurCap != 0 && (o.blurCap == 0 || blurCap < o.blurCap)) || gpuStep > o.gpuStep;
    }
};

[[nodiscard]] constexpr ExportAttemptEffect export_attempt_effect(u32 attempt, const ExportRequestShape& r) noexcept {
    const ExportAttemptPlan p = export_attempt_plan(attempt);
    ExportAttemptEffect e;
    e.safeMode = p.safeMode;
    e.hevc = r.hevc && !p.forceH264;
    // Lado/taxa desconhecidos (0): o teto pode mudar algo, então conta como mudança.
    if (p.shortSideCap == 0) e.shortSide = r.shortSide;
    else if (r.shortSide == 0) { e.shortSide = p.shortSideCap; e.shortCapped = true; }
    else e.shortSide = std::min(r.shortSide, p.shortSideCap);
    if (p.fpsCap <= 0.0) e.fps = r.fps;
    else if (r.fps <= 0.0) { e.fps = p.fpsCap; e.fpsCapped = true; }
    else e.fps = std::min(r.fps, p.fpsCap);
    e.upscale = r.upscale && !p.noUpscale;
    e.blurCap = p.blurCap;
    e.gpuStep = p.gpuStep;
    return e;
}

/// O próximo degrau depois de uma falha no degrau `attempt`. 0 = nada mais
/// ajuda (a tela mostra o motivo). `attempt | kExportAttemptWait` = esperar e
/// refazer o mesmo (Busy). Degraus que não mudariam nada são pulados.
[[nodiscard]] constexpr u32 export_next_attempt(u32 attempt, ExportRetryKind kind, const ExportRequestShape& r) noexcept {
    if (kind == ExportRetryKind::None) return 0;
    if (kind == ExportRetryKind::Busy) return attempt | kExportAttemptWait;
    if (attempt >= kExportAttemptMax) return 0;
    const ExportAttemptEffect now = export_attempt_effect(attempt, r);
    u32 lessBlur = 0;   // Memory: só amostras a menos, se nenhum degrau tirar pixels
    for (u32 next = attempt + 1; next <= kExportAttemptMax; ++next) {
        const ExportAttemptEffect e = export_attempt_effect(next, r);
        // Igual a um degrau de baixo (já tentado, ou mais exigente): pula.
        bool repeated = false;
        for (u32 k = 0; k <= attempt && !repeated; ++k)
            repeated = e.same_as(export_attempt_effect(k, r), kind != ExportRetryKind::Encoder);
        if (repeated) continue;
        const bool fewer = e.shortCapped || (now.shortSide == 0 ? e.shortSide != 0 : e.shortSide < now.shortSide) ||
                           (now.upscale && !e.upscale);
        switch (kind) {
            case ExportRetryKind::Encoder:
                return next;
            case ExportRetryKind::Memory: {
                // Menos pixels (ou sem upscale): trocar só o encoder não ajuda.
                if (fewer) return next;
                if (!lessBlur && e.less_blur_than(now)) lessBlur = next;
                break;
            }
            case ExportRetryKind::GpuTime:
                // Menos amostras de motion blur primeiro (mantém a resolução).
                if (e.less_blur_than(now) || fewer) return next;
                break;
            case ExportRetryKind::Stall:
                // Software, e menos trabalho por quadro.
                if (e.safeMode >= 2 && (e.shortCapped || e.shortSide != now.shortSide || e.fpsCapped || e.fps != now.fps ||
                                        now.safeMode < 2 || (now.upscale && !e.upscale) || e.less_blur_than(now)))
                    return next;
                break;
            default: return 0;
        }
    }
    return kind == ExportRetryKind::Memory ? lessBlur : 0;
}

} // namespace aurea
