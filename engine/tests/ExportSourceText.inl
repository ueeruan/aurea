// =============================================================================
//  AE animation (2026-11-11): keys do texto-fonte no export, quadro a quadro,
//  com a mesma regra da prévia (incluído no fim de test_export.cpp).
// =============================================================================

AUREA_TEST(Export, SourceTextKeysHoldInExportedFramesLikeThePreview) {
    if (!gpu_ok()) { std::printf("(sem GPU Vulkan: pulado) "); return; }
    SyntheticConfig cfg; cfg.width = 320; cfg.height = 180;
    Rig r(cfg, 30, 12, 2); AUREA_CHECK(r.ok); if (!r.ok) return;
    r.comp()->layer(r.video_layer())->visible = false;
    auto id = r.e.add_text("AB");
    if (!id.ok()) { std::printf("(sem fonte no host: pulado) "); return; }
    r.comp()->layer(LayerId::unpack(*id))->end = FrameIndex{12};
    auto seek = [&](i64 f) {
        Command s; s.type = CommandType::PlaybackSeek; s.seek.time = tick_at(FrameIndex{f}, 30.0);
        AUREA_CHECK(r.e.apply_command(s).ok());
    };
    seek(0);
    AUREA_CHECK_EQ(r.e.add_text_source_key(*id), i64{0});
    seek(6);
    Command edit; edit.type = CommandType::TextSetContent; edit.layer_ref.layer = LayerId::unpack(*id);
    AUREA_CHECK(r.e.apply_command(edit, "XYZW").ok());   // com keys: vira o key do quadro 6
    Command duration; duration.type = CommandType::CompositionSetDuration;
    duration.comp_duration.comp = r.e.project()->timeline().current(); duration.comp_duration.duration = FrameIndex{12};
    AUREA_CHECK(r.e.apply_command(duration).ok());
    // Prévia: o quadro 3 mostra "AB", o 8 mostra "XYZW".
    std::vector<u8> p3, p8, pAll;
    u32 w = 0, h = 0;
    seek(3); AUREA_CHECK(r.e.capture_frame_rgba(96, p3, w, h).ok());
    seek(8); AUREA_CHECK(r.e.capture_frame_rgba(96, p8, w, h).ok());
    AUREA_CHECK(p3 != p8);
    r.cap.keepFrames = true;
    auto result = run_export(r, 180, 30, false, 30);
    AUREA_CHECK(result.finished && result.p.result == Errc::Ok);
    AUREA_CHECK_EQ(r.cap.hashes.size(), 12u);
    if (r.cap.hashes.size() != 12) return;
    const std::vector<u64> keyed = r.cap.hashes;
    // Congelado: 0..5 iguais, 6..11 iguais, e o texto troca no quadro 6.
    for (u32 i = 1; i < 6; ++i) AUREA_CHECK_EQ(keyed[i], keyed[0]);
    for (u32 i = 7; i < 12; ++i) AUREA_CHECK_EQ(keyed[i], keyed[6]);
    AUREA_CHECK(keyed[0] != keyed[6]);
    // O mesmo key nos dois quadros: o export inteiro é o quadro 6 de antes —
    // e a prévia do quadro 3 passa a ser a do quadro 8 de antes.
    AUREA_CHECK(r.e.set_text_source_key(*id, 0, "XYZW"));
    seek(3); AUREA_CHECK(r.e.capture_frame_rgba(96, pAll, w, h).ok());
    AUREA_CHECK(pAll == p8);
    r.cap = BenchCapture{}; r.cap.keepFrames = true;
    result = run_export(r, 180, 30, false, 30);
    AUREA_CHECK(result.finished && result.p.result == Errc::Ok);
    AUREA_CHECK_EQ(r.cap.hashes.size(), 12u);
    for (const u64 hash : r.cap.hashes) AUREA_CHECK_EQ(hash, keyed[6]);
    // Salvar e abrir de novo não muda nenhum quadro.
    AUREA_CHECK(r.e.set_text_source_key(*id, 0, "AB"));
    const std::string path = std::string(std::getenv("TEMP") ? std::getenv("TEMP") : ".") + "/aurea_source_text_export.aurea";
    AUREA_CHECK(r.e.save_project(path.c_str()).ok()); AUREA_CHECK(r.e.load_project(path.c_str()).ok());
    r.cap = BenchCapture{}; r.cap.keepFrames = true;
    result = run_export(r, 180, 30, false, 30);
    AUREA_CHECK(result.finished && result.p.result == Errc::Ok);
    AUREA_CHECK(r.cap.hashes == keyed);
    std::remove(path.c_str());
    std::printf("texto-fonte no export: troca exata no quadro 6, congelado, igual à prévia e após reabrir; ");
}
