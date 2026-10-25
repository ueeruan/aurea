// =============================================================================
//  ESCADA DE TENTATIVAS DO EXPORT (0.2.6, export/ExportAttempts.hpp): exportar
//  não termina em "erro N" quando outro jeito funcionaria. Também os limites
//  do export de 32 bits (AddressSpace.hpp) e o encoder pelo nome no Android 8/9.
// =============================================================================
#include "TestFramework.hpp"

#include "aurea/export/ExportAttempts.hpp"
#include "aurea/platform/AddressSpace.hpp"
#include "../src/export/AndroidCodecName.hpp"

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

AUREA_TEST(ExportAttempts, PlansGoFromTheRequestToSoftwareAt480) {
    AUREA_CHECK_EQ(export_attempt_plan(0).safeMode, 0u);
    AUREA_CHECK(!export_attempt_plan(0).forceH264);
    AUREA_CHECK_EQ(export_attempt_plan(1).safeMode, 1u);
    AUREA_CHECK(export_attempt_plan(1).forceH264);
    AUREA_CHECK_EQ(export_attempt_plan(2).safeMode, 2u);
    AUREA_CHECK_EQ(export_attempt_plan(3).shortSideCap, 720u);
    AUREA_CHECK_EQ(export_attempt_plan(4).safeMode, 2u);
    AUREA_CHECK_EQ(export_attempt_plan(5).shortSideCap, 480u);
    AUREA_CHECK(export_attempt_plan(5).noUpscale);
    AUREA_CHECK_EQ(export_attempt_plan(99).shortSideCap, 480u);   // além do fim: o último
    // 0.2.7: teto de motion blur por degrau; o último sai sem desfoque.
    AUREA_CHECK_EQ(export_attempt_plan(0).blurCap, 0u);
    AUREA_CHECK_EQ(export_attempt_plan(1).blurCap, 32u);
    AUREA_CHECK_EQ(export_attempt_plan(3).blurCap, 16u);
    AUREA_CHECK_EQ(export_attempt_plan(5).blurCap, 4u);
    AUREA_CHECK_EQ(export_attempt_plan(6).blurCap, 1u);
    AUREA_CHECK_EQ(export_attempt_plan(6).shortSideCap, 480u);
    AUREA_CHECK_EQ(export_attempt_short_side(3, 1080), 720u);
    AUREA_CHECK_EQ(export_attempt_short_side(3, 480), 480u);      // nunca amplia
    AUREA_CHECK_EQ(export_attempt_short_side(1, 1080), 1080u);
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
    AUREA_CHECK(export_retry_kind(code(Errc::DeviceLost), why(ExportFailure::Render), false, false) == ExportRetryKind::Memory);
    AUREA_CHECK(export_retry_kind(code(Errc::OutOfMemory), why(ExportFailure::GpuMemory), false, false) == ExportRetryKind::Memory);
    // Parou de andar: encoder travado, decoder preso ou a tela desistiu.
    AUREA_CHECK(export_retry_kind(code(Errc::Timeout), why(ExportFailure::EncoderStalled), false, false) == ExportRetryKind::Stall);
    AUREA_CHECK(export_retry_kind(code(Errc::DecodeFailed), why(ExportFailure::Media), false, false) == ExportRetryKind::Stall);
    AUREA_CHECK(export_retry_kind(code(Errc::Cancelled), 0, true, false) == ExportRetryKind::Stall);
    // Quadro que passou do prazo da GPU (motion blur 3D): menos amostras.
    AUREA_CHECK(export_retry_kind(code(Errc::Timeout), why(ExportFailure::Render), false, false) == ExportRetryKind::GpuTime);
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
    AUREA_CHECK_EQ(steps, kExportAttemptMax - 1);   // 1, 2, 3, 4, 5 (o 6 só tira o motion blur)
}

AUREA_TEST(ExportAttempts, MemoryFailuresAlwaysTakeFewerPixels) {
    const ExportRequestShape r = shape(1080, 60.0);
    AUREA_CHECK_EQ(export_next_attempt(0, ExportRetryKind::Memory, r), 3u);   // 1080 → 720 (hardware)
    AUREA_CHECK_EQ(export_next_attempt(3, ExportRetryKind::Memory, r), 5u);   // 720 → 480
    AUREA_CHECK_EQ(export_next_attempt(5, ExportRetryKind::Memory, r), 6u);   // sem pixels a tirar: sem motion blur
    AUREA_CHECK_EQ(export_next_attempt(6, ExportRetryKind::Memory, r), 0u);   // acabou
    AUREA_CHECK_EQ(export_next_attempt(1, ExportRetryKind::Memory, r), 3u);
    // Pedido já pequeno: o primeiro degrau que ainda tira pixels.
    AUREA_CHECK_EQ(export_next_attempt(0, ExportRetryKind::Memory, shape(720, 30.0)), 5u);
    // Upscale ligado: tirar o upscale já é menos memória.
    AUREA_CHECK_EQ(export_next_attempt(0, ExportRetryKind::Memory, shape(480, 30.0, false, true)), 3u);
    // Lado desconhecido (0 = o da composição): o teto pode ajudar.
    AUREA_CHECK_EQ(export_next_attempt(0, ExportRetryKind::Memory, shape(0, 0.0)), 3u);
}

AUREA_TEST(ExportAttempts, StallsGoToSoftwareThenSmaller) {
    const ExportRequestShape r = shape(1080, 30.0);
    AUREA_CHECK_EQ(export_next_attempt(0, ExportRetryKind::Stall, r), 2u);
    AUREA_CHECK_EQ(export_next_attempt(2, ExportRetryKind::Stall, r), 4u);
    AUREA_CHECK_EQ(export_next_attempt(4, ExportRetryKind::Stall, r), 5u);
    AUREA_CHECK_EQ(export_next_attempt(5, ExportRetryKind::Stall, r), 6u);
    AUREA_CHECK_EQ(export_next_attempt(6, ExportRetryKind::Stall, r), 0u);
}

AUREA_TEST(ExportAttempts, GpuTimeoutsDropMotionBlurSamplesBeforePixels) {
    // 0.2.7 (3D com motion blur não exportava): o quadro passa do prazo da
    // GPU; menos amostras mantêm a resolução, e só depois vêm menos pixels.
    const ExportRequestShape r = shape(1080, 60.0);
    AUREA_CHECK_EQ(export_next_attempt(0, ExportRetryKind::GpuTime, r), 1u);   // 32 amostras, 1080
    AUREA_CHECK_EQ(export_next_attempt(1, ExportRetryKind::GpuTime, r), 2u);   // 16
    AUREA_CHECK_EQ(export_next_attempt(2, ExportRetryKind::GpuTime, r), 3u);   // 720p
    AUREA_CHECK_EQ(export_next_attempt(3, ExportRetryKind::GpuTime, r), 4u);   // 8
    AUREA_CHECK_EQ(export_next_attempt(4, ExportRetryKind::GpuTime, r), 5u);   // 480p, 4
    AUREA_CHECK_EQ(export_next_attempt(5, ExportRetryKind::GpuTime, r), 6u);   // sem desfoque
    AUREA_CHECK_EQ(export_next_attempt(6, ExportRetryKind::GpuTime, r), 0u);
}

// 0.3.0: a 200 qps nenhum encoder de hardware se anuncia; o 1o degrau que
// baixava a taxa era o 3 (junto com 720p). Agora 1 e 2 já ficam em 60 qps,
// sem mexer no tamanho, e pedidos até 60 seguem iguais.
AUREA_TEST(ExportAttempts, HighFrameRatesDropTo60BeforeAnyPixel) {
    const ExportRequestShape r = shape(1080, 200.0);
    AUREA_CHECK_EQ(export_next_attempt(0, ExportRetryKind::Encoder, r), 1u);
    AUREA_CHECK_NEAR(export_attempt_effect(1, r).fps, 60.0, 1e-9);
    AUREA_CHECK_EQ(export_attempt_effect(1, r).shortSide, 1080u);
    AUREA_CHECK_NEAR(export_attempt_effect(2, r).fps, 60.0, 1e-9);
    AUREA_CHECK_NEAR(export_attempt_effect(3, r).fps, 30.0, 1e-9);
    // Travou a 200 (software lento): software a 60, ainda em 1080.
    AUREA_CHECK_EQ(export_next_attempt(0, ExportRetryKind::Stall, r), 2u);
    // Até 60 qps nada muda nos degraus 1 e 2.
    AUREA_CHECK_NEAR(export_attempt_effect(1, shape(1080, 60.0)).fps, 60.0, 1e-9);
    AUREA_CHECK_NEAR(export_attempt_effect(2, shape(1080, 30.0)).fps, 30.0, 1e-9);
    AUREA_CHECK_NEAR(export_attempt_plan(1).fpsCap, 60.0, 1e-9);
    AUREA_CHECK_NEAR(export_attempt_plan(0).fpsCap, 0.0, 1e-9);
}

AUREA_TEST(ExportAttempts, BusyRepeatsTheSameRungAndNothingRepeatsAnIdenticalOne) {
    const ExportRequestShape r = shape(720, 30.0);
    AUREA_CHECK_EQ(export_next_attempt(0, ExportRetryKind::Busy, r), 0u | kExportAttemptWait);
    AUREA_CHECK_EQ(export_next_attempt(3, ExportRetryKind::Busy, r), 3u | kExportAttemptWait);
    AUREA_CHECK_EQ(export_next_attempt(2, ExportRetryKind::None, r), 0u);
    // 720p H.264 a 30 qps: o degrau 3 é igual ao 1 e o 4 igual ao 2 — pulados.
    AUREA_CHECK_EQ(export_next_attempt(0, ExportRetryKind::Encoder, r), 1u);
    AUREA_CHECK_EQ(export_next_attempt(1, ExportRetryKind::Encoder, r), 2u);
    AUREA_CHECK_EQ(export_next_attempt(2, ExportRetryKind::Encoder, r), 5u);
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
