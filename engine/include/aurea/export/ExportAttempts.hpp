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
//  0.4.6: os degraus de SOFTWARE (2, 4, 5, 6) também decodificam a mídia de
//  origem no decoder compatível (ExportSettings::softwareDecode: software no
//  Android, planos na CPU no iOS). A Export BETA 2 fazia isso e a decisão se
//  perdeu: o campo continuou existindo, mas nada o ligava, e a escada só
//  trocava encoder, tamanho e taxa. Um vídeo que o decoder do aparelho nunca
//  entrega (Galaxy A23: projeto de 2 clipes parava sempre em ~50%, na
//  transição) falhava igual em TODO degrau e o export recomeçava do zero até
//  a escada acabar.
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
#include <array>
#include <cmath>
#include <mutex>

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
    /// 0.3.6: tamanho no formato que todo encoder aceita (lado maior múltiplo
    /// de 16, menor par — export_frame_size), em vez do nativo exato.
    bool align16 = false;
    /// 0.4.6: a mídia de origem no decoder compatível (ExportSettings::softwareDecode).
    bool softwareDecode = false;
};

[[nodiscard]] constexpr ExportAttemptPlan export_attempt_plan(u32 attempt) noexcept {
    switch (attempt) {
        case 0: return {0, false, 0, 0.0, false, 0, 0, false, false};
        case 1: return {1, true, 0, 60.0, false, 32, 0, true, false};
        case 2: return {2, true, 0, 60.0, false, 16, 1, true, true};
        case 3: return {1, true, 720, 30.0, true, 16, 1, true, false};
        case 4: return {2, true, 720, 30.0, true, 8, 2, true, true};
        case 5: return {2, true, 480, 30.0, true, 4, 3, true, true};
        default: return {2, true, 480, 30.0, true, 1, 4, true, true};
    }
}

/// 0.3.6 ("erro de codec" no export): o tamanho do vídeo de um degrau.
/// Degrau 0 mantém o nativo exato quando ele é par (export_frame_size_v2).
/// Composição com lado ÍMPAR (vídeo recortado, tela gravada, tamanho livre)
/// não tem tamanho nativo 4:2:0: antes o motor recusava ("dimensões pares"),
/// a escada lia isso como falta de memória e pulava direto para 720p. Agora
/// ela sai no tamanho alinhado mais próximo (lado maior múltiplo de 16, menor
/// par), como os degraus de compatibilidade (`align16`) sempre fazem — um
/// passo de 16 que todo encoder de aparelho aceita.
[[nodiscard]] inline ExportFrameSize export_attempt_frame_size(u32 compW, u32 compH, u32 wantShort,
                                                               bool align16) noexcept {
    // 0.4.5: só um valor ABSURDO vindo da ponte (acima do teto do sink, como
    // u32 máximo — ímpar OU par) é inválido: ele alinhava para um tamanho
    // gigante e o export abria o encoder. Vem ANTES do nativo: o v2 escala um
    // par absurdo sem teto. A 0.4.0 recusava todo lado menor pedido ímpar: a
    // tela mostrava "1079p · 1920 × 1080" (VideoExportRules.frameSize / sizeFor
    // alinham o ímpar) e o motor devolvia "dimensões inválidas" nos degraus
    // 0..2; a escada só exportava ao chegar em 720p. Agora o pedido ímpar
    // plausível sai no tamanho alinhado (par mais próximo), como as telas mostram.
    if (wantShort > kExportMaxFrameSide) return {};
    if (!align16) {
        const ExportFrameSize native = export_frame_size_v2(compW, compH, wantShort);
        if (native.width && native.height) return native;
    }
    return export_frame_size(compW, compH, wantShort);
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
        // 0.4.5: "codec/formato que o aparelho não exporta" (o sink recusou ao
        // abrir: NotSupported/UnsupportedCodec/UnsupportedFormat) segue a
        // escada inteira, na ordem pedida: HEVC → H.264, aparelho → software e
        // SÓ DEPOIS menos pixels. Como Memory, um HEVC 1080p sem encoder HEVC
        // pulava direto para 720p, sem tentar o H.264 no mesmo tamanho.
        case ExportFailure::Encoder:
        case ExportFailure::Unsupported: return ExportRetryKind::Encoder;
        case ExportFailure::EncoderStalled: return ExportRetryKind::Stall;
        case ExportFailure::Render:
        case ExportFailure::GpuMemory: return ExportRetryKind::Memory;
        case ExportFailure::Media: return code == static_cast<i32>(Errc::MediaSourceMissing) ? ExportRetryKind::None
                                                                                            : ExportRetryKind::Stall;
        case ExportFailure::File:
        case ExportFailure::Storage:
        case ExportFailure::RenderHung: return ExportRetryKind::None;
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
    bool softwareDecode = false;   ///< mídia no decoder compatível
    /// `withBlur` falso: o teto de motion blur, o custo 3D e o decoder da mídia
    /// não contam (falha de encoder, que nada disso resolve).
    [[nodiscard]] constexpr bool same_as(const ExportAttemptEffect& o, bool withBlur = true) const noexcept {
        return safeMode == o.safeMode && hevc == o.hevc && shortSide == o.shortSide && shortCapped == o.shortCapped &&
               fps == o.fps && fpsCapped == o.fpsCapped && upscale == o.upscale &&
               (!withBlur || (blurCap == o.blurCap && gpuStep == o.gpuStep && softwareDecode == o.softwareDecode));
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
    e.softwareDecode = p.softwareDecode;
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
                // Software (encoder e, 0.4.6, decoder da mídia), e menos
                // trabalho por quadro.
                if (e.safeMode >= 2 && ((e.softwareDecode && !now.softwareDecode) || e.shortCapped || e.shortSide != now.shortSide || e.fpsCapped || e.fps != now.fps ||
                                        now.safeMode < 2 || (now.upscale && !e.upscale) || e.less_blur_than(now)))
                    return next;
                break;
            default: return 0;
        }
    }
    return kind == ExportRetryKind::Memory ? lessBlur : 0;
}

// =============================================================================
//  0.5.2 (E2): a escada com MEMÓRIA. export_next_attempt é pura e não sabe
//  onde a tentativa anterior parou: uma falha determinística (o mesmo recurso
//  que nunca chega no mesmo quadro: Galaxy A23 parado no quadro 19 de 38 nos 7
//  degraus, inclusive 480p) era refeita até 7 vezes do zero, e a pessoa via o
//  progresso voltar a ~50% várias vezes antes do erro. O motor grava o fim de
//  cada tentativa (Engine.cpp: fim da sessão e partida recusada) e as duas
//  pontes (JNI e AureaEngine.mm) perguntam o próximo degrau por aqui; as
//  telas não ganham regra própria.
//
//  - Mesmo tipo de falha no MESMO ponto da timeline (até 3 quadros) em duas
//    tentativas seguidas que passaram da partida = determinístico: para e
//    mostra o motivo. Exceção: encoder ou decoder parado (Encoder/Stall) ainda
//    não tentado no software tenta UMA vez o degrau de software (encoder e
//    decoder compatíveis, 0.4.6), que é o que muda nesse caso.
//  - GpuTime (quadro acima do prazo da GPU) não entra: cada degrau tira custo
//    do MESMO quadro, que é sempre o mais pesado; parar no mesmo ponto é o
//    esperado até o custo caber. O teto de tentativas abaixo o limita.
//  - No máximo kExportRenderedAttemptsMax tentativas que renderizaram quadros
//    por pedido: cada uma custa o export até o ponto da falha.
//  - Falha ao abrir (sem quadro renderizado) segue a escada de sempre: trocar
//    de encoder é exatamente o que a escada faz por elas.
//
//  O ponto é em SEGUNDOS da timeline, não em índice: os degraus que limitam a
//  taxa (60 → 30 qps) mudam o índice do mesmo instante.
// =============================================================================

/// Histórico de um pedido: tentativas guardadas (as mais antigas saem).
inline constexpr u32 kExportAttemptHistoryMax = 8;
/// Tentativas que renderizaram quadros, por pedido, antes de a escada parar.
/// 4 cobre aparelho → Baseline → software → 720p (GpuTime anda de um em um).
inline constexpr u32 kExportRenderedAttemptsMax = 4;
/// "Mesmo ponto": até 3 quadros da saída mais lenta (a fila entre render e
/// encoder deixa o quadro entregue alguns atrás do que falhou).
inline constexpr u32 kExportSamePointFrames = 3;

/// O fim de uma tentativa de vídeo, gravado pelo motor.
struct ExportAttemptRecord {
    u32 attempt = 0;            ///< degrau (ExportSettings::safeMode)
    u64 request = 0;            ///< identidade do pedido: hash do arquivo de saída (o mesmo em todos os degraus)
    i32 code = 0;               ///< Errc do resultado
    u32 failure = 0;            ///< ExportFailure (0 = falha antes de existir motivo)
    bool started = false;       ///< passou da partida: o render rodou
    u32 framesDone = 0;         ///< quadros entregues NESTA tentativa (fora os do checkpoint retomado)
    f64 failedAt = -1.0;        ///< segundos da timeline onde parou (< 0 = desconhecido)
    f64 frameSeconds = 0.0;     ///< 1/fps da saída desta tentativa (tolerância do "mesmo ponto")
    ExportRetryKind kind = ExportRetryKind::None;   ///< o tipo que a tela deu (export_retry_kind)
    bool classified = false;    ///< `kind` já veio de uma pergunta da tela
};

/// Por que a escada parou antes do fim (diagnóstico e testes).
enum class ExportLadderStop : u32 { None = 0, Deterministic = 1, RenderedCap = 2 };

/// As tentativas do pedido atual, na ordem.
struct ExportAttemptHistory {
    std::array<ExportAttemptRecord, kExportAttemptHistoryMax> items{};
    u32 count = 0;
    ExportLadderStop lastStop = ExportLadderStop::None;
    void reset() noexcept { count = 0; lastStop = ExportLadderStop::None; }
    /// Outro pedido (outro arquivo) ou um degrau abaixo do último começa um
    /// histórico novo: a escada só sobe. O MESMO degrau do mesmo pedido
    /// (continuação do checkpoint, "ocupado") substitui o último registro.
    void record(const ExportAttemptRecord& r) noexcept {
        if (count) {
            const ExportAttemptRecord& last = items[count - 1];
            if (last.request != r.request || r.attempt < last.attempt) reset();
            else if (r.attempt == last.attempt) { items[count - 1] = r; return; }
        }
        if (count == items.size()) {
            for (u32 i = 1; i < count; ++i) items[i - 1] = items[i];
            --count;
        }
        items[count++] = r;
    }
};

[[nodiscard]] inline bool export_attempt_same_point(const ExportAttemptRecord& a, const ExportAttemptRecord& b) noexcept {
    if (!(a.failedAt >= 0.0) || !(b.failedAt >= 0.0)) return false;
    const f64 frame = std::max({a.frameSeconds, b.frameSeconds, 1.0 / 240.0});
    return std::abs(a.failedAt - b.failedAt) <= kExportSamePointFrames * frame + 1e-9;
}

/// export_next_attempt com o histórico do pedido. `h` recebe o tipo da falha
/// atual (a tela sabe se ela mesma cancelou por falta de progresso; o motor não).
[[nodiscard]] inline u32 export_next_attempt_with_history(u32 attempt, ExportRetryKind kind, const ExportRequestShape& r,
                                                          ExportAttemptHistory& h) noexcept {
    h.lastStop = ExportLadderStop::None;
    // Dois degraus seguidos recusados já na partida por "ocupado" (a prévia
    // não soltou a GPU, o export anterior não saiu): outro degrau não solta a
    // GPU. As telas refazem o mesmo degrau por "ocupado" e depois pedem o
    // próximo como falha de encoder; sem isto a escada inteira esperava o
    // prazo da partida em cada degrau.
    if (h.count >= 2 && h.items[h.count - 1].attempt == attempt) {
        const ExportAttemptRecord& a = h.items[h.count - 2];
        const ExportAttemptRecord& b = h.items[h.count - 1];
        if (!a.started && !b.started && a.code == static_cast<i32>(Errc::Busy) && b.code == static_cast<i32>(Errc::Busy)) {
            h.lastStop = ExportLadderStop::Deterministic;
            return 0;
        }
    }
    const u32 next = export_next_attempt(attempt, kind, r);
    if (next == 0 || (next & kExportAttemptWait)) return next;
    // Sem registro desta tentativa (o app morreu no export anterior e a tela
    // pergunta o degrau inicial): a escada de sempre.
    if (!h.count || h.items[h.count - 1].attempt != attempt) return next;
    ExportAttemptRecord& now = h.items[h.count - 1];
    now.kind = kind;
    now.classified = true;
    u32 rendered = 0;
    for (u32 i = 0; i < h.count; ++i) rendered += h.items[i].started && h.items[i].framesDone > 0 ? 1u : 0u;
    if (rendered >= kExportRenderedAttemptsMax) { h.lastStop = ExportLadderStop::RenderedCap; return 0; }
    if (h.count < 2 || kind == ExportRetryKind::GpuTime) return next;
    const ExportAttemptRecord& before = h.items[h.count - 2];
    if (!before.classified || before.kind != kind || !before.started || !now.started ||
        !export_attempt_same_point(before, now))
        return next;
    if (kind == ExportRetryKind::Encoder || kind == ExportRetryKind::Stall) {
        const auto software = [](u32 a) noexcept {
            const ExportAttemptPlan p = export_attempt_plan(a);
            return p.safeMode >= 2 && p.softwareDecode;
        };
        bool tried = false;
        for (u32 i = 0; i < h.count; ++i) tried |= software(h.items[i].attempt);
        if (!tried) for (u32 a = attempt + 1; a <= kExportAttemptMax; ++a)
            if (software(a)) return a;
    }
    h.lastStop = ExportLadderStop::Deterministic;
    return 0;
}

/// O histórico do processo. Um motor por processo nos apps; as pontes da
/// escada são estáticas (sem o handle do motor), então o registro também é.
class ExportAttemptLedger {
public:
    [[nodiscard]] static ExportAttemptLedger& process() noexcept { static ExportAttemptLedger ledger; return ledger; }
    void record(const ExportAttemptRecord& r) noexcept { std::lock_guard lock(mutex_); history_.record(r); }
    [[nodiscard]] u32 next(u32 attempt, ExportRetryKind kind, const ExportRequestShape& r) noexcept {
        std::lock_guard lock(mutex_);
        return export_next_attempt_with_history(attempt, kind, r, history_);
    }
    [[nodiscard]] ExportAttemptHistory snapshot() const noexcept { std::lock_guard lock(mutex_); return history_; }
    void reset() noexcept { std::lock_guard lock(mutex_); history_.reset(); }
    /// FNV-1a do caminho de saída (a identidade do pedido no histórico).
    [[nodiscard]] static u64 request_key(const char* outputPath) noexcept {
        u64 h = 1469598103934665603ull;
        if (outputPath) for (const char* c = outputPath; *c; ++c) { h ^= static_cast<unsigned char>(*c); h *= 1099511628211ull; }
        return h;
    }
private:
    mutable std::mutex mutex_;
    ExportAttemptHistory history_{};
};

} // namespace aurea
