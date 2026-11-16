// Included by test_export.cpp. Faults stay at the existing backend / decoder
// boundary; these checks exercise the actual offline renderer accounting.
namespace export_frame_measurements {
struct Fixture {
    MockBackend backend;
    EffectRegistry effects;
    Project project;
    OfflineFrameRenderer offline;
    OffscreenTarget target;
    CompositionId composition;
    bool ready = false;

    Fixture() {
        if (!backend.initialize({}).ok()) return;
        register_builtin_effects(effects);
        composition = project.timeline().create_composition("measurements", 64, 36, 30);
        TextureDesc desc;
        desc.width = 64; desc.height = 36;
        desc.format = SurfaceFormat::RGBA16F;
        desc.sampled = desc.renderTarget = true;
        const auto texture = backend.create_texture(desc);
        if (!texture.ok()) return;
        target.texture = *texture; target.width = 64; target.height = 36;
        ready = offline.initialize(backend, effects, nullptr, nullptr, 16ull << 20, 1'000'000'000ull).ok();
    }
    ~Fixture() {
        offline.shutdown();
        if (target.texture.valid()) backend.destroy_texture(target.texture);
        backend.shutdown();
    }
};
}

AUREA_TEST(ExportFrameMeasurements, AcquireWaitIsSeparatedFromPrepareAndRecording) {
    export_frame_measurements::Fixture fixture;
    AUREA_CHECK(fixture.ready); if (!fixture.ready) return;
    fixture.backend.beforeBeginOffscreenFrame = [] {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    };
    RenderSettings settings;
    u64 submitted = 0;
    AUREA_CHECK(fixture.offline.render(fixture.project, fixture.composition, FrameIndex{0}, fixture.target,
                                      nullptr, nullptr, settings, submitted).ok());
    const auto measured = fixture.offline.last_measurements();
    AUREA_CHECK_EQ(measured.attempts, 1u);
    AUREA_CHECK_EQ(measured.recordAttempts, 1u);
    AUREA_CHECK(measured.prepareNs > 0 && measured.recordNs > 0);
    AUREA_CHECK(measured.fenceWaitNs >= 15'000'000ull);
    AUREA_CHECK_EQ(measured.mediaWaitNs, 0ull);
    AUREA_CHECK_EQ(measured.resourceWaitNs, 0ull);
    AUREA_CHECK(measured.elapsedNs >= measured.prepareNs + measured.recordNs + measured.fenceWaitNs);
}

AUREA_TEST(ExportFrameMeasurements, FailedAndCancelledCallsResetPreviousFrameMeasurements) {
    export_frame_measurements::Fixture fixture;
    AUREA_CHECK(fixture.ready); if (!fixture.ready) return;
    fixture.backend.beforeEndFrame = [] {
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
        return Status{Errc::DeviceLost};
    };
    RenderSettings settings;
    u64 submitted = 0;
    const Status failed = fixture.offline.render(fixture.project, fixture.composition, FrameIndex{0}, fixture.target,
                                                nullptr, nullptr, settings, submitted);
    AUREA_CHECK_EQ(failed.code(), Errc::DeviceLost);
    const auto measured = fixture.offline.last_measurements();
    AUREA_CHECK_EQ(measured.attempts, 1u);
    AUREA_CHECK_EQ(measured.recordAttempts, 1u);
    AUREA_CHECK(measured.elapsedNs >= 1'000'000ull && measured.recordNs > 0);
    std::atomic<bool> cancelled{true};
    settings.cancelFlag = &cancelled;
    const Status stopped = fixture.offline.render(fixture.project, fixture.composition, FrameIndex{0}, fixture.target,
                                                 nullptr, nullptr, settings, submitted);
    AUREA_CHECK_EQ(stopped.code(), Errc::Cancelled);
    const auto reset = fixture.offline.last_measurements();
    AUREA_CHECK_EQ(reset.attempts, 0u);
    AUREA_CHECK_EQ(reset.recordAttempts, 0u);
    AUREA_CHECK_EQ(reset.prepareNs + reset.recordNs + reset.mediaWaitNs + reset.fenceWaitNs, 0ull);
    AUREA_CHECK_EQ(submitted, 0ull);
}

AUREA_TEST(ExportFrameMeasurements, DelayedSourcePublishesNonzeroDecodeWaitProgress) {
    SyntheticConfig config;
    config.width = 64; config.height = 36; config.frameCount = 6;
    config.decodeCostUs = 80'000;
    SyntheticFactory factory(config);
    auto* backend = new MockBackend(); backend->mapBuffers = true;
    Rig rig(config, 30, 3, 2, &factory, backend);
    AUREA_CHECK(rig.ok); if (!rig.ok) return;
    const Outcome outcome = run_export(rig, 36, 30, false, 15);
    AUREA_CHECK(outcome.finished);
    AUREA_CHECK_EQ(outcome.p.result, Errc::Ok);
    AUREA_CHECK(outcome.p.decodeWaitMs > 0);
    AUREA_CHECK(outcome.p.renderMs > 0);
    AUREA_CHECK(rig.cap.finished && rig.cap.hashes.size() == 3);
}
