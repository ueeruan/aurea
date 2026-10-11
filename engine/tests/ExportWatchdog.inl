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

Outcome run_export_with(Rig& r, const ExportSettings& s, int timeoutS, const char* output = "nao-usado.mp4") {
    Outcome o;
    const auto t0 = std::chrono::steady_clock::now();
    if (!r.e.start_export(s, output).ok()) return o;
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
    std::atomic<bool> entered{false}, release{false}, returned{false}, cancelled{false};
    void arm(BenchCapture& c) {
        c.hangEntered = &entered;
        c.hangRelease = &release;
        c.hangReturned = &returned;
        c.hangCancelled = &cancelled;
    }
};

} // namespace

AUREA_TEST(ExportWatchdog, HungEncoderIsAbandonedAndSafeModeRetryFinishes) {
    SyntheticConfig cfg; cfg.width = 64; cfg.height = 36;
    auto* backend = new MockBackend(); backend->mapBuffers = true;
    Rig r(cfg, 30, 6, 2, nullptr, backend, 1500); AUREA_CHECK(r.ok); if (!r.ok) return;
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
    // export novo termina com o mesmo formato, tamanho e bitrate solicitado.
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
    AUREA_CHECK(r.cap.video.codec == ExportCodec::HEVC);
    AUREA_CHECK_EQ(r.cap.video.profile, kExportProfileBaseline);
    AUREA_CHECK(!r.cap.video.preferSoftware);
    AUREA_CHECK_EQ(r.cap.video.width, 64u);
    AUREA_CHECK_EQ(r.cap.video.height, 36u);
    AUREA_CHECK_EQ(r.cap.video.bitrateBps,
                   export_video_bitrate_bps(64, 36, 30.0, ExportCodec::HEVC, static_cast<ExportQuality>(s.quality)));
    release_hung(gate.release, gate.returned);
    AUREA_CHECK(gate.returned.load());
}

AUREA_TEST(ExportWatchdog, HungFinishIsAbandonedAsEncoderStall) {
    SyntheticConfig cfg; cfg.width = 64; cfg.height = 36;
    auto* backend = new MockBackend(); backend->mapBuffers = true;
    Rig r(cfg, 30, 3, 1, nullptr, backend, 1500); AUREA_CHECK(r.ok); if (!r.ok) return;
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

AUREA_TEST(ExportWatchdog, LateValidationCannotPublishAnAbandonedSession) {
    SyntheticConfig cfg; cfg.width = 64; cfg.height = 36;
    auto* backend = new MockBackend(); backend->mapBuffers = true;
    Rig r(cfg, 30, 3, 1, nullptr, backend, 300); AUREA_CHECK(r.ok); if (!r.ok) return;
    r.comp()->layer(r.video_layer())->visible = false;
    const auto dir = std::filesystem::path("build/reference/astra-watchdog-late-validation") /
        std::to_string(monotonic_ns());
    std::filesystem::create_directories(dir);
    const std::string target = (dir / "video.mp4").string();
    constexpr char previous[] = "previous complete export";
    AUREA_CHECK(fileio::write_atomic(target, previous, sizeof(previous)).ok());
    HungGate gate; gate.arm(r.cap); r.cap.hangValidation = true;
    ExportSettings settings; settings.height = 36; settings.fps = 30; settings.dither = false;
    const Outcome timedOut = run_export_with(r, settings, 10, target.c_str());
    AUREA_CHECK(gate.entered.load()); AUREA_CHECK(timedOut.finished);
    AUREA_CHECK_EQ(timedOut.p.result, Errc::Timeout);
    AUREA_CHECK_EQ(timedOut.p.failure, static_cast<u32>(ExportFailure::EncoderStalled));
    AUREA_CHECK(r.cap.finished && !r.cap.aborted);
    // The platform's fresh decoder now returns success, after the watchdog
    // already retired the worker. Neither the result nor the target may change.
    release_hung(gate.release, gate.returned);
    AUREA_CHECK(gate.returned.load()); AUREA_CHECK(gate.cancelled.load());
    AUREA_CHECK_EQ(r.e.export_progress().result, Errc::Timeout);
    AUREA_CHECK_EQ(r.e.export_progress().failure, timedOut.p.failure);
    AUREA_CHECK_EQ(export_retry_from_flags(r.e.export_progress().flags), export_retry_from_flags(timedOut.p.flags));
    std::vector<u8> bytes;
    const bool read = fileio::read_all(target, bytes, 1024);
    AUREA_CHECK(read);
    if (read) {
        AUREA_CHECK_EQ(bytes.size(), sizeof(previous));
        if (bytes.size() == sizeof(previous))
            AUREA_CHECK(std::memcmp(bytes.data(), previous, sizeof(previous)) == 0);
    }
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
    AUREA_CHECK(r.cap.heartbeatPulses > 0);
    AUREA_CHECK(r.cap.beat == nullptr); // finished sinks release borrowed atomics
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

AUREA_TEST(ExportWatchdog, TwoStuckSessionsBoundRetriesAndReturnedWorkersReleaseReadbacks) {
    SyntheticConfig cfg; cfg.width = 64; cfg.height = 36;
    auto* backend = new MockBackend(); backend->mapBuffers = true;
    Rig r(cfg, 30, 3, 1, nullptr, backend, 300); AUREA_CHECK(r.ok); if (!r.ok) return;
    r.comp()->layer(r.video_layer())->visible = false;
    const usize baseBuffers = backend->mappedBuffers.size();
    HungGate first, second;
    std::atomic<bool> firstDestroyed{false}, secondDestroyed{false}, retryDestroyed{false};
    first.arm(r.cap); r.cap.hangWriteAt = 0; r.cap.sinkDestroyed = &firstDestroyed;
    const Outcome one = run_export(r, 36, 30, false, 10);
    AUREA_CHECK(first.entered.load()); AUREA_CHECK(one.finished);
    AUREA_CHECK_EQ(one.p.result, Errc::Timeout);
    second.arm(r.cap); r.cap.writes = 0; r.cap.sinkDestroyed = &secondDestroyed;
    const Outcome two = run_export(r, 36, 30, false, 10);
    AUREA_CHECK(second.entered.load()); AUREA_CHECK(two.finished);
    AUREA_CHECK_EQ(two.p.result, Errc::Timeout);
    AUREA_CHECK(!firstDestroyed.load() && !secondDestroyed.load());
    const usize twoHungBuffers = backend->mappedBuffers.size();
    AUREA_CHECK(twoHungBuffers > baseBuffers);
    ExportSettings settings; settings.height = 36; settings.fps = 30; settings.dither = false;
    const auto started = std::chrono::steady_clock::now();
    const Status refused = r.e.start_export(settings, "nao-usado.mp4");
    AUREA_CHECK_EQ(refused.code(), Errc::BudgetExceeded);
    AUREA_CHECK(std::chrono::steady_clock::now() - started < std::chrono::seconds(1));
    AUREA_CHECK_EQ(backend->mappedBuffers.size(), twoHungBuffers);
    AUREA_CHECK_EQ(r.e.export_progress().result, Errc::Timeout);
    // Each old sink closes on its own worker. The next admitted export reclaims
    // that session's GPU readbacks and graph/cache instead of leaking forever.
    if (first.entered.load()) release_hung(first.release, first.returned);
    AUREA_CHECK(first.returned.load()); AUREA_CHECK(firstDestroyed.load());
    AUREA_CHECK(!secondDestroyed.load());
    r.cap.hangWriteAt = -1; r.cap.writes = 0;
    r.cap.hashes.clear(); r.cap.pts.clear(); r.cap.finished = r.cap.aborted = false;
    r.cap.sinkDestroyed = &retryDestroyed;
    const Outcome resumed = run_export(r, 36, 30, false, 10);
    AUREA_CHECK(resumed.finished); AUREA_CHECK_EQ(resumed.p.result, Errc::Ok);
    AUREA_CHECK(retryDestroyed.load());
    AUREA_CHECK(backend->mappedBuffers.size() < twoHungBuffers);
    if (second.entered.load()) release_hung(second.release, second.returned);
    AUREA_CHECK(second.returned.load()); AUREA_CHECK(secondDestroyed.load());
    r.cap.writes = 0; r.cap.hashes.clear(); r.cap.pts.clear(); r.cap.finished = r.cap.aborted = false;
    const Outcome clean = run_export(r, 36, 30, false, 10);
    AUREA_CHECK(clean.finished); AUREA_CHECK_EQ(clean.p.result, Errc::Ok);
    AUREA_CHECK_EQ(backend->mappedBuffers.size(), baseBuffers);
}

AUREA_TEST(ExportWatchdog, NativeFailureDetailSurvivesAbandonedCloseAndLateDestruction) {
    SyntheticConfig cfg; cfg.width = 64; cfg.height = 36;
    auto* backend = new MockBackend(); backend->mapBuffers = true;
    Rig r(cfg, 30, 3, 1, nullptr, backend, 300); AUREA_CHECK(r.ok); if (!r.ok) return;
    r.comp()->layer(r.video_layer())->visible = false;
    HungGate gate; gate.arm(r.cap); r.cap.hangAbort = true;
    constexpr char expected[] = "native encoder rejected exact picture; preserve original detail";
    std::snprintf(r.cap.platformDiagnostic, sizeof(r.cap.platformDiagnostic), "%s", expected);
    r.cap.writeFailure = Status{Errc::IoError, r.cap.platformDiagnostic};
    r.cap.invalidateDiagnosticOnDestroy = true;
    std::atomic<bool> destroyed{false}; r.cap.sinkDestroyed = &destroyed;
    const Outcome failed = run_export(r, 36, 30, false, 10);
    AUREA_CHECK(failed.finished); AUREA_CHECK(gate.entered.load());
    AUREA_CHECK_EQ(failed.p.result, Errc::IoError);
    AUREA_CHECK(std::strcmp(failed.p.message, expected) == 0);
    if (gate.entered.load()) release_hung(gate.release, gate.returned);
    AUREA_CHECK(gate.returned.load()); AUREA_CHECK(destroyed.load());
    AUREA_CHECK(std::strcmp(r.cap.platformDiagnostic, "diagnostico expirado") == 0);
    AUREA_CHECK_EQ(r.e.export_progress().result, Errc::IoError);
    AUREA_CHECK(std::strcmp(r.e.export_progress().message, expected) == 0);
}

// Exercise the complete serial export session, not just the codec recipe:
// frozen project, render/readback, native sink boundary, final validation and
// staged publication must still work when the compatibility retry is selected.
AUREA_TEST(OfflineExportSerialGpu, SafeModeTwoPublishesValidatedFramesUsingRequestedCodecAndBitrate) {
    if (!gpu_ok()) { std::printf("(sem GPU Vulkan: pulado) "); return; }
    const std::string source = "aurea_serial_retry_source.bin";
    struct Cleanup {
        std::string source;
        ~Cleanup() {
            fileio::remove_file(source);
            for (const auto& path : {"nao-usado.mp4", "nao-usado.mp4.aurea-export", "nao-usado.mp4.aurea-export.project.aurea"})
                fileio::remove_file(path);
        }
    } cleanup{source};
    const char identity[] = "stable identity for serial compatibility retry";
    AUREA_CHECK(fileio::write_atomic(source, identity, sizeof(identity)).ok());
    SyntheticConfig cfg; cfg.width = 96; cfg.height = 64;
    cfg.pattern = SyntheticPattern::MovingSquare; cfg.audioRate = 44100; cfg.audioSeconds = 1;
    Rig r(cfg, 30, 8, 1, nullptr, nullptr, 0, false, source.c_str(),
          ExportExecutionProfile::Balanced, false, nullptr, nullptr, false, 0, true);
    AUREA_CHECK(r.ok); if (!r.ok) return;
    r.cap.keepFrames = true;
    r.cap.persistFinishMarker = true;
    r.cap.validateCapturedPlanes = true;
    ExportSettings settings;
    settings.height = 64; settings.fps = 30; settings.dither = false;
    settings.videoCodec = ExportCodec::HEVC;
    settings.videoBitrateMbps = 13;
    settings.safeMode = 2;
    const auto result = run_export_with(r, settings, 30);
    AUREA_CHECK(result.finished);
    AUREA_CHECK_EQ(result.p.result, Errc::Ok);
    AUREA_CHECK_EQ(result.p.framesDone, 8u);
    AUREA_CHECK_EQ(result.p.framesTotal, 8u);
    AUREA_CHECK_EQ(result.p.pipelineDepth, 1u);
    AUREA_CHECK_EQ((result.p.flags >> 8) & 15u, 6u);
    AUREA_CHECK((result.p.flags & Engine::kExportSafeMode) != 0);
    AUREA_CHECK_EQ(export_retry_from_flags(result.p.flags), 0u);
    AUREA_CHECK_EQ(r.cap.video.codec, ExportCodec::HEVC);
    AUREA_CHECK_EQ(r.cap.video.profile, kExportProfileBaseline);
    AUREA_CHECK(r.cap.video.preferSoftware);
    AUREA_CHECK_EQ(r.cap.video.bitrateBps, 13'000'000u);
    AUREA_CHECK(r.cap.video.validateBeforePublish);
    AUREA_CHECK(r.cap.finished);
    AUREA_CHECK_EQ(r.cap.validationCalls, 1u);
    AUREA_CHECK_EQ(r.cap.frames.size(), usize{8});
    if (r.cap.frames.size() == 8) AUREA_CHECK(r.cap.frames.front() != r.cap.frames.back());
    AUREA_CHECK(r.cap.ptsMonotonic && r.cap.audioContiguous);
    AUREA_CHECK_EQ(r.cap.pts.size(), usize{8});
    if (r.cap.pts.size() == 8) {
        AUREA_CHECK_EQ(r.cap.pts.front(), i64{0});
        AUREA_CHECK_EQ(r.cap.pts.back(), i64{233333});
    }
    AUREA_CHECK_EQ(r.cap.audioFrames, i64{12800});
    std::vector<u8> published;
    AUREA_CHECK(fileio::read_all("nao-usado.mp4", published, 1024));
    const char marker[] = "finished benchmark sink";
    AUREA_CHECK_EQ(published.size(), sizeof(marker));
    if (published.size() == sizeof(marker)) AUREA_CHECK(std::memcmp(published.data(), marker, sizeof(marker)) == 0);
}
