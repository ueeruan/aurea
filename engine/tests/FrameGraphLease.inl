// Lifecycle assertions use the recording backend. Native queue/fence and
// exact-pixel checks remain in the separately labeled GPU fixtures.
AUREA_TEST(FrameGraphSegmentsLease, LeaseUsesOneAcquisitionAndKeepsOriginalExtentAndRotation) {
    MockBackend backend; backend.surfaceAttached = true;
    backend.supportsFrameDiscard = backend.supportsPresentableLease = true;
    backend.surfaceWidth = 320; backend.surfaceHeight = 180;
    backend.surfaceRotation = SurfaceRotation::Rotate90;
    FrameBegin original;
    AUREA_CHECK(backend.begin_frame(original).ok());
    AUREA_CHECK(backend.begin_presentable_frame_lease().ok());
    FrameBegin unexpected;
    AUREA_CHECK_EQ(backend.begin_frame(unexpected).code(), Errc::Busy);
    AUREA_CHECK(backend.discard_frame().ok());
    for (u32 i = 0; i < 3; ++i) {
        FrameBegin intermediate;
        AUREA_CHECK(backend.begin_offscreen_frame(intermediate).ok());
        AUREA_CHECK(!intermediate.backbuffer.valid());
        AUREA_CHECK(backend.end_frame().ok());
        AUREA_CHECK_EQ(backend.acquires, 1u); AUREA_CHECK_EQ(backend.presents, 0u);
    }
    FrameBegin final;
    AUREA_CHECK(backend.begin_leased_frame(final).ok());
    AUREA_CHECK_EQ(final.backbuffer.id, original.backbuffer.id);
    AUREA_CHECK_EQ(final.backbufferWidth, original.backbufferWidth);
    AUREA_CHECK_EQ(final.backbufferHeight, original.backbufferHeight);
    AUREA_CHECK_EQ(final.rotation, original.rotation);
    AUREA_CHECK(final.frameNumber > original.frameNumber);
    AUREA_CHECK(backend.end_frame().ok());
    backend.end_presentable_frame_lease();
    AUREA_CHECK_EQ(backend.acquires, 1u); AUREA_CHECK_EQ(backend.presents, 1u);
    AUREA_CHECK_EQ(backend.leasedFrames, 1u);
    AUREA_CHECK_EQ(backend.presentableLeases, backend.presentableLeasesEnded);
    AUREA_CHECK(!backend.presentableLeaseActive);
}

AUREA_TEST(FrameGraphSegmentsLease, ResizeDetachAndRotationInvalidateFinalLeasedFrame) {
    for (u32 change = 0; change < 3; ++change) {
        MockBackend backend; backend.surfaceAttached = true;
        backend.supportsFrameDiscard = backend.supportsPresentableLease = true;
        FrameBegin original; AUREA_CHECK(backend.begin_frame(original).ok());
        AUREA_CHECK(backend.begin_presentable_frame_lease().ok());
        AUREA_CHECK(backend.discard_frame().ok());
        if (change == 0) AUREA_CHECK(backend.resize_surface(640, 360).ok());
        else if (change == 1) backend.detach_surface();
        else backend.surfaceRotation = SurfaceRotation::Rotate270;
        FrameBegin stale;
        AUREA_CHECK_EQ(backend.begin_leased_frame(stale).code(), Errc::SurfaceLost);
        AUREA_CHECK(!stale.backbuffer.valid());
        AUREA_CHECK_EQ(backend.acquires, 1u); AUREA_CHECK_EQ(backend.presents, 0u);
        AUREA_CHECK_EQ(backend.leasedFrames, 0u);
        backend.end_presentable_frame_lease();
        AUREA_CHECK(!backend.presentableLeaseActive);
        AUREA_CHECK_EQ(backend.presentableLeases, backend.presentableLeasesEnded);
    }
}

namespace {
struct PreviewLeaseFixture {
    // The authored source stays 1024x1024; its Gaussian output is cropped
    // to the visible 320x180 canvas. H adds the exact 4px sampling halo.
    // Whole compilation retains all four incompatible physical descriptors:
    // 1024x1024 + 328x188 + 320x180 + transfer-src 320x180, RGBA16F.
    // A fenced one-pass segment retains at most source+H. Choose a limit
    // BETWEEN these measured/derived peaks, without changing the picture.
    static constexpr u64 wholeBytes = 9'803'520;
    static constexpr u64 segmentPeakBytes = 8'881'920;
    static constexpr u64 pressureLimit = 9ull << 20;
    static_assert(segmentPeakBytes < pressureLimit && pressureLimit < wholeBytes);
    RenderFixture f;
    RenderSettings settings;
    FrameSnapshot snapshot;
    LayerId source;
    PreviewLeaseFixture() {
        auto project = Project::create_new(320, 180, 30., "leased preview lifecycle");
        AUREA_CHECK(project.ok());
        if (project.ok()) f.project = std::move(*project);
        f.comp = f.project.timeline().composition(f.project.timeline().root());
        f.backend.surfaceAttached = true;
        f.backend.surfaceWidth = 320; f.backend.surfaceHeight = 180;
        f.backend.surfaceRotation = SurfaceRotation::Rotate90;
        f.backend.mapBuffers = true;
        f.backend.supportsFrameDiscard = f.backend.supportsPresentableLease = true;
        source = f.solid("same authored ellipse", Vec4{.3f, .6f, .9f, .65f}, 160, 90);
        auto* layer = f.comp->layer(source);
        layer->shape.bounds = Rect{0, 0, 1024, 1024}; layer->shape.shapeType = 1;
        layer->transform.anchor = Vec3{512,512,0};
        auto blur = make_effect(f.effects, effect_keys::kGaussianBlur, 0);
        blur.params[0].constant.v[0] = 3;
        layer->effects.push_back(std::move(blur));
        settings.finalQuality = true; settings.dither = false;
        settings.allowGraphSegments = true;
        settings.previewCacheRevision = 1; settings.mediaGeneration = 1;
        f.renderer.set_transient_cache_budget(256ull << 20);
        prepare();
    }
    ~PreviewLeaseFixture() {
        // A failed assertion must still end the borrowing scope while its
        // settings and snapshot objects are alive.
        f.renderer.cancel_preview_graph();
        f.backend.beforeWaitFrame = {};
        f.backend.wait_idle();
    }
    void prepare(FrameIndex time = FrameIndex{0}) {
        f.renderer.prepare(*f.comp, f.project, time, nullptr, nullptr, nullptr,
            settings, 1, 0, DecodeMode::Still, 1, snapshot);
    }
    Status finish(FrameStats& stats, RenderTimings& timings) {
        Status result = Errc::Busy;
        for (u32 i = 0; i < 1000 && result.code() == Errc::Busy; ++i)
            result = f.renderer.continue_preview_graph(stats, timings);
        return result;
    }
    void unchanged_source() {
        AUREA_CHECK_EQ(settings.previewNumerator, 1u); AUREA_CHECK_EQ(settings.previewDenominator, 1u);
        AUREA_CHECK(settings.finalQuality);
        AUREA_CHECK_EQ(snapshot.compWidth, 320u); AUREA_CHECK_EQ(snapshot.compHeight, 180u);
        AUREA_CHECK_EQ(f.comp->layer(source)->shape.bounds.w, 1024.f);
        AUREA_CHECK_EQ(f.comp->layer(source)->effects[0].params[0].constant.v[0], 3.f);
    }
};
}

AUREA_TEST(FrameGraphSegmentsLease, PendingFencesDoNotPresentAndFinalPassMatchesFullRecording) {
    PreviewLeaseFixture control;
    FrameStats baselineStats; RenderTimings baselineTimings;
    AUREA_CHECK(control.f.renderer.render(control.snapshot, control.settings, nullptr,
        baselineStats, baselineTimings).ok());
    const u32 baselineDraws = control.f.backend.count(MockBackend::Event::Draw);
    AUREA_CHECK_EQ(control.f.renderer.pool_stats().bytes, PreviewLeaseFixture::wholeBytes);
    std::printf("\n    leased preview admission: whole=%llu segment-read-write=%llu limit=%llu\n",
        static_cast<unsigned long long>(control.f.renderer.pool_stats().bytes),
        static_cast<unsigned long long>(PreviewLeaseFixture::segmentPeakBytes),
        static_cast<unsigned long long>(PreviewLeaseFixture::pressureLimit));
    PreviewLeaseFixture fixture;
    auto& backend = fixture.f.backend; auto& renderer = fixture.f.renderer;
    renderer.set_transient_allocation_limit(PreviewLeaseFixture::pressureLimit);
    FrameStats stats; RenderTimings timings;
    const auto first = renderer.render(fixture.snapshot, fixture.settings, nullptr, stats, timings);
    AUREA_CHECK_EQ(first.code(), Errc::Busy);
    if (first.code() != Errc::Busy) return; // Retain the admission failure without cascading unrelated checks.
    AUREA_CHECK(renderer.preview_graph_pending());
    AUREA_CHECK_EQ(backend.acquires, 1u); AUREA_CHECK_EQ(backend.presents, 0u);
    AUREA_CHECK(backend.presentableLeaseActive);
    u64 measuredSegmentPeak = 0;
    // Installed after the partial whole-plan submission: observe each real
    // recovery submission, before its pool resources can be fence-trimmed.
    backend.beforeEndFrame = [&] {
        measuredSegmentPeak = std::max(measuredSegmentPeak, renderer.pool_stats().bytes);
        return OkStatus;
    };
    const auto bytes = renderer.pool_stats().bytes;
    const auto destroyed = backend.texturesDestroyed;
    backend.beforeWaitFrame = [&](u64, u64 timeout) {
        AUREA_CHECK_EQ(timeout, 0ull); return Status{Errc::Timeout};
    };
    for (u32 i = 0; i < 5; ++i) {
        AUREA_CHECK_EQ(renderer.continue_preview_graph(stats, timings).code(), Errc::Busy);
        AUREA_CHECK(renderer.preview_graph_pending());
        AUREA_CHECK_EQ(renderer.pool_stats().bytes, bytes);
        AUREA_CHECK_EQ(backend.texturesDestroyed, destroyed);
        AUREA_CHECK_EQ(backend.presents, 0u);
        AUREA_CHECK(!backend.frameOpen);
    }
    backend.beforeWaitFrame = {};
    AUREA_CHECK(fixture.finish(stats, timings).ok());
    backend.beforeEndFrame = {};
    AUREA_CHECK_EQ(measuredSegmentPeak, PreviewLeaseFixture::segmentPeakBytes);
    AUREA_CHECK(measuredSegmentPeak < PreviewLeaseFixture::pressureLimit);
    AUREA_CHECK(!renderer.preview_graph_pending());
    AUREA_CHECK_EQ(backend.acquires, 1u); AUREA_CHECK_EQ(backend.presents, 1u);
    AUREA_CHECK_EQ(backend.leasedFrames, 1u);
    AUREA_CHECK_EQ(backend.presentableLeases, backend.presentableLeasesEnded);
    AUREA_CHECK(!backend.presentableLeaseActive);
    AUREA_CHECK_EQ(backend.count(MockBackend::Event::Draw), baselineDraws);
    AUREA_CHECK_EQ(stats.passesExecuted, baselineStats.passesExecuted);
    AUREA_CHECK(timings.gpuProfileFragmented && !timings.gpuMeasured);
    fixture.unchanged_source();
}

AUREA_TEST(FrameGraphSegmentsLease, CancelSeekRevisionAndGenerationChangesPreserveFenceGate) {
    enum class Change { Cancel, SeekRevision, Resize, Detach };
    for (const auto change : {Change::Cancel, Change::SeekRevision, Change::Resize, Change::Detach}) {
        PreviewLeaseFixture fixture;
        auto& backend = fixture.f.backend; auto& renderer = fixture.f.renderer;
        renderer.set_transient_allocation_limit(PreviewLeaseFixture::pressureLimit);
        FrameStats stats; RenderTimings timings;
        const auto first = renderer.render(fixture.snapshot, fixture.settings, nullptr, stats, timings);
        AUREA_CHECK_EQ(first.code(), Errc::Busy);
        if (first.code() != Errc::Busy) continue;
        u64 pending = 0;
        backend.beforeWaitFrame = [&](u64 frame, u64 timeout) {
            AUREA_CHECK_EQ(timeout, 0ull);
            if (!renderer.pool_stats().inUse) return OkStatus;
            pending = frame; return Status{Errc::Timeout};
        };
        AUREA_CHECK_EQ(renderer.continue_preview_graph(stats, timings).code(), Errc::Busy);
        AUREA_CHECK(pending > 0); AUREA_CHECK(renderer.pool_stats().inUse > 0);
        const auto destroyed = backend.texturesDestroyed;
        if (change == Change::SeekRevision) {
            auto newer = fixture.settings; ++newer.previewCacheRevision; ++newer.mediaGeneration;
            AUREA_CHECK(!renderer.preview_graph_settings_match(newer));
            fixture.settings = newer;
        }
        // Engine requests cancellation before replacing a snapshot, surface or
        // project revision. Backing remains gated by the actual pending fence.
        renderer.cancel_preview_graph();
        AUREA_CHECK(!renderer.preview_graph_pending()); AUREA_CHECK(!backend.presentableLeaseActive);
        AUREA_CHECK_EQ(backend.presentableLeases, backend.presentableLeasesEnded);
        AUREA_CHECK_EQ(backend.acquires, 1u); AUREA_CHECK_EQ(backend.presents, 0u);
        AUREA_CHECK_EQ(renderer.pool_stats().inUse, 0u);
        AUREA_CHECK_EQ(backend.texturesDestroyed, destroyed);
        backend.beforeWaitFrame = [&](u64 frame, u64 timeout) {
            AUREA_CHECK_EQ(frame, pending); AUREA_CHECK_EQ(timeout, 0ull);
            return Status{Errc::Timeout};
        };
        AUREA_CHECK_EQ(renderer.reclaim_failed_transients(0).code(), Errc::Timeout);
        AUREA_CHECK_EQ(backend.texturesDestroyed, destroyed);
        if (change == Change::Resize) AUREA_CHECK(backend.resize_surface(640, 360).ok());
        if (change == Change::Detach) backend.detach_surface();
        backend.beforeWaitFrame = {};
        AUREA_CHECK(renderer.reclaim_failed_transients(0).ok());
        if (change == Change::Detach) AUREA_CHECK(backend.attach_surface({}).ok());
        fixture.prepare(FrameIndex{30});
        const auto next = renderer.render(fixture.snapshot, fixture.settings, nullptr, stats, timings);
        AUREA_CHECK(next.ok() || next.code() == Errc::Busy);
        if (next.code() == Errc::Busy) AUREA_CHECK(fixture.finish(stats, timings).ok());
        AUREA_CHECK_EQ(backend.acquires, 2u); AUREA_CHECK_EQ(backend.presents, 1u);
        AUREA_CHECK(!renderer.preview_graph_pending()); AUREA_CHECK(!backend.presentableLeaseActive);
        fixture.unchanged_source();
    }
}

AUREA_TEST(FrameGraphSegmentsLease, TimeoutAndDeviceLossNeverPublishPendingPicture) {
    for (const bool deviceLost : {false, true}) {
        PreviewLeaseFixture fixture;
        auto& backend = fixture.f.backend; auto& renderer = fixture.f.renderer;
        renderer.set_transient_allocation_limit(PreviewLeaseFixture::pressureLimit);
        if (!deviceLost) fixture.settings.sceneExposureFenceTimeoutNs = 1;
        FrameStats stats; RenderTimings timings;
        const auto first = renderer.render(fixture.snapshot, fixture.settings, nullptr, stats, timings);
        AUREA_CHECK_EQ(first.code(), Errc::Busy);
        if (first.code() != Errc::Busy) continue;
        AUREA_CHECK(renderer.preview_graph_pending());
        backend.beforeWaitFrame = [&](u64, u64 timeout) {
            AUREA_CHECK_EQ(timeout, 0ull);
            if (deviceLost) backend.deviceLost = true;
            return Status{deviceLost ? Errc::DeviceLost : Errc::Timeout};
        };
        const auto result = renderer.continue_preview_graph(stats, timings);
        AUREA_CHECK_EQ(result.code(), deviceLost ? Errc::DeviceLost : Errc::Timeout);
        AUREA_CHECK(!renderer.preview_graph_pending()); AUREA_CHECK(!backend.presentableLeaseActive);
        AUREA_CHECK_EQ(backend.acquires, 1u); AUREA_CHECK_EQ(backend.presents, 0u);
        AUREA_CHECK_EQ(backend.presentableLeases, backend.presentableLeasesEnded);
        AUREA_CHECK(!backend.frameOpen);
        AUREA_CHECK(timings.gpuProfileFragmented && !timings.gpuMeasured);
        fixture.unchanged_source();
        backend.beforeWaitFrame = {};
    }
}
