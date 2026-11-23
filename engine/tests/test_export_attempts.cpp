// =============================================================================
//  ESCADA DE TENTATIVAS DO EXPORT (0.2.6, export/ExportAttempts.hpp): exportar
//  não termina em "erro N" quando outro jeito funcionaria. Também os limites
//  do export de 32 bits (AddressSpace.hpp) e o encoder pelo nome no Android 8/9.
// =============================================================================
#include "TestFramework.hpp"

#include "aurea/export/ExportAttempts.hpp"
#include "aurea/platform/AddressSpace.hpp"
#include "../src/export/AndroidCodecName.hpp"

#include <cmath>
#include <cstdio>

using namespace aurea;

namespace {

ExportRequestShape shape(u32 shortSide, f64 fps, bool hevc = false, bool upscale = false) {
    ExportRequestShape r;
    r.shortSide = shortSide;
    r.fps = fps;
    r.hevc = hevc;
    r.upscale = upscale;
    return r;
}

i32 code(Errc e) { return static_cast<i32>(e); }
u32 why(ExportFailure f) { return static_cast<u32>(f); }

} // namespace

AUREA_TEST(ExportAttempts, PlansPreserveEveryRequestedQualitySetting) {
    for (u32 attempt : {0u, 1u, 2u, 3u, 4u, 5u, 6u, 99u}) {
        const auto plan = export_attempt_plan(attempt);
        AUREA_CHECK(!plan.forceH264 && !plan.noUpscale && !plan.align16);
        AUREA_CHECK_EQ(plan.shortSideCap, 0u);
        AUREA_CHECK_EQ(plan.fpsCap, 0.0);
        AUREA_CHECK_EQ(plan.blurCap, 0u);
        AUREA_CHECK_EQ(plan.gpuStep, 0u);
        AUREA_CHECK_EQ(export_attempt_short_side(attempt, 2160), 2160u);
        AUREA_CHECK_EQ(export_attempt_short_side(attempt, 0), 0u);
    }
    AUREA_CHECK_EQ(export_attempt_plan(0).safeMode, 0u);
    AUREA_CHECK_EQ(export_attempt_plan(1).safeMode, 1u);
    AUREA_CHECK_EQ(export_attempt_plan(2).safeMode, 2u);
}

// 0.3.6 ("erro de codec"): composição de lado ímpar não recusa mais, e todo
// degrau de compatibilidade sai no tamanho que encoder de aparelho aceita
// (lado maior múltiplo de 16, menor par). A cadeia completa de um encoder que
// recusa: 0 pedido → 1 H.264 Baseline no aparelho, ≤60 qps, alinhado a 16 →
// 2 software → 3/4 720p 30 qps → 5 480p.
AUREA_TEST(ExportAttempts, OddAndCompatibilitySizesAreAlignedForTheEncoder) {
    AUREA_CHECK(!export_attempt_plan(0).align16);
    for (u32 a = 1; a <= kExportAttemptMax; ++a) AUREA_CHECK(!export_attempt_plan(a).align16);
    // Degrau 0: nativo exato quando par.
    auto s = export_attempt_frame_size(1920, 1080, 0, false);
    AUREA_CHECK_EQ(s.width, 1920u); AUREA_CHECK_EQ(s.height, 1080u);
    s = export_attempt_frame_size(1920, 1080, 480, false);
    AUREA_CHECK_EQ(s.width, 854u); AUREA_CHECK_EQ(s.height, 480u);
    // Lado ímpar: alinhado, nunca 0 (antes: "dimensoes pares" e a escada pulava para 720p).
    s = export_attempt_frame_size(1919, 1079, 0, false);
    AUREA_CHECK_EQ(s.width, 1920u); AUREA_CHECK_EQ(s.height, 1080u);
    s = export_attempt_frame_size(853, 480, 0, false);
    AUREA_CHECK_EQ(s.width, 848u); AUREA_CHECK_EQ(s.height, 480u);
    s = export_attempt_frame_size(1079, 1920, 0, false);
    AUREA_CHECK_EQ(s.width, 1080u); AUREA_CHECK_EQ(s.height, 1920u);
    // Degrau compatível: o maior em múltiplo de 16 mesmo vindo de um par.
    s = export_attempt_frame_size(1920, 1080, 480, true);
    AUREA_CHECK_EQ(s.width, 848u); AUREA_CHECK_EQ(s.height, 480u);
    s = export_attempt_frame_size(1080, 1350, 0, true);
    AUREA_CHECK_EQ(s.width, 1080u); AUREA_CHECK_EQ(s.height % 16, 0u);
    for (u32 w : {640u, 853u, 1001u, 1919u, 2560u, 3839u})
        for (u32 h : {360u, 479u, 1079u, 1440u})
            for (u32 want : {0u, 480u, 720u, 1080u}) {
                const auto z = export_attempt_frame_size(w, h, want, true);
                AUREA_CHECK(z.width > 0 && z.height > 0 && z.width % 2 == 0 && z.height % 2 == 0);
                AUREA_CHECK_EQ(std::max(z.width, z.height) % 16, 0u);
                const auto n = export_attempt_frame_size(w, h, want, false);
                AUREA_CHECK(n.width > 0 && n.height > 0 && n.width % 2 == 0 && n.height % 2 == 0);
            }
    // Encoder que recusa a partida: a escada toda, em ordem, até o software menor.
    const ExportRequestShape r = shape(1080, 60.0, true);
    AUREA_CHECK_EQ(export_next_attempt(0, export_retry_kind(code(Errc::EncoderUnavailable), why(ExportFailure::Encoder), false, false), r), 1u);
    AUREA_CHECK(!export_attempt_plan(1).forceH264 && export_attempt_plan(1).safeMode == 1 && export_attempt_plan(1).fpsCap == 0.0);
    AUREA_CHECK_EQ(export_next_attempt(1, ExportRetryKind::Encoder, r), 2u);
    AUREA_CHECK_EQ(export_attempt_plan(2).safeMode, 2u);   // encoder de software
}

AUREA_TEST(ExportAttempts, WhatCanHelpIsDecidedByCodeReasonAndStall) {
    // Nada ajuda: cancelado, sem espaço, mídia que sumiu, acima de 3 h.
    AUREA_CHECK(export_retry_kind(code(Errc::Cancelled), 0, false, false) == ExportRetryKind::None);
    AUREA_CHECK(export_retry_kind(code(Errc::StorageFull), 0, false, false) == ExportRetryKind::None);
    AUREA_CHECK(export_retry_kind(code(Errc::MediaSourceMissing), 0, false, false) == ExportRetryKind::None);
    AUREA_CHECK(export_retry_kind(code(Errc::NotSupported), 0, false, true) == ExportRetryKind::None);
    AUREA_CHECK(export_retry_kind(code(Errc::IoError), why(ExportFailure::Storage), false, false) == ExportRetryKind::None);
    AUREA_CHECK(export_retry_kind(code(Errc::MediaSourceMissing), why(ExportFailure::Media), false, false) == ExportRetryKind::None);
    // Ocupado: esperar e refazer igual.
    AUREA_CHECK(export_retry_kind(code(Errc::InvalidState), 0, false, false) == ExportRetryKind::Busy);
    AUREA_CHECK(export_retry_kind(code(Errc::Timeout), 0, false, false) == ExportRetryKind::Busy);
    // Encoder.
    AUREA_CHECK(export_retry_kind(code(Errc::EncoderUnavailable), 0, false, false) == ExportRetryKind::Encoder);
    AUREA_CHECK(export_retry_kind(code(Errc::EncodeFailed), why(ExportFailure::Encoder), false, false) == ExportRetryKind::Encoder);
    // Memória / GPU / tamanho acima do aparelho.
    AUREA_CHECK(export_retry_kind(code(Errc::OutOfMemory), 0, false, false) == ExportRetryKind::Memory);
    AUREA_CHECK(export_retry_kind(code(Errc::OutOfDeviceMemory), 0, false, false) == ExportRetryKind::Memory);
    AUREA_CHECK(export_retry_kind(code(Errc::NotSupported), 0, false, false) == ExportRetryKind::Memory);
    // Dispositivo perdido: o MESMO degrau (a GPU volta fatiada por passe).
    AUREA_CHECK(export_retry_kind(code(Errc::DeviceLost), why(ExportFailure::Render), false, false) == ExportRetryKind::Busy);
    AUREA_CHECK(export_retry_kind(code(Errc::OutOfMemory), why(ExportFailure::GpuMemory), false, false) == ExportRetryKind::Memory);
    // Parou de andar: encoder travado, decoder preso ou a tela desistiu.
    AUREA_CHECK(export_retry_kind(code(Errc::Timeout), why(ExportFailure::EncoderStalled), false, false) == ExportRetryKind::Stall);
    AUREA_CHECK(export_retry_kind(code(Errc::DecodeFailed), why(ExportFailure::Media), false, false) == ExportRetryKind::Stall);
    AUREA_CHECK(export_retry_kind(code(Errc::Cancelled), 0, true, false) == ExportRetryKind::Stall);
    // Quadro que passou do prazo da GPU (motion blur 3D): menos amostras.
    AUREA_CHECK(export_retry_kind(code(Errc::Timeout), why(ExportFailure::Render), false, false) == ExportRetryKind::GpuTime);
    // 0.3.2: a Surface do encoder de hardware parou e há a entrada por buffer
    // do mesmo encoder: refazer o MESMO degrau (Busy), não descer a escada.
    AUREA_CHECK(export_retry_kind(code(Errc::Busy), why(ExportFailure::Encoder), false, false) == ExportRetryKind::Busy);
    AUREA_CHECK(export_retry_kind(code(Errc::Busy), why(ExportFailure::EncoderStalled), false, false) == ExportRetryKind::Busy);
    // A tela que desistiu continua indo para o software.
    AUREA_CHECK(export_retry_kind(code(Errc::Busy), why(ExportFailure::Encoder), true, false) == ExportRetryKind::Stall);
}

AUREA_TEST(ExportAttempts, EncoderFailuresClimbEveryRungThenStop) {
    const ExportRequestShape r = shape(1080, 30.0, true);
    u32 a = 0, steps = 0;
    std::printf("   ");
    while ((a = export_next_attempt(a, ExportRetryKind::Encoder, r)) != 0) {
        std::printf(" %u", a);
        ++steps;
        AUREA_CHECK(steps <= kExportAttemptMax);
        if (steps > kExportAttemptMax) break;
    }
    std::printf("\n");
    AUREA_CHECK_EQ(steps, 2u);   // 1, 2, 3, 4, 5 (o 6 só tira o motion blur)
}

AUREA_TEST(ExportAttempts, MemoryFailuresTryCompatibleDecodingWithoutReducingPixels) {
    for (const auto& request : {shape(2160, 120.0), shape(1080, 60.0), shape(480, 30.0, true, true), shape(0, 0.0)}) {
        AUREA_CHECK_EQ(export_next_attempt(0, ExportRetryKind::Memory, request), 2u);
        AUREA_CHECK_EQ(export_next_attempt(1, ExportRetryKind::Memory, request), 2u);
        AUREA_CHECK_EQ(export_next_attempt(2, ExportRetryKind::Memory, request), 0u);
    }
}

AUREA_TEST(ExportAttempts, StallsTrySoftwareWithoutChangingTheTimeline) {
    const auto request = shape(2160, 120.0, true, true);
    AUREA_CHECK_EQ(export_next_attempt(0, ExportRetryKind::Stall, request), 2u);
    for (u32 attempt = 2; attempt <= kExportAttemptMax; ++attempt)
        AUREA_CHECK_EQ(export_next_attempt(attempt, ExportRetryKind::Stall, request), 0u);
}

AUREA_TEST(ExportAttempts, SoftwareStepsAlsoDecodeMediaInSoftware) {
    // 0.4.6 (Galaxy A23: 2 clipes paravam sempre em ~50%): uma mídia que o
    // decoder do aparelho não entrega falhava igual em todo degrau, porque a
    // escada só trocava encoder/tamanho. O primeiro degrau de uma falha de
    // mídia (Stall) já troca o decoder.
    for (u32 a : {0u, 1u}) AUREA_CHECK(!export_attempt_plan(a).softwareDecode);
    for (u32 a : {2u, 3u, 4u, 5u, 6u}) AUREA_CHECK(export_attempt_plan(a).softwareDecode);
    const ExportRequestShape r = shape(1080, 30.0);
    const u32 next = export_next_attempt(0, export_retry_kind(static_cast<i32>(Errc::DecodeFailed),
        static_cast<u32>(ExportFailure::Media), false, false), r);
    AUREA_CHECK(export_attempt_plan(next).softwareDecode);
    AUREA_CHECK_EQ(export_attempt_short_side(next, 1080), 1080u);   // sem perder resolução
}

AUREA_TEST(ExportAttempts, GpuTimeoutsNeverReduceAuthoredSamples) {
    // 1.0.7.1: o mesmo degrau de novo (agora um passe por envio), nunca um
    // degrau com menos amostras, pixels ou quadros.
    const auto request = shape(2160, 120.0, true, true);
    for (u32 attempt = 0; attempt <= kExportAttemptMax; ++attempt)
        AUREA_CHECK_EQ(export_next_attempt(attempt, ExportRetryKind::GpuTime, request), attempt | kExportAttemptWait);
}

AUREA_TEST(ExportAttempts, EveryRetryKeepsFullGpuQuality) {
    for (u32 attempt = 0; attempt <= kExportAttemptMax; ++attempt) {
        AUREA_CHECK_EQ(export_attempt_plan(attempt).gpuStep, 0u);
        AUREA_CHECK_EQ(export_attempt_plan(attempt).blurCap, 0u);
    }
}

// 0.3.0: a 200 qps nenhum encoder de hardware se anuncia; o 1o degrau que
// baixava a taxa era o 3 (junto com 720p). Agora 1 e 2 já ficam em 60 qps,
// sem mexer no tamanho, e pedidos até 60 seguem iguais.
AUREA_TEST(ExportAttempts, HighFrameRatesArePreservedAcrossRetries) {
    for (f64 fps : {23.976, 30.0, 60.0, 120.0, 200.0}) {
        const auto request = shape(2160, fps, true, true);
        for (u32 attempt = 0; attempt <= kExportAttemptMax; ++attempt) {
            const auto effect = export_attempt_effect(attempt, request);
            AUREA_CHECK_NEAR(effect.fps, fps, 1e-9);
            AUREA_CHECK_EQ(effect.shortSide, 2160u);
            AUREA_CHECK(effect.hevc && effect.upscale);
        }
    }
}

AUREA_TEST(ExportAttempts, BusyRepeatsTheSameRungAndNothingRepeatsAnIdenticalOne) {
    const ExportRequestShape r = shape(720, 30.0);
    AUREA_CHECK_EQ(export_next_attempt(0, ExportRetryKind::Busy, r), 0u | kExportAttemptWait);
    AUREA_CHECK_EQ(export_next_attempt(3, ExportRetryKind::Busy, r), 3u | kExportAttemptWait);
    AUREA_CHECK_EQ(export_next_attempt(2, ExportRetryKind::None, r), 0u);
    // 720p H.264 a 30 qps: o degrau 3 é igual ao 1 e o 4 igual ao 2 — pulados.
    AUREA_CHECK_EQ(export_next_attempt(0, ExportRetryKind::Encoder, r), 1u);
    AUREA_CHECK_EQ(export_next_attempt(1, ExportRetryKind::Encoder, r), 2u);
    AUREA_CHECK_EQ(export_next_attempt(2, ExportRetryKind::Encoder, r), 0u);
}

AUREA_TEST(ExportAttempts, NarrowAddressSpaceKeepsTheExportSmall) {
    // 32 bits: no máximo 2 slots de leitura e 64 MB de cache do renderer.
    AUREA_CHECK_EQ(address_space::export_readback_slots(4, 32), 2u);
    AUREA_CHECK_EQ(address_space::export_readback_slots(1, 32), 1u);
    AUREA_CHECK_EQ(address_space::export_render_budget(128ull << 20, 32), 64ull << 20);
    AUREA_CHECK_EQ(address_space::export_render_budget(32ull << 20, 32), 32ull << 20);
    // 64 bits: como sempre.
    AUREA_CHECK_EQ(address_space::export_readback_slots(4, 64), 4u);
    AUREA_CHECK_EQ(address_space::export_render_budget(512ull << 20, 64), 512ull << 20);
}

AUREA_TEST(ExportAttempts, QualcommOmxEncodersGetCbrInsteadOfFrameDroppingVbr) {
    using android_codec::device_rate_mode;
    // Moto g52: OMX.qcom em VBR descarta quadros e o export trava; CBR não.
    // (o g52 não anuncia CBR e o aceita: o encoder tenta CBR e, se o
    // configure recusar, abre de novo no modo pedido).
    AUREA_CHECK(device_rate_mode("OMX.qcom.video.encoder.avc", 1) == 0u);
    AUREA_CHECK(device_rate_mode("OMX.qcom.video.encoder.hevc", 1) == 0u);
    AUREA_CHECK(device_rate_mode("OMX.qcom.video.encoder.avc", 0) == 0u);
    // Codec2 da Qualcomm, outros fabricantes e software: o VBR pedido fica.
    for (const char* name : {"c2.qti.avc.encoder", "OMX.MTK.VIDEO.ENCODER.AVC", "c2.exynos.h264.encoder",
                             "c2.android.avc.encoder", "OMX.google.h264.encoder", ""})
        AUREA_CHECK(device_rate_mode(name, 1) == 1u);
    AUREA_CHECK(device_rate_mode(nullptr, 1) == 1u);
}

AUREA_TEST(ExportAttempts, AndroidCodecNamesTellSoftwareFromHardware) {
    AUREA_CHECK(android_codec::software_by_name("OMX.google.h264.encoder"));
    AUREA_CHECK(android_codec::software_by_name("c2.android.avc.encoder"));
    AUREA_CHECK(android_codec::software_by_name("OMX.SEC.sw.avc.encoder"));
    AUREA_CHECK(android_codec::software_by_name("OMX.MTK.VIDEO.ENCODER.AVC.sw"));
    AUREA_CHECK(android_codec::software_by_name(""));
    AUREA_CHECK(android_codec::software_by_name(nullptr));
    AUREA_CHECK(!android_codec::software_by_name("OMX.qcom.video.encoder.avc"));
    AUREA_CHECK(!android_codec::software_by_name("OMX.MTK.VIDEO.ENCODER.AVC"));
    AUREA_CHECK(!android_codec::software_by_name("OMX.Exynos.AVC.Encoder"));
    AUREA_CHECK(!android_codec::software_by_name("c2.exynos.h264.encoder"));
    AUREA_CHECK(!android_codec::software_by_name("OMX.IMG.TOPAZ.VIDEO.Encoder"));
}

// 0.4.5 ("exportação ainda falha para alguns usuários"): a correção da 0.4.0
// recusava TODO lado menor pedido ímpar ("dimensoes de exportacao invalidas")
// enquanto as duas telas mostravam o tamanho alinhado; os degraus 0..2 falhavam
// e o vídeo só saía em 720p. Agora só o valor absurdo (acima do teto do sink,
// par ou ímpar) é recusado; o ímpar plausível sai alinhado, como as telas mostram.
AUREA_TEST(ExportAttempts, OddRequestedSideAlignsAndOnlyAbsurdValuesAreRejected) {
    auto s = export_attempt_frame_size(1920, 1080, 1079, false);
    AUREA_CHECK_EQ(s.width, 1920u); AUREA_CHECK_EQ(s.height, 1080u);
    s = export_attempt_frame_size(1920, 1080, 1079, true);
    AUREA_CHECK_EQ(s.width, 1920u); AUREA_CHECK_EQ(s.height, 1080u);
    s = export_attempt_frame_size(1080, 1920, 721, false);
    AUREA_CHECK_EQ(s.width, 722u); AUREA_CHECK_EQ(s.height, 1280u);
    for (u32 want : {481u, 719u, 1079u, 1081u, 1439u, 2159u})
        for (bool align : {false, true}) {
            const auto z = export_attempt_frame_size(1920, 1080, want, align);
            AUREA_CHECK(z.width > 0 && z.height > 0 && z.width % 2 == 0 && z.height % 2 == 0);
            AUREA_CHECK_EQ(z.width % kExportLongSideAlign, 0u);
        }
    // Absurdo (ímpar ou par): nunca vira um tamanho gigante para o encoder.
    for (u32 want : {std::numeric_limits<u32>::max(), std::numeric_limits<u32>::max() - 1u, kExportMaxFrameSide + 2u})
        for (bool align : {false, true}) {
            const auto z = export_attempt_frame_size(1920, 1080, want, align);
            AUREA_CHECK_EQ(z.width, 0u); AUREA_CHECK_EQ(z.height, 0u);
        }
    // O teto do sink ainda é um pedido válido (o teto do aparelho reduz depois).
    AUREA_CHECK(export_attempt_frame_size(1080, 1080, kExportMaxFrameSide, false).width > 0);
}

// 0.4.5: o sink recusou o codec/formato ao abrir (ExportFailure::Unsupported):
// a escada tenta H.264 e o software NO MESMO TAMANHO antes de tirar pixels.
// Antes era tratado como memória e um HEVC 1080p ia direto para 720p.
AUREA_TEST(ExportAttempts, UnsupportedCodecTriesCompatibleRoutesAtTheRequestedCodec) {
    const auto request = shape(2160, 120.0, true, true);
    const auto kind = export_retry_kind(code(Errc::NotSupported), why(ExportFailure::Unsupported), false, false);
    AUREA_CHECK(kind == ExportRetryKind::Encoder);
    AUREA_CHECK_EQ(export_next_attempt(0, kind, request), 1u);
    AUREA_CHECK_EQ(export_next_attempt(1, kind, request), 2u);
    AUREA_CHECK_EQ(export_next_attempt(2, kind, request), 0u);
}

// 0.4.5: a taxa enviada ao encoder de hardware respeita o teto que ele anuncia
// (getBitrateRange); teto desconhecido mantém o pedido.
AUREA_TEST(ExportAttempts, EncoderBitrateRespectsTheAnnouncedCeiling) {
    AUREA_CHECK_EQ(export_encoder_bitrate_bps(18'000'000u, 14'000'000u), 14'000'000u);
    AUREA_CHECK_EQ(export_encoder_bitrate_bps(8'000'000u, 14'000'000u), 8'000'000u);
    AUREA_CHECK_EQ(export_encoder_bitrate_bps(18'000'000u, 0u), 18'000'000u);
    AUREA_CHECK_EQ(export_encoder_bitrate_bps(0u, 14'000'000u), 0u);
    AUREA_CHECK_EQ(export_encoder_bitrate_bps(18'000'000u, 1u), 100'000u);
}

// =============================================================================
//  0.5.2 (E2): a escada com memória (ExportAttemptHistory). Uma falha no MESMO
//  ponto em duas tentativas seguidas é determinística: a escada para em vez de
//  refazer o export inteiro até 7 vezes (Galaxy A23, quadro 19 de 38 nos 7).
// =============================================================================
namespace {
ExportAttemptRecord ran(u32 attempt, f64 failedAt, u32 frames, f64 fps = 30.0, u64 request = 7) {
    ExportAttemptRecord r;
    r.attempt = attempt;
    r.request = request;
    r.code = static_cast<i32>(Errc::Timeout);
    r.failure = static_cast<u32>(ExportFailure::Render);
    r.started = true;
    r.framesDone = frames;
    r.failedAt = failedAt;
    r.frameSeconds = 1.0 / fps;
    return r;
}
} // namespace

AUREA_TEST(ExportAttempts, RepeatedMemoryFailureStopsAfterCompatibleDecoding) {
    const auto request = shape(1080, 30.0);
    ExportAttemptHistory history;
    history.record(ran(0, 19.0 / 30.0, 18));
    AUREA_CHECK_EQ(export_next_attempt_with_history(0, ExportRetryKind::Memory, request, history), 2u);
    history.record(ran(2, 19.0 / 30.0, 18));
    AUREA_CHECK_EQ(export_next_attempt_with_history(2, ExportRetryKind::Memory, request, history), 0u);
}

AUREA_TEST(ExportAttempts, SamePointIsMeasuredInTimelineSeconds) {
    const auto a = ran(0, 2.0, 119, 60.0);
    const auto b = ran(2, 2.0, 59, 30.0);
    AUREA_CHECK(export_attempt_same_point(a, b));
}

AUREA_TEST(ExportAttempts, DeterministicEncoderOrDecoderStopTriesSoftwareOnce) {
    // Encoder do aparelho e o Baseline param no mesmo quadro: o software
    // (encoder E decoder compatíveis) ainda não foi tentado — UMA vez.
    const ExportRequestShape r = shape(1080, 30.0);
    ExportAttemptHistory h;
    h.record(ran(0, 5.0, 149));
    AUREA_CHECK_EQ(export_next_attempt_with_history(0, ExportRetryKind::Encoder, r, h), 1u);
    h.record(ran(1, 5.0, 149));
    AUREA_CHECK_EQ(export_next_attempt_with_history(1, ExportRetryKind::Encoder, r, h), 2u);
    h.record(ran(2, 5.0, 149));
    AUREA_CHECK_EQ(export_next_attempt_with_history(2, ExportRetryKind::Encoder, r, h), 0u);
    // Decoder parado: o 1o degrau já é o de software; o mesmo ponto depois dele para.
    ExportAttemptHistory s;
    s.record(ran(0, 3.0, 89));
    AUREA_CHECK_EQ(export_next_attempt_with_history(0, ExportRetryKind::Stall, r, s), 2u);
    s.record(ran(2, 3.0, 89));
    AUREA_CHECK_EQ(export_next_attempt_with_history(2, ExportRetryKind::Stall, r, s), 0u);
}

AUREA_TEST(ExportAttempts, GpuTimeStopsWhileEncoderStartupStillHasCompatibleRoutes) {
    const auto request = shape(1080, 30.0);
    ExportAttemptHistory history;
    history.record(ran(0, 2.0, 59));
    AUREA_CHECK_EQ(export_next_attempt_with_history(0, ExportRetryKind::GpuTime, request, history), 0u | kExportAttemptWait);
    ExportAttemptHistory opening;
    ExportAttemptRecord record; record.request = 7; record.code = code(Errc::EncoderUnavailable);
    opening.record(record);
    AUREA_CHECK_EQ(export_next_attempt_with_history(0, ExportRetryKind::Encoder, request, opening), 1u);
    record.attempt = 1; opening.record(record);
    AUREA_CHECK_EQ(export_next_attempt_with_history(1, ExportRetryKind::Encoder, request, opening), 2u);
}

AUREA_TEST(ExportAttempts, EquivalentSoftwareRoutesAreNotRenderedAgain) {
    const auto request = shape(1080, 30.0);
    ExportAttemptHistory history;
    history.record(ran(0, 1.0, 29));
    AUREA_CHECK_EQ(export_next_attempt_with_history(0, ExportRetryKind::Stall, request, history), 2u);
    history.record(ran(2, 4.0, 119));
    AUREA_CHECK_EQ(export_next_attempt_with_history(2, ExportRetryKind::Stall, request, history), 0u);
}

AUREA_TEST(ExportAttempts, HistoryStartsOverForANewRequest) {
    const ExportRequestShape r = shape(1080, 30.0);
    ExportAttemptHistory h;
    h.record(ran(0, 1.0, 29, 30.0, 1));
    (void)export_next_attempt_with_history(0, ExportRetryKind::Memory, r, h);
    h.record(ran(3, 1.0, 29, 30.0, 1));
    AUREA_CHECK_EQ(export_next_attempt_with_history(3, ExportRetryKind::Memory, r, h), 0u);
    // Outro arquivo de saída = outro pedido: começa do zero.
    h.record(ran(0, 1.0, 29, 30.0, 2));
    AUREA_CHECK_EQ(h.count, 1u);
    AUREA_CHECK_EQ(export_next_attempt_with_history(0, ExportRetryKind::Memory, r, h), 2u);
    // O mesmo pedido de novo no degrau 0 (a pessoa tocou Exportar outra vez).
    h.record(ran(3, 1.0, 29, 30.0, 2));
    h.record(ran(0, 1.0, 29, 30.0, 2));
    AUREA_CHECK_EQ(h.count, 1u);
    // O MESMO degrau (continuação do checkpoint) substitui o registro.
    h.record(ran(0, 2.0, 59, 30.0, 2));
    AUREA_CHECK_EQ(h.count, 1u);
    AUREA_CHECK(std::abs(h.items[0].failedAt - 2.0) < 1e-9);
    AUREA_CHECK_EQ(ExportAttemptLedger::request_key("a.mp4"), ExportAttemptLedger::request_key("a.mp4"));
    AUREA_CHECK(ExportAttemptLedger::request_key("a.mp4") != ExportAttemptLedger::request_key("b.mp4"));
}

AUREA_TEST(ExportAttempts, StartRefusedAsBusyOnTwoRungsStopsTheLadder) {
    // A prévia não soltou a GPU no prazo da partida (Engine: Busy) no degrau 0
    // e de novo no 1: outro degrau não solta a GPU — a escada para.
    const ExportRequestShape r = shape(1080, 30.0);
    ExportAttemptHistory h;
    ExportAttemptRecord busy; busy.attempt = 0; busy.request = 9; busy.code = static_cast<i32>(Errc::Busy);
    h.record(busy);
    AUREA_CHECK_EQ(export_next_attempt_with_history(0, ExportRetryKind::Busy, r, h), 0u | kExportAttemptWait);
    AUREA_CHECK_EQ(export_next_attempt_with_history(0, ExportRetryKind::Encoder, r, h), 1u);
    busy.attempt = 1; h.record(busy);
    AUREA_CHECK_EQ(export_next_attempt_with_history(1, ExportRetryKind::Encoder, r, h), 0u);
    AUREA_CHECK(h.lastStop == ExportLadderStop::Deterministic);
}

// 0.5.3 (Fase B): orçamento de memória, tempo do pacote e memória da escada.
#include "Export053Pure.inl"
