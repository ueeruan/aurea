// surface-direct (docs/architecture/EXPORT_SURFACE_DIRETO_2026-10-15.md): o que
// o host prova sem encoder do aparelho. Desligado por padrão; ligado, cada slot
// do export tem um alvo R'G'B' com AHardwareBuffer que chega ao sink junto do
// quadro; depois que o sink confirma o degrau, o NV12 e o readback param. Sem
// o alvo, o export segue no NV12 de sempre. O degrau em si (EGLImage, Surface,
// eglPresentationTimeANDROID) só existe no Android.
namespace {
struct DirectSurfaceSwitch {
    explicit DirectSurfaceSwitch(bool on) noexcept { gRigDirectSurface = on; }
    ~DirectSurfaceSwitch() { gRigDirectSurface = false; }
};
MockBackend* direct_backend(bool sharedTargets) {
    auto* backend = new MockBackend();
    backend->mapBuffers = true;
    backend->caps.externalMemoryHardwareBuffer = true;
    backend->sharedTargetsSupported = sharedTargets;
    return backend;
}
bool exact_clock(const BenchCapture& cap, u32 frames, f64 fps) {
    if (cap.pts.size() != frames || !cap.ptsMonotonic) return false;
    for (u32 i = 0; i < frames; ++i)
        if (std::llabs(cap.pts[i] - std::llround(i * 1'000'000.0 / fps)) > 1) return false;
    return true;
}
} // namespace

AUREA_TEST(ExportDirectSurface, OffByDefaultCreatesNoSharedTargets) {
    SyntheticConfig cfg; cfg.width = 64; cfg.height = 36;
    auto* backend = direct_backend(true);
    Rig r(cfg, 30, 6, 2, nullptr, backend); AUREA_CHECK(r.ok); if (!r.ok) return;
    const Outcome o = run_export(r, 36, 30, false, 30);
    AUREA_CHECK(o.finished); AUREA_CHECK_EQ(o.p.result, Errc::Ok);
    AUREA_CHECK(!r.cap.video.directSurface);
    AUREA_CHECK_EQ(backend->sharedTargetsCreated, 0u);
    AUREA_CHECK_EQ(r.cap.directFrames.size(), usize{6});
    for (void* buffer : r.cap.directFrames) AUREA_CHECK(buffer == nullptr);
    AUREA_CHECK(exact_clock(r.cap, 6, 30));
}

AUREA_TEST(ExportDirectSurface, EverySlotBufferReachesTheSinkAndNv12StopsAfterConfirmation) {
    DirectSurfaceSwitch on(true);
    SyntheticConfig cfg; cfg.width = 64; cfg.height = 36;
    auto* backend = direct_backend(true);
    Rig r(cfg, 30, 12, 2, nullptr, backend); AUREA_CHECK(r.ok); if (!r.ok) return;
    r.cap.confirmDirectAfter = 3;
    const Outcome o = run_export(r, 36, 30, false, 30);
    AUREA_CHECK(o.finished); AUREA_CHECK_EQ(o.p.result, Errc::Ok);
    AUREA_CHECK(r.cap.video.directSurface);
    AUREA_CHECK(backend->sharedTargetsCreated >= 1u);
    AUREA_CHECK_EQ(r.cap.directFrames.size(), usize{12});
    for (void* buffer : r.cap.directFrames) {
        AUREA_CHECK(buffer != nullptr);
        AUREA_CHECK(std::find(backend->sharedBuffers.begin(), backend->sharedBuffers.end(), buffer)
                    != backend->sharedBuffers.end());
    }
    AUREA_CHECK(exact_clock(r.cap, 12, 30));
    // 2 cópias (Y e CbCr) por quadro só até a confirmação (mais os quadros em
    // voo); depois, nenhuma: o caminho direto não lê o quadro de volta.
    const u32 copies = backend->bufferCopies.load();
    std::printf("\n    surface-direct: %u alvos, %u copias de readback em 12 quadros\n",
                backend->sharedTargetsCreated, copies);
    AUREA_CHECK(copies > 0u && copies < 2u * 12u);
    // Os alvos compartilhados saem com o export (o motor é o dono).
    for (const u64 id : backend->sharedTargetIds) AUREA_CHECK(!backend->textureAlive[id - 1]);
}

AUREA_TEST(ExportDirectSurface, MissingSharedTargetKeepsTheNv12PathUnchanged) {
    DirectSurfaceSwitch on(true);
    SyntheticConfig cfg; cfg.width = 64; cfg.height = 36;
    auto* backend = direct_backend(false);
    Rig r(cfg, 30, 6, 2, nullptr, backend); AUREA_CHECK(r.ok); if (!r.ok) return;
    r.cap.confirmDirectAfter = 1;   // mesmo que o sink "confirme", sem buffer o NV12 continua
    const Outcome o = run_export(r, 36, 30, false, 30);
    AUREA_CHECK(o.finished); AUREA_CHECK_EQ(o.p.result, Errc::Ok);
    AUREA_CHECK_EQ(backend->sharedTargetsCreated, 0u);
    for (void* buffer : r.cap.directFrames) AUREA_CHECK(buffer == nullptr);
    AUREA_CHECK(exact_clock(r.cap, 6, 30));
    AUREA_CHECK(backend->bufferCopies.load() >= 2u * 6u);
}
