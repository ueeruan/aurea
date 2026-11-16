// =============================================================================
//  0.5.3 (Fase B, docs/architecture/EXPORT_053_AUDIT.md) com o motor e a GPU:
//   - P0-7: o degrau de memória (o sistema matou o export anterior) muda só o
//     orçamento — fila, caches, quadros em voo —, nunca a imagem nem o som;
//   - P0-10: pasta de saída que não aceita arquivo falha na partida, antes de
//     renderizar (antes: o render inteiro e a falha só na montagem do MP4).
//  Incluído por test_export.cpp (AUREA_TEST_VULKAN).
// =============================================================================

AUREA_TEST(Export053, MemoryStepChangesTheBudgetButNeverThePixelsOrTheSound) {
    if (!gpu_ok()) { std::printf("(sem GPU Vulkan: pulado) "); return; }
    SyntheticConfig cfg;
    cfg.width = 320;
    cfg.height = 180;
    cfg.pattern = SyntheticPattern::MovingSquare;
    cfg.audioRate = 44100;
    cfg.audioSeconds = 2.0;
    u64 hashes[2]{}, audio[2]{};
    const u32 steps[2] = {0, 3};
    for (int k = 0; k < 2; ++k) {
        Rig r(cfg, 30.0, 30, 0);
        AUREA_CHECK(r.ok);
        if (!r.ok) return;
        r.e.set_export_memory_step(steps[k]);
        const Outcome o = run_export(r, 180, 0.0, true, 120);
        AUREA_CHECK(o.finished);
        AUREA_CHECK_EQ(o.p.result, Errc::Ok);
        AUREA_CHECK_EQ(r.cap.hashes.size(), static_cast<usize>(30));
        AUREA_CHECK(r.cap.ptsMonotonic && r.cap.audioContiguous);
        hashes[k] = combined_hash(r.cap);
        audio[k] = r.cap.audioHash;
    }
    AUREA_CHECK_EQ(hashes[0], hashes[1]);
    AUREA_CHECK_EQ(audio[0], audio[1]);
}

AUREA_TEST(Export053, OutputFolderThatRefusesFilesFailsAtStartBeforeRendering) {
    if (!gpu_ok()) { std::printf("(sem GPU Vulkan: pulado) "); return; }
    SyntheticConfig cfg;
    cfg.width = 160;
    cfg.height = 90;
    Rig r(cfg, 30.0, 30, 0);
    AUREA_CHECK(r.ok);
    if (!r.ok) return;
    ExportSettings s;
    s.height = 90;
    const Status started = r.e.start_export(s, "pasta-que-nao-existe-053/saida.mp4");
    AUREA_CHECK(!started.ok());
    AUREA_CHECK_MSG(started.code() == Errc::IoError, started.message().data());
    AUREA_CHECK(!r.cap.opened);
    AUREA_CHECK(r.cap.hashes.empty());
    // A mesma sessão exporta normalmente depois, numa pasta que aceita arquivo.
    const Outcome o = run_export(r, 90, 0.0, true, 120);
    AUREA_CHECK(o.finished && o.p.result == Errc::Ok);
}
