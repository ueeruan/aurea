// =============================================================================
//  Export LENTO não é export quebrado (beta 0.0.2/0.0.3: "não consigo exportar
//  o projeto", com vídeo). Decoder andando um GOP longo, quadro de GPU pesado e
//  encoder lento terminam; só a falta de TRABALHO (nada anda) para o export.
//  As regras ficam em export/ExportWatchdog.hpp; o sinal de vida do decoder é
//  MediaManager::decode_work (quadros decodificados, inclusive os descartados
//  a caminho do alvo depois de um seek).
//
//  Incluído por test_export.cpp depois de ExportWatchdog.inl e CutFrameGpu.inl
//  (usa Rig, Outcome, run_export, run_export_with e cut_frame_test::set_range).
// =============================================================================

AUREA_TEST(ExportWatchdogRules, MissingFrameWaitFollowsDecoderWorkNotTheFallbackStreak) {
    // O teto curto dos aproximados nunca vale para a camada SEM imagem.
    const ExportSourceWait streak = export_source_wait(kExportFallbackStreak);
    const ExportSourceWait missing = export_missing_source_wait();
    AUREA_CHECK_EQ(missing.patienceNs, 4'000'000'000ull);
    AUREA_CHECK_EQ(missing.hardCapNs, 120'000'000'000ull);
    AUREA_CHECK(missing.hardCapNs > streak.hardCapNs);
    AUREA_CHECK(missing.hardCapNs > export_source_wait(0).hardCapNs);
    // Decoder que trabalha 1×/s por 30 s: o prazo nunca vence...
    const u64 s = 1'000'000'000ull;
    ProgressDeadline d(0, missing.patienceNs, missing.hardCapNs);
    for (u64 t = 1; t <= 30; ++t) {
        AUREA_CHECK(!d.expired(t * s));
        d.progress(t * s);
    }
    // ...e vence 4 s depois do último trabalho (falha de mídia de verdade).
    AUREA_CHECK(!d.expired(34 * s));
    AUREA_CHECK(d.expired(34 * s + 1));
    // Recurso de GPU: o mesmo princípio, teto menor.
    const ExportSourceWait res = export_resource_wait();
    AUREA_CHECK_EQ(res.patienceNs, 4'000'000'000ull);
    AUREA_CHECK(res.hardCapNs >= 20'000'000'000ull && res.hardCapNs <= missing.hardCapNs);
    // Sinal de vida da tela: só com trabalho novo e no máximo 1×/s.
    AUREA_CHECK(!export_liveness_due(5 * s, 4 * s, 10, 10));           // nada novo
    AUREA_CHECK(!export_liveness_due(4 * s + s / 2, 4 * s, 11, 10));   // cedo demais
    AUREA_CHECK(export_liveness_due(5 * s, 4 * s, 11, 10));
}

/// Clipe que começa no meio de um GOP LONGO num decoder lento: o 1º quadro só
/// sai depois de decodificar (e descartar) 150 quadros a 40 ms = 6 s, sem
/// NENHUM quadro entregue nesse tempo. Antes, 4 s sem quadro entregue = "mídia
/// de vídeo indisponível" e o export inteiro falhava. Agora o trabalho do
/// decoder conta: termina com os quadros EXATOS, sem o aviso de aproximados.
AUREA_TEST(ExportSlowMedia, LongGopSeekOnASlowDecoderFinishesWithExactFrames) {
    if (!gpu_ok()) { std::printf("(sem GPU Vulkan: pulado) "); return; }
    SyntheticConfig cfg;
    cfg.width = 64; cfg.height = 36;
    cfg.pattern = SyntheticPattern::FrameGray;
    cfg.frameCount = 200;
    cfg.gop = 200;              // um keyframe só
    cfg.decodeCostUs = 40'000;  // decoder de entrada com vídeo pesado
    constexpr i64 kOffset = 150, kFrames = 4;
    SyntheticFactory factory(cfg);
    Rig r(cfg, 30.0, kFrames, 3, &factory);
    AUREA_CHECK(r.ok); if (!r.ok) return;
    AUREA_CHECK(cut_frame_test::set_range(r.e, r.video_layer(), 0, kFrames, kOffset));
    AUREA_CHECK(cut_frame_test::set_duration(r.e, kFrames));
    r.cap.keepFrames = true;
    const Outcome o = run_export(r, 36, 30, false, 60);
    std::printf("GOP longo: %.1f s, resultado %d ", o.seconds, static_cast<int>(o.p.result));
    AUREA_CHECK(o.finished);
    AUREA_CHECK_EQ(o.p.result, Errc::Ok);
    AUREA_CHECK(r.cap.finished && !r.cap.aborted);
    AUREA_CHECK_EQ(r.cap.frames.size(), static_cast<usize>(kFrames));
    AUREA_CHECK_EQ(o.p.flags & Engine::kExportFrameFallback, 0u);
    // A espera passou do prazo antigo de 4 s sem quadro entregue.
    AUREA_CHECK(o.seconds > 4.5);
    if (r.cap.frames.size() == static_cast<usize>(kFrames)) {
        AUREA_CHECK(r.cap.frames[0] != r.cap.frames[1]);
        AUREA_CHECK(r.cap.frames[1] != r.cap.frames[2]);
    }
}

/// O decoder morre no meio (sem trabalho nenhum): continua sendo falha de
/// mídia honesta e rápida — o sinal de vida não esconde decoder parado.
AUREA_TEST(ExportSlowMedia, DeadDecoderStillFailsAsMediaQuickly) {
    if (!gpu_ok()) { std::printf("(sem GPU Vulkan: pulado) "); return; }
    SyntheticConfig cfg;
    cfg.width = 64; cfg.height = 36;
    cfg.pattern = SyntheticPattern::FrameGray;
    // Nada decodifica a partir do quadro 0: nenhuma imagem, nenhum trabalho.
    StallingFactory factory(cfg, 0);
    Rig r(cfg, 30.0, 3, 3, &factory);
    AUREA_CHECK(r.ok); if (!r.ok) return;
    const Outcome o = run_export(r, 36, 30, false, 30);
    std::printf("decoder morto: %.1f s ", o.seconds);
    AUREA_CHECK(o.finished);
    AUREA_CHECK_EQ(o.p.result, Errc::DecodeFailed);
    AUREA_CHECK_EQ(o.p.failure, static_cast<u32>(ExportFailure::Media));
    AUREA_CHECK(r.cap.aborted && !r.cap.finished);
    AUREA_CHECK(o.seconds < 9.0);
}

/// Quadro de GPU pesado (3D denso, motion blur de 64 amostras num aparelho
/// fraco): o fence demora 3 s. Não é travamento — o export espera e termina.
AUREA_TEST(ExportSlowMedia, SlowGpuFrameFinishes) {
    SyntheticConfig cfg; cfg.width = 64; cfg.height = 36;
    // Antes do Rig: o backend (dono do callback) morre depois deles.
    std::atomic<bool> armed{false};
    std::atomic<u64> slowFrame{0};
    std::atomic<i64> slowUntilNs{0};
    auto* backend = new MockBackend(); backend->mapBuffers = true;
    Rig r(cfg, 30, 4, 2, nullptr, backend, 1500); AUREA_CHECK(r.ok); if (!r.ok) return;
    r.comp()->layer(r.video_layer())->visible = false;
    AUREA_CHECK(r.e.add_text("Lento").ok());
    backend->beforeWaitFrame = [&](u64 frame, u64) -> Status {
        // O 1º fence esperado depois do início do export segura 3 s (em
        // fatias, como a GPU real devolve Timeout a cada fatia).
        u64 expected = 0;
        if (armed.load() && slowFrame.compare_exchange_strong(expected, frame))
            slowUntilNs.store(static_cast<i64>(monotonic_ns() + 3'000'000'000ull));
        if (frame == slowFrame.load() && static_cast<i64>(monotonic_ns()) < slowUntilNs.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            return Errc::Timeout;
        }
        return OkStatus;
    };
    ExportSettings s; s.height = 36; s.fps = 30; s.dither = false;
    armed.store(true);
    const Outcome o = run_export_with(r, s, 30);
    std::printf("GPU lenta: %.1f s ", o.seconds);
    AUREA_CHECK(o.finished);
    AUREA_CHECK_EQ(o.p.result, Errc::Ok);
    AUREA_CHECK(r.cap.finished && !r.cap.aborted);
    AUREA_CHECK_EQ(r.cap.hashes.size(), usize{4});
    AUREA_CHECK(o.seconds > 2.5);
}

/// Encoder lento que devolve cada chamada (400 ms por quadro; o export inteiro
/// passa do limite de worker travado de 1,5 s deste teste): só uma CHAMADA
/// presa conta como travada — o export lento termina e o arquivo fecha.
AUREA_TEST(ExportSlowMedia, SlowEncoderFinishesWhileItKeepsReturning) {
    SyntheticConfig cfg; cfg.width = 64; cfg.height = 36;
    auto* backend = new MockBackend(); backend->mapBuffers = true;
    Rig r(cfg, 30, 6, 2, nullptr, backend, 1500); AUREA_CHECK(r.ok); if (!r.ok) return;
    r.comp()->layer(r.video_layer())->visible = false;
    r.cap.encodeUs = 400'000;   // cada write_video leva 400 ms (abaixo do limite)
    ExportSettings s; s.height = 36; s.fps = 30; s.dither = false;
    const Outcome o = run_export_with(r, s, 30);
    std::printf("encoder lento: %.1f s ", o.seconds);
    AUREA_CHECK(o.finished);
    AUREA_CHECK_EQ(o.p.result, Errc::Ok);
    AUREA_CHECK(r.cap.finished && !r.cap.aborted);
    AUREA_CHECK_EQ(r.cap.hashes.size(), usize{6});
    AUREA_CHECK(o.seconds > 1.5);
}
