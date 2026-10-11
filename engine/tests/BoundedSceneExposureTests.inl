// Dense geometry is retained by each submission, not by the complete shutter.
AUREA_TEST(MotionBlur, ImportedAccumulatorLoadsThePreviousSubmissionWithoutAFutureWriterCycle) {
    test::MockBackend backend; TransientTexturePool pool; FrameGraph graph;
    pool.begin_frame(backend, 2);
    TextureDesc desc; desc.width = desc.height = 32; desc.format = SurfaceFormat::RGBA16F;
    desc.renderTarget = desc.sampled = true;
    const auto texture = backend.create_texture(desc); AUREA_CHECK(texture.ok()); if (!texture) return;
    const auto accumulation = graph.import_texture("preserved exposure", *texture, desc);
    const auto first = graph.add_raster_pass("load previous submission", PassStage::Composite,
        accumulation, LoadOp::Load, Vec4{}, [](PassContext&) {});
    const auto second = graph.add_raster_pass("add next sample", PassStage::Composite,
        accumulation, LoadOp::Load, Vec4{}, [](PassContext&) {});
    graph.set_output(accumulation, ResourceState::ShaderRead);
    AUREA_CHECK(graph.compile(pool).ok());
    AUREA_CHECK_EQ(graph.order().size(), 2u);
    if (graph.order().size() == 2) { AUREA_CHECK_EQ(graph.order()[0], first); AUREA_CHECK_EQ(graph.order()[1], second); }
    graph.release(pool); pool.end_frame(); backend.destroy_texture(*texture);
}

AUREA_TEST(MotionBlur, BoundedDenseMorphKeepsEverySampleAndRetiresEachBatch) {
    MorphExposureFixture f(100000);
    f.backend.mapBuffers = true;
    FrameSnapshot snapshot; f.prepare(snapshot, true, true);
    AUREA_CHECK(!f.renderer.take_incomplete());
    AUREA_CHECK_EQ(snapshot.sceneExposureBatches.size(), 1u);
    if (snapshot.sceneExposureBatches.empty() || snapshot.scenes.empty()) return;
    const auto plan = snapshot.sceneExposureBatches[0];
    const auto& samples = snapshot.scenes[0].blurFrames;
    AUREA_CHECK_EQ(samples.size(), 16u);
    AUREA_CHECK(plan.samplesPerBatch > 0 && plan.samplesPerBatch < samples.size());
    if (!plan.samplesPerBatch) return;
    std::vector<f32> expected;
    for (const auto& sample : samples) expected.push_back(sample.instances[0].pose().morphWeights[0][0]);
    std::vector<std::pair<void (*)(void*), void*>> pending;
    std::vector<f32> observed;
    u32 allocations = 0, waits = 0;
    f.backend.beforeCreateBuffer = [&](const BufferDesc& desc) {
        if (desc.debugName && std::strcmp(desc.debugName, "3d-morph") == 0) ++allocations;
        return OkStatus;
    };
    // The backend deliberately does not retire at submit. Only a successful
    // fence wait releases uploads, so accidental pre-fence reuse is exposed.
    f.backend.beforeDeferUntilGpuDone = [&](void (*release)(void*), void* value) {
        pending.emplace_back(release, value);
    };
    f.backend.beforeWaitFrame = [&](u64 frame, u64 timeout) {
        AUREA_CHECK_EQ(frame, f.backend.framesSubmitted);
        AUREA_CHECK(timeout > 0);
        AUREA_CHECK(!pending.empty());
        ++waits;
        for (const auto& [id, bytes] : f.backend.mappedBuffers) {
            if (bytes.size() < 100000u * sizeof(Vec3)) continue;
            f32 weight = 0; std::memcpy(&weight, bytes.data(), sizeof(weight));
            if (std::any_of(expected.begin(), expected.end(), [weight](f32 value) { return std::fabs(value - weight) < 1e-6f; })
                && std::none_of(observed.begin(), observed.end(), [weight](f32 value) { return std::fabs(value - weight) < 1e-6f; }))
                observed.push_back(weight);
        }
        for (const auto& value : pending) value.first(value.second);
        pending.clear();
        return OkStatus;
    };
    RenderSettings settings; settings.finalQuality = settings.boundedSceneExposure = true;
    settings.dither = false;
    TextureDesc desc; desc.width = 320; desc.height = 180; desc.renderTarget = true; desc.format = SurfaceFormat::RGBA16F;
    const auto texture = f.backend.create_texture(desc);
    AUREA_CHECK(texture.ok()); if (!texture) return;
    OffscreenTarget target{*texture, 320, 180}; FrameStats stats; RenderTimings timings;
    const auto rendered = f.renderer.render(snapshot, settings, &target, stats, timings);
    if (!rendered.ok()) std::printf("\n    bounded render: %d %.*s (batches=%u waits=%u)\n", rendered.raw(),
        static_cast<int>(rendered.detail().size()), rendered.detail().data(), timings.sceneExposureBatches, waits);
    AUREA_CHECK(rendered.ok());
    AUREA_CHECK(!f.renderer.take_incomplete());
    AUREA_CHECK_EQ(waits, (16u + plan.samplesPerBatch - 1) / plan.samplesPerBatch);
    AUREA_CHECK_EQ(timings.sceneExposureBatches, waits);
    AUREA_CHECK_EQ(allocations, plan.samplesPerBatch);
    AUREA_CHECK_EQ(observed.size(), expected.size());
    AUREA_CHECK(pending.empty());
    AUREA_CHECK_EQ(f.backend.framesSubmitted, waits + 1u); // Final composition after all batches.
    for (const auto& value : pending) value.first(value.second);
    pending.clear();
    f.backend.beforeWaitFrame = {}; f.backend.beforeDeferUntilGpuDone = {};
    f.backend.destroy_texture(*texture);
}

AUREA_TEST(MotionBlur, BoundedExposureStopsOnFenceTimeoutWithoutReusingUploads) {
    MorphExposureFixture f(100000); f.backend.mapBuffers = true;
    FrameSnapshot snapshot; f.prepare(snapshot, true, true);
    AUREA_CHECK(!f.renderer.take_incomplete());
    if (snapshot.sceneExposureBatches.empty()) return;
    std::vector<std::pair<void (*)(void*), void*>> pending;
    f.backend.beforeDeferUntilGpuDone = [&](void (*release)(void*), void* value) { pending.emplace_back(release, value); };
    u32 waits = 0;
    f.backend.beforeWaitFrame = [&](u64, u64) { ++waits; return Status{Errc::Timeout}; };
    RenderSettings settings; settings.finalQuality = settings.boundedSceneExposure = true;
    settings.sceneExposureFenceTimeoutNs = 1;
    FrameStats stats; RenderTimings timings;
    const Status status = f.renderer.render(snapshot, settings, nullptr, stats, timings);
    AUREA_CHECK_EQ(status.code(), Errc::Timeout);
    AUREA_CHECK(f.renderer.take_incomplete());
    AUREA_CHECK_EQ(waits, 1u); AUREA_CHECK_EQ(f.backend.framesSubmitted, 1u);
    AUREA_CHECK(!pending.empty()); // No retry or early release while the GPU still owns them.
    for (const auto& value : pending) value.first(value.second);
    pending.clear(); f.backend.beforeWaitFrame = {}; f.backend.beforeDeferUntilGpuDone = {};
}

AUREA_TEST(MotionBlur, SubmissionDeviceLossWinsOverEarlierMorphAllocationFailure) {
    // Recovery must receive DeviceLost both after valid recording and when an
    // allocation error already made the scene incomplete. Neither path may
    // recycle command-referenced storage or proceed to another batch.
    for (const bool failMorph : {false, true}) {
        MorphExposureFixture f(100000); f.backend.mapBuffers = true;
        FrameSnapshot snapshot; f.prepare(snapshot, true, true);
        AUREA_CHECK(!f.renderer.take_incomplete());
        AUREA_CHECK(!snapshot.sceneExposureBatches.empty());
        if (snapshot.sceneExposureBatches.empty()) continue;
        std::vector<std::pair<void (*)(void*), void*>> pending;
        f.backend.beforeDeferUntilGpuDone = [&](void (*release)(void*), void* value) {
            pending.emplace_back(release, value);
        };
        u32 allocationFailures = 0, waits = 0;
        f.backend.beforeCreateBuffer = [&](const BufferDesc& desc) {
            if (failMorph && desc.debugName && std::strcmp(desc.debugName, "3d-morph") == 0) {
                ++allocationFailures; return Status{Errc::OutOfMemory};
            }
            return OkStatus;
        };
        f.backend.beforeEndFrame = [&] {
            f.backend.deviceLost = true; return Status{Errc::DeviceLost};
        };
        f.backend.beforeWaitFrame = [&](u64, u64) { ++waits; return OkStatus; };
        RenderSettings settings; settings.finalQuality = settings.boundedSceneExposure = true;
        FrameStats stats; RenderTimings timings;
        const u32 destroyed = f.backend.texturesDestroyed;
        const auto status = f.renderer.render(snapshot, settings, nullptr, stats, timings);
        AUREA_CHECK_EQ(status.code(), Errc::DeviceLost);
        AUREA_CHECK(f.backend.requires_reinitialization());
        AUREA_CHECK(f.renderer.take_incomplete());
        AUREA_CHECK_EQ(waits, 0u); AUREA_CHECK_EQ(f.backend.framesSubmitted, 1u);
        AUREA_CHECK_EQ(timings.sceneExposureBatches, 1u);
        AUREA_CHECK_EQ(allocationFailures > 0, failMorph);
        AUREA_CHECK(!pending.empty()); AUREA_CHECK_EQ(f.backend.texturesDestroyed, destroyed);
        for (const auto& value : pending) value.first(value.second);
        pending.clear(); AUREA_CHECK(f.backend.texturesDestroyed > destroyed);
        f.backend.beforeCreateBuffer = {}; f.backend.beforeEndFrame = {};
        f.backend.beforeWaitFrame = {}; f.backend.beforeDeferUntilGpuDone = {};
    }
}

AUREA_TEST(MotionBlur, BoundedExposureCancellationRetainsGpuOwnedMemory) {
    MorphExposureFixture f(100000); f.backend.mapBuffers = true;
    FrameSnapshot snapshot; f.prepare(snapshot, true, true);
    AUREA_CHECK(!f.renderer.take_incomplete());
    if (snapshot.sceneExposureBatches.empty()) return;
    std::atomic<bool> cancel{false};
    std::vector<std::pair<void (*)(void*), void*>> pending;
    f.backend.beforeDeferUntilGpuDone = [&](void (*release)(void*), void* value) { pending.emplace_back(release, value); };
    u32 waits = 0;
    f.backend.beforeWaitFrame = [&](u64, u64 timeout) {
        ++waits; AUREA_CHECK(timeout <= 100ull * 1000 * 1000);
        cancel.store(true, std::memory_order_release); return Status{Errc::Timeout};
    };
    RenderSettings settings; settings.finalQuality = settings.boundedSceneExposure = true; settings.cancelFlag = &cancel;
    FrameStats stats; RenderTimings timings;
    const u32 destroyed = f.backend.texturesDestroyed;
    AUREA_CHECK_EQ(f.renderer.render(snapshot, settings, nullptr, stats, timings).code(), Errc::Cancelled);
    AUREA_CHECK_EQ(waits, 1u); AUREA_CHECK_EQ(f.backend.framesSubmitted, 1u);
    AUREA_CHECK(!pending.empty()); AUREA_CHECK_EQ(f.backend.texturesDestroyed, destroyed);
    // The deferred retirement tickets own quarantined targets until actual
    // completion. Cancellation cannot destroy them or start another batch.
    for (const auto& value : pending) value.first(value.second);
    pending.clear(); AUREA_CHECK(f.backend.texturesDestroyed > destroyed);
    f.backend.beforeWaitFrame = {}; f.backend.beforeDeferUntilGpuDone = {};
}

AUREA_TEST(MotionBlur, AbortedExposureTexturesCannotBeReacquiredBeforeCompletion) {
    test::MockBackend backend; TransientTexturePool pool;
    std::vector<std::pair<void (*)(void*), void*>> pending;
    backend.beforeDeferUntilGpuDone = [&](void (*release)(void*), void* value) { pending.emplace_back(release, value); };
    TextureDesc desc; desc.width = 32; desc.height = 32; desc.format = SurfaceFormat::RGBA16F;
    desc.renderTarget = desc.sampled = true;
    pool.begin_frame(backend, 1);
    const auto original = pool.acquire(desc); AUREA_CHECK(original.valid());
    pool.retire(original, 1);
    AUREA_CHECK_EQ(backend.texturesDestroyed, 0u);
    pool.begin_frame(backend, 2);
    const auto replacement = pool.acquire(desc); AUREA_CHECK(replacement.valid());
    AUREA_CHECK(replacement != original);
    AUREA_CHECK_EQ(pool.stats().alive, 1u);
    for (const auto& value : pending) value.first(value.second);
    pending.clear(); AUREA_CHECK_EQ(backend.texturesDestroyed, 1u);
    pool.release(replacement); pool.clear(); backend.beforeDeferUntilGpuDone = {};
}

AUREA_TEST(MotionBlur, UnsupportedBoundedExposureKeepsTheOriginalAdmission) {
    MorphExposureFixture f(100000);
    f.wrap_in_parent(1);
    FrameSnapshot snapshot; f.prepare(snapshot, true, true);
    AUREA_CHECK(f.renderer.take_incomplete());
    AUREA_CHECK(snapshot.sceneExposureBatches.empty());
    AUREA_CHECK_EQ(snapshot.nested.size(), 1u);
    if (!snapshot.nested.empty()) {
        AUREA_CHECK(snapshot.nested[0]->sceneExposureBatches.empty());
        AUREA_CHECK(snapshot.nested[0]->scenes[0].blurFrames.empty());
    }
    MorphExposureFixture mixed(100000);
    const auto id = mixed.comp->add_layer(LayerKind::Shape, "scene plane");
    mixed.comp->layer(id)->threeD = true;
    mixed.comp->layer(id)->shape.bounds = Rect{0, 0, 20, 20};
    mixed.comp->layer(id)->transform.position = Vec3{160, 90, 0};
    mixed.prepare(snapshot, true, true);
    AUREA_CHECK(mixed.renderer.take_incomplete());
    AUREA_CHECK(snapshot.sceneExposureBatches.empty());
}
