// =============================================================================
//  Watchdog do export (export/ExportWatchdog.hpp) — "a exportação para num
//  percentual e nunca termina" (beta, vários Android). O encoder do aparelho
//  pode prender a thread DENTRO da plataforma (HAL travado: nem o timeout do
//  MediaCodec volta, nem o stop()). O motor não pode esperar esse join para
//  sempre: desiste do worker, conclui com EncoderStalled e sugere o modo de
//  segurança. Sem GPU (MockBackend): roda em qualquer máquina.
//
//  Incluído por test_export.cpp (usa Rig, BenchCapture e Outcome de lá).
// =============================================================================

namespace {

Outcome run_export_with(Rig& r, const ExportSettings& s, int timeoutS) {
    Outcome o;
    const auto t0 = std::chrono::steady_clock::now();
    if (!r.e.start_export(s, "nao-usado.mp4").ok()) return o;
    for (int i = 0; i < timeoutS * 1000; ++i) {
        if (r.e.export_progress().finished) { o.finished = true; break; }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    o.seconds = std::chrono::duration<f64>(std::chrono::steady_clock::now() - t0).count();
    o.p = r.e.export_progress();
    return o;
}

/// Solta a chamada presa e espera o worker abandonado sair (ele não toca mais
/// no sink nem na captura: só marca a saída no contexto que vazou).
void release_hung(std::atomic<bool>& release, std::atomic<bool>& returned) {
    release.store(true, std::memory_order_release);
    for (int i = 0; i < 5000 && !returned.load(std::memory_order_acquire); ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
}

struct HungGate {
    std::atomic<bool> entered{false}, release{false}, returned{false};
    void arm(BenchCapture& c) {
        c.hangEntered = &entered;
        c.hangRelease = &release;
        c.hangReturned = &returned;
    }
};

} // namespace

AUREA_TEST(ExportWatchdog, HungEncoderIsAbandonedAndSafeModeRetryFinishes) {
    SyntheticConfig cfg; cfg.width = 64; cfg.height = 36;
    auto* backend = new MockBackend(); backend->mapBuffers = true;
    Rig r(cfg, 30, 6, 2, nullptr, backend, 400); AUREA_CHECK(r.ok); if (!r.ok) return;
    r.comp()->layer(r.video_layer())->visible = false;
    HungGate gate; gate.arm(r.cap);
    r.cap.hangWriteAt = 2;   // o 3º quadro entra no encoder e não volta
    ExportSettings s; s.height = 36; s.fps = 30; s.dither = false; s.videoCodec = ExportCodec::HEVC;
    s.videoBitrateMbps = 0;   // automático pela qualidade, como as telas pedem
    const Outcome o = run_export_with(r, s, 20);
    AUREA_CHECK(gate.entered.load());
    AUREA_CHECK(o.finished);
    AUREA_CHECK_EQ(o.p.result, Errc::Timeout);
    AUREA_CHECK_EQ(o.p.failure, static_cast<u32>(ExportFailure::EncoderStalled));
    AUREA_CHECK_EQ(export_retry_from_flags(o.p.flags), 1u);
    std::printf("travado detectado em %.1f s ", o.seconds);
    AUREA_CHECK(o.seconds < 5.0);
    // Ninguém chamou o sink preso de novo: abort/finish prenderiam também.
    AUREA_CHECK(!r.cap.finished && !r.cap.aborted);
    // O que vai para as duas telas (ABI da bridge) leva a sugestão e o motivo.
    bridge::ExportProgressPOD pod;
    r.e.fill_export_progress(pod);
    AUREA_CHECK_EQ(export_retry_from_flags(pod.flags), 1u);
    AUREA_CHECK_EQ(pod.flags >> kExportFailureShift, static_cast<u32>(ExportFailure::EncoderStalled));

    // A tela refaz no modo sugerido: o contexto preso fica de lado (vivo) e o
    // export novo termina — H.264 Baseline, múltiplos de 16, taxa menor.
    r.cap.hangWriteAt = -1;
    r.cap.hashes.clear();
    r.cap.pts.clear();
    r.cap.writes = 0;
    s.safeMode = export_retry_from_flags(o.p.flags);
    const Outcome retry = run_export_with(r, s, 20);
    AUREA_CHECK(retry.finished);
    AUREA_CHECK_EQ(retry.p.result, Errc::Ok);
    AUREA_CHECK(r.cap.finished);
    AUREA_CHECK_EQ(r.cap.hashes.size(), usize{6});
    AUREA_CHECK(r.cap.ptsMonotonic);
    AUREA_CHECK((retry.p.flags & Engine::kExportSafeMode) != 0);
    AUREA_CHECK_EQ(export_retry_from_flags(retry.p.flags), 0u);
    AUREA_CHECK(r.cap.video.codec == ExportCodec::H264);
    AUREA_CHECK_EQ(r.cap.video.profile, kExportProfileBaseline);
    AUREA_CHECK(!r.cap.video.preferSoftware);
    AUREA_CHECK_EQ(r.cap.video.width % 16, 0u);
    AUREA_CHECK_EQ(r.cap.video.height % 16, 0u);
    AUREA_CHECK_EQ(r.cap.video.bitrateBps,
                   export_safe_bitrate_bps(export_video_bitrate_bps(r.cap.video.width, r.cap.video.height, 30.0,
                                                                    ExportCodec::H264, ExportQuality::Normal), 1));
    release_hung(gate.release, gate.returned);
    AUREA_CHECK(gate.returned.load());
}

AUREA_TEST(ExportWatchdog, HungFinishIsAbandonedAsEncoderStall) {
    SyntheticConfig cfg; cfg.width = 64; cfg.height = 36;
    auto* backend = new MockBackend(); backend->mapBuffers = true;
    Rig r(cfg, 30, 3, 1, nullptr, backend, 400); AUREA_CHECK(r.ok); if (!r.ok) return;
    r.comp()->layer(r.video_layer())->visible = false;
    HungGate gate; gate.arm(r.cap);
    r.cap.hangFinish = true;   // EOS/esvaziar/fechar o MP4 nunca volta
    const Outcome o = run_export(r, 36, 30, false, 20);
    AUREA_CHECK(o.finished);
    AUREA_CHECK_EQ(o.p.result, Errc::Timeout);
    AUREA_CHECK_EQ(o.p.failure, static_cast<u32>(ExportFailure::EncoderStalled));
    AUREA_CHECK_EQ(export_retry_from_flags(o.p.flags), 1u);
    AUREA_CHECK_EQ(r.cap.hashes.size(), usize{3});
    AUREA_CHECK(!r.cap.aborted);
    AUREA_CHECK(o.seconds < 5.0);
    release_hung(gate.release, gate.returned);
}

/// Cancelar com o encoder preso na plataforma: o limite cai para 3 s (todo
/// laço de sink olha o cancelamento a cada volta), mesmo com o prazo padrão de
/// 45 s — o botão Cancelar nunca fica sem resposta.
AUREA_TEST(ExportWatchdog, CancelNeverWaitsForAnEncoderStuckInThePlatform) {
    SyntheticConfig cfg; cfg.width = 64; cfg.height = 36;
    auto* backend = new MockBackend(); backend->mapBuffers = true;
    Rig r(cfg, 30, 30, 2, nullptr, backend); AUREA_CHECK(r.ok); if (!r.ok) return;
    r.comp()->layer(r.video_layer())->visible = false;
    HungGate gate; gate.arm(r.cap);
    r.cap.hangWriteAt = 1;
    ExportSettings s; s.height = 36; s.fps = 30; s.dither = false;
    AUREA_CHECK(r.e.start_export(s, "nao-usado.mp4").ok());
    for (int i = 0; i < 5000 && !gate.entered.load(); ++i) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    AUREA_CHECK(gate.entered.load());
    AUREA_CHECK(r.e.export_progress().running);
    const auto t0 = std::chrono::steady_clock::now();
    AUREA_CHECK(r.e.cancel_export().ok());
    bool done = false;
    for (int i = 0; i < 10000 && !done; ++i) {
        done = r.e.export_progress().finished;
        if (!done) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    const f64 secs = std::chrono::duration<f64>(std::chrono::steady_clock::now() - t0).count();
    std::printf("cancelado com o encoder preso em %.1f s ", secs);
    AUREA_CHECK(done);
    AUREA_CHECK(secs < 5.0);
    AUREA_CHECK_EQ(r.e.export_progress().result, Errc::Cancelled);
    AUREA_CHECK_EQ(export_retry_from_flags(r.e.export_progress().flags), 0u);
    release_hung(gate.release, gate.returned);
}

/// Encoder de software lento mas VIVO (o laço dele volta da plataforma e bate
/// o coração): nunca é confundido com travado, mesmo bem acima do limite.
AUREA_TEST(ExportWatchdog, SlowEncoderThatKeepsBeatingIsNotAbandoned) {
    SyntheticConfig cfg; cfg.width = 64; cfg.height = 36;
    auto* backend = new MockBackend(); backend->mapBuffers = true;
    Rig r(cfg, 30, 3, 1, nullptr, backend, 300); AUREA_CHECK(r.ok); if (!r.ok) return;
    r.comp()->layer(r.video_layer())->visible = false;
    r.cap.beatingWriteMs = 1200;   // 4× o limite, batendo a cada 10 ms
    const Outcome o = run_export(r, 36, 30, false, 20);
    AUREA_CHECK(o.finished);
    AUREA_CHECK_EQ(o.p.result, Errc::Ok);
    AUREA_CHECK(r.cap.finished && !r.cap.aborted);
    AUREA_CHECK_EQ(r.cap.hashes.size(), usize{3});
    AUREA_CHECK(r.cap.beat != nullptr);
}

/// Nível 2 = o mesmo do 1 no encoder de SOFTWARE, e é a última volta: se ainda
/// falhar pelo encoder, o motor não sugere outra (a tela mostra o motivo).
AUREA_TEST(ExportWatchdog, SafeModeLevelTwoAsksForSoftwareAndIsTheLastRetry) {
    SyntheticConfig cfg; cfg.width = 64; cfg.height = 36;
    auto* backend = new MockBackend(); backend->mapBuffers = true;
    Rig r(cfg, 30, 3, 1, nullptr, backend); AUREA_CHECK(r.ok); if (!r.ok) return;
    r.comp()->layer(r.video_layer())->visible = false;
    r.cap.writeFailure = Status{Errc::EncodeFailed, "encoder recusou o quadro (teste)"};
    ExportSettings s; s.height = 36; s.fps = 30; s.dither = false;
    const Outcome first = run_export_with(r, s, 10);
    AUREA_CHECK(first.finished);
    AUREA_CHECK_EQ(first.p.failure, static_cast<u32>(ExportFailure::Encoder));
    AUREA_CHECK_EQ(export_retry_from_flags(first.p.flags), 1u);
    AUREA_CHECK((first.p.flags & Engine::kExportSafeMode) == 0);
    AUREA_CHECK_EQ(r.cap.video.profile, kExportProfileDefault);
    s.safeMode = 2;
    const Outcome last = run_export_with(r, s, 10);
    AUREA_CHECK(last.finished);
    AUREA_CHECK_EQ(last.p.failure, static_cast<u32>(ExportFailure::Encoder));
    AUREA_CHECK_EQ(export_retry_from_flags(last.p.flags), 0u);
    AUREA_CHECK((last.p.flags & Engine::kExportSafeMode) != 0);
    AUREA_CHECK(r.cap.video.preferSoftware);
    AUREA_CHECK_EQ(r.cap.video.profile, kExportProfileBaseline);
    AUREA_CHECK(r.cap.video.codec == ExportCodec::H264);
    AUREA_CHECK(r.cap.aborted && !r.cap.finished);
    // Fora do limite vale o máximo (a ponte também limita).
    r.cap.writeFailure = OkStatus;
    s.safeMode = 9;
    const Outcome clamped = run_export_with(r, s, 10);
    AUREA_CHECK(clamped.finished && clamped.p.result == Errc::Ok);
    AUREA_CHECK(r.cap.video.preferSoftware);
}
