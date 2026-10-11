AUREA_TEST(PreviewGraphLifecycle, PendingPictureKeepsItsSnapshotAndSeekCancelsBehindItsFence) {
    Engine engine;
    auto* backend = new test::MockBackend;
    backend->mapBuffers = true;
    backend->supportsFrameDiscard = backend->supportsPresentableLease = true;
    auto config = headless_config(); config.backend = backend;
    config.initialPreviewScale = PreviewScale::Full;
    config.memoryBudgetBytes = 256ull << 20;
    AUREA_CHECK(engine.initialize(config).ok());
    AUREA_CHECK(engine.new_project(320, 180, 30., "same quality pending preview").ok());
    const auto source = engine.add_shape(0);
    AUREA_CHECK(source.ok()); if (!source.ok()) return;
    auto* composition = engine.project()->timeline().composition(engine.project()->timeline().current());
    auto* layer = composition->layer(LayerId::unpack(*source));
    layer->shape.bounds = Rect{0,0,1024,1024}; layer->shape.shapeType = 1;
    layer->transform.anchor = Vec3{512,512,0};
    layer->transform.position = Vec3{160,90,0};
    Command add; add.type = CommandType::EffectAdd;
    add.effect_add.layer = LayerId::unpack(*source);
    add.effect_add.effectType = effect_type_id(effect_keys::kGaussianBlur);
    add.effect_add.index = kInvalidIndex;
    AUREA_CHECK(engine.apply_command(add).ok());
    AUREA_CHECK_EQ(layer->effects.size(), 1u);
    if (layer->effects.empty()) return;
    layer->effects[0].params[0].constant.v[0] = 3;
    engine.renderer().set_preview_cache_budget(0);
    // Visible blur bounds produce a 9,803,520-byte whole graph, while the
    // largest segmented source+horizontal pass needs 8,881,920 bytes.
    engine.renderer().set_transient_allocation_limit(9ull << 20);
    int window = 0;
    AUREA_CHECK(engine.attach_surface(&window, 320, 180).ok());
    AUREA_CHECK(engine.render_frame(false).ok());
    AUREA_CHECK(engine.renderer().preview_graph_pending());
    if (!engine.renderer().preview_graph_pending()) return;
    AUREA_CHECK_EQ(backend->acquires, 1u); AUREA_CHECK_EQ(backend->presents, 0u);
    const auto created = backend->texturesCreated;
    const auto submitted = backend->framesSubmitted;
    backend->beforeWaitFrame = [](u64, u64 timeout) {
        AUREA_CHECK_EQ(timeout, 0ull);
        return Status{Errc::Timeout};
    };
    for (u32 turn = 0; turn < 5; ++turn) {
        AUREA_CHECK(engine.render_frame(false).ok());
        AUREA_CHECK(engine.renderer().preview_graph_pending());
        AUREA_CHECK_EQ(backend->framesSubmitted, submitted);
        AUREA_CHECK_EQ(backend->presents, 0u);
        AUREA_CHECK_EQ(engine.read_status().playhead.value, 0ll);
    }
    std::vector<u8> pixels;
    u32 width = 0, height = 0;
    AUREA_CHECK_EQ(engine.render_effect_preview(effect_type_id(effect_keys::kGaussianBlur), 64,64,
        pixels,width,height).code(), Errc::Busy);
    AUREA_CHECK_EQ(backend->texturesCreated, created);
    AUREA_CHECK_EQ(backend->framesSubmitted, submitted);
    Command seek; seek.type = CommandType::PlaybackSeek;
    seek.seek.time = tick_at(FrameIndex{30}, 30.);
    AUREA_CHECK_EQ(engine.submit_commands(&seek,1), 1u);
    AUREA_CHECK_EQ(engine.render_frame(false).code(), Errc::Timeout);
    AUREA_CHECK(!engine.renderer().preview_graph_pending());
    AUREA_CHECK(!backend->presentableLeaseActive);
    AUREA_CHECK_EQ(backend->presents, 0u);
    AUREA_CHECK_EQ(backend->presentableLeases, backend->presentableLeasesEnded);
    backend->beforeWaitFrame = {};
    AUREA_CHECK(engine.render_frame(false).ok());
    AUREA_CHECK(engine.renderer().preview_graph_pending());
    for (u32 turn = 0; turn < 1000 && engine.renderer().preview_graph_pending(); ++turn)
        AUREA_CHECK(engine.render_frame(false).ok());
    AUREA_CHECK(!engine.renderer().preview_graph_pending());
    AUREA_CHECK_EQ(backend->acquires, 2u); AUREA_CHECK_EQ(backend->presents, 1u);
    AUREA_CHECK_EQ(engine.read_status().playhead.value, 30ll);
    AUREA_CHECK_EQ(engine.read_status().previewDenominator, 1u);
    AUREA_CHECK_EQ(layer->shape.bounds.w, 1024.f);
    AUREA_CHECK_EQ(layer->effects[0].params[0].constant.v[0], 3.f);
    // Pending backpressure was not an editor error; a genuine queue loss
    // must remain visible in the engine status after its safe cancellation.
    AUREA_CHECK(engine.render_frame(false).ok());
    AUREA_CHECK(engine.renderer().preview_graph_pending());
    const auto presents = backend->presents;
    backend->beforeWaitFrame = [](u64, u64 timeout) {
        AUREA_CHECK_EQ(timeout, 0ull); return Status{Errc::DeviceLost};
    };
    AUREA_CHECK_EQ(engine.render_frame(false).code(), Errc::DeviceLost);
    AUREA_CHECK_EQ(engine.read_status().lastError, Errc::DeviceLost);
    AUREA_CHECK(!engine.renderer().preview_graph_pending());
    AUREA_CHECK(!backend->presentableLeaseActive);
    AUREA_CHECK_EQ(backend->presents, presents);
    backend->beforeWaitFrame = {};
    engine.shutdown();
}

AUREA_TEST(PreviewGraphLifecycle, AutomaticPressureChangesOnlyThePictureAfterFinalPresentation) {
    Engine engine;
    auto* backend = new test::MockBackend;
    backend->mapBuffers = true;
    backend->supportsFrameDiscard = backend->supportsPresentableLease = true;
    auto config = headless_config(); config.backend = backend;
    config.initialPreviewScale = PreviewScale::Auto;
    config.memoryBudgetBytes = 256ull << 20;
    AUREA_CHECK(engine.initialize(config).ok());
    AUREA_CHECK(engine.new_project(320, 180, 30., "AUTO real graph pressure").ok());
    const auto source = engine.add_shape(0);
    AUREA_CHECK(source.ok()); if (!source.ok()) return;
    auto* composition = engine.project()->timeline().composition(engine.project()->timeline().current());
    auto* layer = composition->layer(LayerId::unpack(*source));
    layer->shape.bounds = Rect{0,0,1024,1024}; layer->shape.shapeType = 1;
    layer->transform.anchor = Vec3{512,512,0}; layer->transform.position = Vec3{160,90,0};
    Command add; add.type = CommandType::EffectAdd;
    add.effect_add.layer = LayerId::unpack(*source);
    add.effect_add.effectType = effect_type_id(effect_keys::kGaussianBlur);
    add.effect_add.index = kInvalidIndex;
    AUREA_CHECK(engine.apply_command(add).ok());
    AUREA_CHECK_EQ(layer->effects.size(), 1u); if (layer->effects.empty()) return;
    layer->effects[0].params[0].constant.v[0] = 3;
    engine.renderer().set_preview_cache_budget(0);
    engine.renderer().set_transient_allocation_limit(9ull << 20);
    int window = 0; AUREA_CHECK(engine.attach_surface(&window,320,180).ok());
    AUREA_CHECK_EQ(engine.read_status().previewDenominator, 1u);
    AUREA_CHECK(engine.render_frame(false).ok());
    AUREA_CHECK(engine.renderer().preview_graph_pending());
    if (!engine.renderer().preview_graph_pending()) return;
    backend->beforeWaitFrame = [](u64, u64 timeout) {
        AUREA_CHECK_EQ(timeout, 0ull); return Status{Errc::Timeout};
    };
    for (u32 turn=0; turn<3; ++turn) {
        AUREA_CHECK(engine.render_frame(false).ok());
        AUREA_CHECK(engine.renderer().preview_graph_pending());
        AUREA_CHECK_EQ(engine.read_status().previewDenominator, 1u);
        AUREA_CHECK_EQ(backend->presents, 0u);
    }
    backend->beforeWaitFrame = {};
    for (u32 turn=0; turn<1000 && engine.renderer().preview_graph_pending(); ++turn)
        AUREA_CHECK(engine.render_frame(false).ok());
    AUREA_CHECK(!engine.renderer().preview_graph_pending());
    AUREA_CHECK_EQ(backend->presents, 1u);
    const auto completed = engine.read_status();
    // The first picture was rendered at its original frozen dimensions,
    // while the controller's selected scale now belongs to the next picture.
    AUREA_CHECK_EQ(completed.previewWidth, 320u);
    AUREA_CHECK_EQ(completed.previewHeight, 180u);
    AUREA_CHECK_EQ(completed.previewDenominator, 2u);
    AUREA_CHECK_EQ(completed.gpuMs, 0.f);
    // This fixture exercises one real admission failure and sticky AUTO,
    // rather than imposing another artificial failure on the next picture.
    // Shape/SDF keeps its authored 1024² source even at reduced preview scale.
    // Continuous-pressure continuation is covered by the other lease tests.
    engine.renderer().set_transient_allocation_limit(256ull << 20);
    AUREA_CHECK(engine.render_frame(true).ok()); // no request_render/seek/edit
    AUREA_CHECK(!engine.renderer().preview_graph_pending());
    AUREA_CHECK_EQ(backend->presents, 2u);
    AUREA_CHECK_EQ(engine.read_status().previewWidth, 160u);
    AUREA_CHECK_EQ(engine.read_status().previewHeight, 90u);
    AUREA_CHECK_EQ(engine.read_status().playhead.value, 0ll);
    AUREA_CHECK_EQ(layer->shape.bounds.w, 1024.f);
    AUREA_CHECK_EQ(layer->effects[0].params[0].constant.v[0], 3.f);
    // Passing the ordinary paused refinement deadline must not jump back to
    // FULL and repeat the same memory failure. The pressure limit follows
    // AUTO's current state, and an explicit manual FULL remains honored.
    std::this_thread::sleep_for(std::chrono::milliseconds(350));
    AUREA_CHECK(engine.render_frame(true).ok());
    AUREA_CHECK(!engine.renderer().preview_graph_pending());
    AUREA_CHECK_EQ(backend->presents, 2u);
    AUREA_CHECK_EQ(engine.read_status().previewWidth, 160u);
    AUREA_CHECK_EQ(engine.read_status().previewDenominator, 2u);
    engine.shutdown();
}

AUREA_TEST(PreviewGraphLifecycle, SynchronousCaptureCancelsLiveAndWaitsOutsideTheRenderLockWithoutCallerRetry) {
    Engine engine;
    auto* backend = new test::MockBackend;
    backend->mapBuffers = true;
    backend->supportsFrameDiscard = backend->supportsPresentableLease = true;
    auto config = headless_config(); config.backend = backend;
    config.initialPreviewScale = PreviewScale::Full;
    config.memoryBudgetBytes = 256ull << 20;
    AUREA_CHECK(engine.initialize(config).ok());
    AUREA_CHECK(engine.new_project(320,180,30.,"capture owns unchanged preview").ok());
    const auto source = engine.add_shape(0);
    AUREA_CHECK(source.ok()); if (!source.ok()) return;
    auto* comp = engine.project()->timeline().composition(engine.project()->timeline().current());
    auto* layer = comp->layer(LayerId::unpack(*source));
    layer->shape.bounds = Rect{0,0,1024,1024}; layer->shape.shapeType = 1;
    layer->transform.anchor = Vec3{512,512,0}; layer->transform.position = Vec3{160,90,0};
    Command effect; effect.type = CommandType::EffectAdd;
    effect.effect_add.layer = LayerId::unpack(*source);
    effect.effect_add.effectType = effect_type_id(effect_keys::kGaussianBlur);
    effect.effect_add.index = kInvalidIndex;
    AUREA_CHECK(engine.apply_command(effect).ok());
    if (layer->effects.empty()) return;
    layer->effects[0].params[0].constant.v[0] = 3;
    engine.renderer().set_preview_cache_budget(0);
    engine.renderer().set_transient_allocation_limit(9ull << 20);
    int window=0;
    AUREA_CHECK(engine.attach_surface(&window,320,180).ok());
    AUREA_CHECK(engine.render_frame(false).ok());
    AUREA_CHECK(engine.renderer().preview_graph_pending());
    if (!engine.renderer().preview_graph_pending()) return;
    const u64 pending = backend->last_submitted_frame();
    const u32 created = backend->texturesCreated, destroyed = backend->texturesDestroyed;
    const u32 submitted = backend->framesSubmitted;
    std::atomic<bool> complete{false}, wrongTimeout{false};
    std::atomic<u32> polls{0}, readbacks{0};
    backend->beforeWaitFrame = [&](u64 frame,u64 timeout) {
        if (frame == pending && !complete.load(std::memory_order_acquire)) {
            if (timeout != 0) wrongTimeout = true;
            polls.fetch_add(1,std::memory_order_release);
            return Status{Errc::Timeout};
        }
        return OkStatus;
    };
    // This is explicitly a mocked readback sink: it exercises the facade's
    // conversion/byte contract, not the renderer's pixel fidelity. The latter
    // is covered by FrameGraphSegmentsGpu and native device capture tests.
    backend->beforeReadTexture = [&](TextureHandle handle,void* data,u32 stride) {
        const TextureDesc desc = backend->texture_desc(handle);
        if (desc.format != SurfaceFormat::RGBA16F || stride != desc.width*8) return Status{Errc::InvalidArgument};
        for (u32 y=0; y<desc.height; ++y) {
            auto* row = reinterpret_cast<u16*>(static_cast<u8*>(data)+usize(y)*stride);
            for (u32 x=0; x<desc.width; ++x) {
                row[x*4+0] = x&1 ? 0x3000 : 0x3800;
                row[x*4+1] = 0x3400;
                row[x*4+2] = x&1 ? 0x3800 : 0x3000;
                row[x*4+3] = 0x3c00;
            }
        }
        readbacks.fetch_add(1,std::memory_order_release);
        return OkStatus;
    };
    std::vector<u8> pixels;
    u32 width=0,height=0;
    auto capture=std::async(std::launch::async,[&] { return engine.capture_frame_rgba(64,pixels,width,height); });
    const auto until=std::chrono::steady_clock::now()+std::chrono::seconds(1);
    while (!polls.load(std::memory_order_acquire) && std::chrono::steady_clock::now()<until)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    AUREA_CHECK(polls.load(std::memory_order_acquire)>0);
    AUREA_CHECK(capture.wait_for(std::chrono::milliseconds(0))==std::future_status::timeout);
    AUREA_CHECK_EQ(backend->texturesCreated,created);
    AUREA_CHECK_EQ(backend->texturesDestroyed,destroyed);
    AUREA_CHECK_EQ(backend->framesSubmitted,submitted);
    AUREA_CHECK_EQ(backend->presents,0u);
    // attach_surface takes renderMutex_: it can execute while the request is
    // polling a blocked GPU fence, rather than waiting for that request.
    const u64 callbackBegan=monotonic_ns();
    AUREA_CHECK(engine.attach_surface(&window,320,180).ok());
    AUREA_CHECK(monotonic_ns()-callbackBegan<100'000'000ull);
    for (u32 i=0;i<3;++i) AUREA_CHECK(engine.render_frame(false).ok());
    AUREA_CHECK_EQ(backend->texturesCreated,created);
    AUREA_CHECK_EQ(backend->texturesDestroyed,destroyed);
    AUREA_CHECK_EQ(backend->framesSubmitted,submitted);
    complete.store(true,std::memory_order_release);
    const Status captured=capture.get();
    AUREA_CHECK(captured.ok()); AUREA_CHECK(!wrongTimeout.load());
    AUREA_CHECK_EQ(readbacks.load(),1u);
    AUREA_CHECK_EQ(width,64u); AUREA_CHECK_EQ(height,36u);
    AUREA_CHECK_EQ(pixels.size(),usize(64*36*4));
    if (pixels.size()>=8) {
        AUREA_CHECK_EQ(pixels[0],188u); AUREA_CHECK_EQ(pixels[1],137u);
        AUREA_CHECK_EQ(pixels[2],99u); AUREA_CHECK_EQ(pixels[3],255u);
        AUREA_CHECK_EQ(pixels[4],99u); AUREA_CHECK_EQ(pixels[6],188u);
    }
    AUREA_CHECK_EQ(backend->presents,0u); // capture must not repair the display
    AUREA_CHECK_EQ(engine.read_status().playhead.value,0ll);
    AUREA_CHECK_EQ(engine.read_status().previewDenominator,1u);
    AUREA_CHECK_EQ(layer->shape.bounds.w,1024.f);
    AUREA_CHECK_EQ(layer->effects[0].params[0].constant.v[0],3.f);
    backend->beforeWaitFrame={}; backend->beforeReadTexture={};
    AUREA_CHECK(engine.render_frame(false).ok());
    for (u32 i=0;i<1000 && engine.renderer().preview_graph_pending();++i)
        AUREA_CHECK(engine.render_frame(false).ok());
    AUREA_CHECK(!engine.renderer().preview_graph_pending());
    AUREA_CHECK_EQ(backend->presents,1u);
    AUREA_CHECK_EQ(engine.read_status().previewWidth,320u);
    AUREA_CHECK_EQ(engine.read_status().previewHeight,180u);
    std::printf("\n    capture handoff: blocked-fence polls=%u mocked_readbacks=%u request_retries=0 early_allocations=0 capture_presents=0 resumed_presents=1\n",
        polls.load(),readbacks.load());
    engine.shutdown();
}

AUREA_TEST(PreviewGraphLifecycle, CaptureOwnershipParksThePlayingRenderThreadUntilRelease) {
    IdlePreviewFixture fixture;
    fixture.engine.renderer().set_preview_cache_budget(0);
    Command play; play.type=CommandType::PlaybackPlay;
    AUREA_CHECK(fixture.engine.apply_command(play).ok());
    AUREA_CHECK(fixture.engine.render_frame(false).ok());
    AUREA_CHECK(fixture.engine.read_status().playing);
    std::atomic<bool> complete{false};
    std::atomic<u32> polls{0};
    fixture.backend->beforeWaitFrame=[&](u64,u64) {
        if (!complete.load(std::memory_order_acquire)) {
            polls.fetch_add(1,std::memory_order_release);
            return Status{Errc::Timeout};
        }
        return OkStatus;
    };
    fixture.backend->beforeReadTexture=[&](TextureHandle handle,void* data,u32 stride) {
        const TextureDesc desc=fixture.backend->texture_desc(handle);
        std::memset(data,0,usize(stride)*desc.height); // mocked sink, no GPU pixel claim
        return OkStatus;
    };
    std::vector<u8> pixels; u32 width=0,height=0;
    auto capture=std::async(std::launch::async,[&] {
        return fixture.engine.capture_frame_rgba(32,pixels,width,height);
    });
    const auto until=std::chrono::steady_clock::now()+std::chrono::seconds(1);
    while (!polls.load(std::memory_order_acquire) && std::chrono::steady_clock::now()<until)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    AUREA_CHECK(polls.load(std::memory_order_acquire)>0);
    fixture.engine.start_render_thread();
    std::this_thread::sleep_for(std::chrono::milliseconds(25));
    const u64 before=fixture.engine.render_wakeups();
    for (u32 wake=0;wake<12;++wake) {
        fixture.engine.request_render(); // even vsync/UI wakes cannot steal the token
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    const u64 parkedWakes=fixture.engine.render_wakeups()-before;
    AUREA_CHECK(parkedWakes<=1);
    fixture.engine.stop_render_thread();
    complete.store(true,std::memory_order_release);
    AUREA_CHECK(capture.get().ok());
    AUREA_CHECK_EQ(width,32u); AUREA_CHECK_EQ(height,32u);
    AUREA_CHECK_EQ(pixels.size(),usize(32*32*4));
    fixture.backend->beforeWaitFrame={}; fixture.backend->beforeReadTexture={};
    std::printf("\n    capture ownership: playing=1 blocked_GPU_polls=%u UI_wakes=12 render_wakeups_while_owned=%llu\n",
        polls.load(),static_cast<unsigned long long>(parkedWakes));
}

AUREA_TEST(PreviewGraphLifecycle, ShutdownCancelsAndJoinsAnOwnedCaptureBeforeBackendTeardown) {
    IdlePreviewFixture fixture;
    fixture.engine.renderer().set_preview_cache_budget(0);
    std::atomic<u32> polls{0}, readbacks{0}, teardownWaits{0};
    fixture.backend->beforeWaitFrame=[&](u64,u64) {
        polls.fetch_add(1,std::memory_order_release);
        return Status{Errc::Timeout}; // never completes; lifecycle must cancel the request
    };
    fixture.backend->beforeReadTexture=[&](TextureHandle,void*,u32) {
        readbacks.fetch_add(1,std::memory_order_relaxed);
        return OkStatus;
    };
    fixture.backend->beforeWaitIdle=[&] {
        teardownWaits.fetch_add(1,std::memory_order_relaxed);
    };
    std::vector<u8> pixels; u32 width=0,height=0;
    auto capture=std::async(std::launch::async,[&] {
        return fixture.engine.capture_frame_rgba(32,pixels,width,height);
    });
    const auto until=std::chrono::steady_clock::now()+std::chrono::seconds(1);
    while (!polls.load(std::memory_order_acquire) && std::chrono::steady_clock::now()<until)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    AUREA_CHECK(polls.load(std::memory_order_acquire)>0);
    const auto began=std::chrono::steady_clock::now();
    auto teardown=std::async(std::launch::async,[&] { fixture.engine.shutdown(); });
    // A never-signalled capture fence must not consume its four-second
    // deadline before shutdown can join it. Backend wait_idle still owns
    // real GPU teardown; this mock checks Engine request lifetime only.
    AUREA_CHECK_EQ(capture.wait_for(std::chrono::seconds(1)),std::future_status::ready);
    AUREA_CHECK_EQ(capture.get().code(),Errc::Cancelled);
    AUREA_CHECK_EQ(teardown.wait_for(std::chrono::seconds(1)),std::future_status::ready);
    teardown.get();
    AUREA_CHECK_EQ(fixture.engine.state(),EngineState::Uninitialized);
    AUREA_CHECK_EQ(readbacks.load(),0u);
    AUREA_CHECK(teardownWaits.load()>0);
    const auto elapsed=std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now()-began).count();
    std::printf("\n    capture shutdown: never-signalled-fence polls=%u cancelled=1 readbacks=0 request_join_ms=%lld backend_teardown_waits=%u\n",
        polls.load(),static_cast<long long>(elapsed),teardownWaits.load());
}

namespace preview_capture_progress_test {
constexpr u64 wholeOffscreenBytes = 9'342'720;
constexpr u64 largestSegmentBytes = 8'881'920;
constexpr u64 captureAllocationLimit = 9'100'000;
struct Fixture {
    Engine engine;
    test::MockBackend* backend = new test::MockBackend;
    bool ok = false;
    int window = 0;
    explicit Fixture(u64 allocationLimit = captureAllocationLimit) {
        backend->mapBuffers = true;
        backend->supportsFrameDiscard = backend->supportsPresentableLease = true;
        auto config = headless_config(); config.backend = backend;
        config.initialPreviewScale = PreviewScale::Full;
        config.memoryBudgetBytes = 256ull << 20;
        if (!engine.initialize(config).ok() || !engine.new_project(320,180,30.,"capture real fence progress").ok()) return;
        const auto source = engine.add_shape(0);
        if (!source.ok()) return;
        auto* comp = engine.project()->timeline().composition(engine.project()->timeline().current());
        auto* layer = comp->layer(LayerId::unpack(*source));
        if (!layer) return;
        layer->shape.bounds = Rect{0,0,1024,1024}; layer->shape.shapeType = 1;
        layer->transform.anchor = Vec3{512,512,0}; layer->transform.position = Vec3{160,90,0};
        Command add; add.type = CommandType::EffectAdd;
        add.effect_add.layer = LayerId::unpack(*source);
        add.effect_add.effectType = effect_type_id(effect_keys::kGaussianBlur);
        add.effect_add.index = kInvalidIndex;
        if (!engine.apply_command(add).ok() || layer->effects.empty()) return;
        layer->effects[0].params[0].constant.v[0] = 3;
        engine.renderer().set_preview_cache_budget(0);
        // Capture imports its output: unlike LIVE, its composition contributes
        // no transient allocation. The real control below measures this path.
        engine.renderer().set_transient_allocation_limit(allocationLimit);
        ok = engine.attach_surface(&window,320,180).ok();
    }
    ~Fixture() {
        if (engine.gpu()==backend) {
            backend->beforeWaitFrame = {}; backend->beforeReadTexture = {};
            backend->beforeEndFrame = {};
        }
        engine.shutdown();
    }
};
Status pattern(test::MockBackend& backend, TextureHandle handle, void* data, u32 stride) {
    const auto desc = backend.texture_desc(handle);
    if (desc.format != SurfaceFormat::RGBA16F || stride != desc.width*8)
        return Errc::InvalidArgument;
    for (u32 y=0;y<desc.height;++y) {
        auto* row = reinterpret_cast<u16*>(static_cast<u8*>(data)+usize(y)*stride);
        for (u32 x=0;x<desc.width;++x) {
            row[x*4] = x&1 ? 0x3000 : 0x3800;
            row[x*4+1] = 0x3400;
            row[x*4+2] = x&1 ? 0x3800 : 0x3000;
            row[x*4+3] = 0x3c00;
        }
    }
    return OkStatus;
}
} // namespace preview_capture_progress_test

AUREA_TEST(PreviewGraphLifecycle, CaptureRenewsOnDistinctCompletedSegmentFencesAndReturnsBytesOnce) {
    using namespace preview_capture_progress_test;
    u64 measuredWhole = 0;
    {
        // A separate Engine proves the actual OFFSCREEN graph's footprint.
        // Destroy it before pressure: cached compatible control slots must not
        // bypass admission in the pressured request.
        Fixture control(256ull << 20); AUREA_CHECK(control.ok); if (!control.ok) return;
        u32 controlReadbacks = 0;
        control.backend->beforeEndFrame = [&] {
            measuredWhole = std::max(measuredWhole, control.engine.renderer().pool_stats().bytes);
            return OkStatus;
        };
        control.backend->beforeReadTexture = [&](TextureHandle h,void* data,u32 stride) {
            ++controlReadbacks; return pattern(*control.backend,h,data,stride);
        };
        std::vector<u8> reference; u32 w=0,h=0;
        AUREA_CHECK(control.engine.capture_frame_rgba(320,reference,w,h).ok());
        AUREA_CHECK_EQ(w,320u); AUREA_CHECK_EQ(h,180u);
        AUREA_CHECK_EQ(reference.size(),usize(320*180*4));
        AUREA_CHECK_EQ(controlReadbacks,1u);
        AUREA_CHECK_EQ(control.backend->framesSubmitted,1u);
        AUREA_CHECK_EQ(measuredWhole,wholeOffscreenBytes);
    }
    AUREA_CHECK(largestSegmentBytes < captureAllocationLimit && captureAllocationLimit < measuredWhole);
    Fixture fixture; AUREA_CHECK(fixture.ok); if (!fixture.ok) return;
    struct Fence { u64 frame, firstPoll; bool completed = false; };
    std::vector<Fence> fences;
    u32 polls=0,readbacks=0,completed=0;
    u64 measuredSegmentPeak = 0;
    fixture.backend->beforeEndFrame = [&] {
        measuredSegmentPeak = std::max(measuredSegmentPeak, fixture.engine.renderer().pool_stats().bytes);
        return OkStatus;
    };
    constexpr u64 delay = 1'100'000'000ull;
    fixture.backend->beforeWaitFrame = [&](u64 frame,u64) -> Status {
        ++polls;
        auto it = std::find_if(fences.begin(),fences.end(),[&](const auto& f) { return f.frame == frame; });
        if (it == fences.end()) { fences.push_back({frame,monotonic_ns()}); it=std::prev(fences.end()); }
        if (monotonic_ns()-it->firstPoll < delay) {
            // Real elapsed waits, each below the inactivity deadline. Returning
            // Timeout repeatedly without a signalled fence is not progress.
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            return Errc::Timeout;
        }
        if (!it->completed) { it->completed=true; ++completed; }
        return OkStatus;
    };
    fixture.backend->beforeReadTexture = [&](TextureHandle h,void* data,u32 stride) {
        AUREA_CHECK(completed>=5); // final readback cannot race any segment
        ++readbacks; return pattern(*fixture.backend,h,data,stride);
    };
    std::vector<u8> pixels; u32 width=0,height=0;
    const u64 start=monotonic_ns();
    // One synchronous caller invocation. The readback sink is mocked, while
    // graph admission/segmentation and every elapsed wait run in production.
    const Status capture=fixture.engine.capture_frame_rgba(320,pixels,width,height);
    const f64 seconds=static_cast<f64>(monotonic_ns()-start)/1e9;
    AUREA_CHECK(capture.ok()); AUREA_CHECK(seconds>5.0 && seconds<20.0);
    AUREA_CHECK_EQ(width,320u); AUREA_CHECK_EQ(height,180u);
    AUREA_CHECK_EQ(pixels.size(),usize(320*180*4));
    AUREA_CHECK(completed>=5 && fences.size()>=5);
    AUREA_CHECK_EQ(measuredSegmentPeak,largestSegmentBytes);
    AUREA_CHECK(measuredSegmentPeak<=captureAllocationLimit && measuredSegmentPeak<measuredWhole);
    AUREA_CHECK_EQ(readbacks,1u);
    AUREA_CHECK_EQ(fixture.backend->acquires,0u); AUREA_CHECK_EQ(fixture.backend->presents,0u);
    bool exact=pixels.size()==usize(320*180*4);
    for (usize i=0;exact && i<pixels.size()/4;++i)
        exact = pixels[i*4]==(i&1 ? 99 : 188) && pixels[i*4+1]==137 &&
                pixels[i*4+2]==(i&1 ? 188 : 99) && pixels[i*4+3]==255;
    AUREA_CHECK(exact);
    AUREA_CHECK_EQ(fixture.engine.read_status().playhead.value,0ll);
    AUREA_CHECK_EQ(fixture.engine.read_status().previewDenominator,1u);
    std::printf("\n    capture progress: seconds=%.3f distinct_completed_fences=%u polls=%u capture_invocations=1 mocked_RGBA_bytes=%zu exact_conversion=%d readbacks=%u acquire=0 present=0 whole_offscreen_bytes=%llu segment_peak_bytes=%llu allocation_limit=%llu\n",
        seconds,completed,polls,pixels.size(),exact,readbacks,
        static_cast<unsigned long long>(measuredWhole),static_cast<unsigned long long>(measuredSegmentPeak),
        static_cast<unsigned long long>(captureAllocationLimit));
}

AUREA_TEST(PreviewGraphLifecycle, RepeatedTimeoutPollsDoNotRenewTheCaptureInactivityDeadline) {
    using namespace preview_capture_progress_test;
    Fixture fixture; AUREA_CHECK(fixture.ok); if (!fixture.ok) return;
    u32 polls=0,readbacks=0;
    fixture.backend->beforeWaitFrame = [&](u64,u64) -> Status {
        ++polls;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        return Errc::Timeout; // no completed GPU work, no deadline renewal
    };
    fixture.backend->beforeReadTexture = [&](TextureHandle,void*,u32) {
        ++readbacks; return OkStatus;
    };
    std::vector<u8> pixels; u32 width=0,height=0;
    const u64 start=monotonic_ns();
    const Status capture=fixture.engine.capture_frame_rgba(320,pixels,width,height);
    const f64 seconds=static_cast<f64>(monotonic_ns()-start)/1e9;
    AUREA_CHECK_EQ(capture.code(),Errc::Timeout);
    AUREA_CHECK(seconds>=3.8 && seconds<5.5);
    AUREA_CHECK(polls>10); AUREA_CHECK(pixels.empty()); AUREA_CHECK_EQ(readbacks,0u);
    AUREA_CHECK_EQ(fixture.backend->acquires,0u); AUREA_CHECK_EQ(fixture.backend->presents,0u);
    std::printf("\n    capture stalled: seconds=%.3f polls=%u completed_fences=0 capture_invocations=1 readbacks=0 acquire=0 present=0\n",
        seconds,polls);
}


namespace preview_capture_resource_test {
struct WorkerGate {
    std::atomic<bool> entered{false}, release{false}, left{false};
    std::atomic<u32> calls{0};
    static void before_build(void* context) noexcept {
        auto& gate=*static_cast<WorkerGate*>(context);
        gate.calls.fetch_add(1,std::memory_order_relaxed);
        gate.entered.store(true,std::memory_order_release);
        while (!gate.release.load(std::memory_order_acquire))
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        gate.left.store(true,std::memory_order_release);
    }
};
struct Fixture {
    WorkerGate gate;
    Engine engine;
    test::MockBackend* backend=new test::MockBackend;
    std::filesystem::path source;
    bool ownsSource=false,ok=false;
    int window=0;
    Fixture() {
        backend->mapBuffers=true;
        backend->supportsFrameDiscard=backend->supportsPresentableLease=true;
        auto config=headless_config(); config.backend=backend;
        config.initialPreviewScale=PreviewScale::Full; config.memoryBudgetBytes=256ull<<20;
        if (!engine.initialize(config).ok() || !engine.new_project(64,36,30.,"capture genuine CPU map preparation").ok()) return;
        // A real small HDRI uses the normal source-resolution cap in the CPU
        // environment builder. Requested final quality remains unchanged.
        source=std::filesystem::temp_directory_path()/std::filesystem::path(
            "aurea_capture_resource_2165_"+std::to_string(monotonic_ns())+".hdr");
        FILE* file=std::fopen(source.string().c_str(),"wb");
        if (!file) return;
        ownsSource=true;
        const char header[]="#?RADIANCE\nFORMAT=32-bit_rle_rgbe\n\n-Y 2 +X 4\n";
        bool wrote=std::fwrite(header,1,sizeof(header)-1,file)==sizeof(header)-1;
        for (u32 i=0;i<8;++i) {
            const u8 pixel[4]={static_cast<u8>(64+i*8),32,16,129};
            wrote &= std::fwrite(pixel,1,sizeof(pixel),file)==sizeof(pixel);
        }
        std::fclose(file);
        if (!wrote || !engine.import_hdri(source.string().c_str()).ok()) return;
        auto* composition=engine.project()->timeline().composition(engine.project()->timeline().current());
        if (!composition || !composition->environment().hdri.valid()) return;
        composition->environment().showBackground=true;
        engine.renderer().set_preview_cache_budget(0);
        engine.renderer().scene_renderer().set_environment_prepare_probe(&WorkerGate::before_build,&gate);
        ok=engine.attach_surface(&window,64,36).ok();
    }
    ~Fixture() {
        // The final CPU future owns its HDRI/context until the real builder
        // exits. Logical cancellation never pretends to preempt that worker.
        gate.release.store(true,std::memory_order_release);
        if (engine.gpu()==backend) {
            engine.renderer().scene_renderer().set_environment_prepare_probe(nullptr,nullptr);
            backend->beforeWaitFrame={}; backend->beforeReadTexture={}; backend->beforeEndFrame={};
        }
        engine.shutdown();
        if (ownsSource) std::remove(source.string().c_str());
    }
    bool wait_for_worker() {
        const u64 until=monotonic_ns()+3'500'000'000ull;
        while (!gate.entered.load(std::memory_order_acquire) && monotonic_ns()<until)
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        return gate.entered.load(std::memory_order_acquire);
    }
};
struct Capture {
    std::vector<u8> pixels; u32 width=0,height=0;
    Status result;
};
std::future<Capture> start(Fixture& fixture) {
    return std::async(std::launch::async,[&fixture] {
        Capture result;
        result.result=fixture.engine.capture_frame_rgba(64,result.pixels,result.width,result.height);
        return result;
    });
}
bool exact(const Capture& capture) {
    if (capture.width!=64 || capture.height!=36 || capture.pixels.size()!=64*36*4) return false;
    for (u32 y=0;y<36;++y) for (u32 x=0;x<64;++x) {
        const usize offset=(usize(y)*64+x)*4;
        if (capture.pixels[offset]!=(x&1?99:188) || capture.pixels[offset+1]!=137
            || capture.pixels[offset+2]!=(x&1?188:99) || capture.pixels[offset+3]!=255) return false;
    }
    return true;
}
}

AUREA_TEST(PreviewGraphLifecycle, ColdCpuResourcesRemainOwnedAndReturnExactCaptureAfterFiveSeconds) {
    using namespace preview_capture_resource_test;
    Fixture fixture; AUREA_CHECK(fixture.ok); if (!fixture.ok) return;
    std::atomic<u32> readbacks{0};
    fixture.backend->beforeReadTexture=[&](TextureHandle h,void* data,u32 stride) {
        ++readbacks; return preview_capture_progress_test::pattern(*fixture.backend,h,data,stride);
    };
    const u64 started=monotonic_ns(); auto capture=start(fixture);
    const bool entered=fixture.wait_for_worker(); AUREA_CHECK(entered);
    if (!entered) { fixture.gate.release=true; (void)capture.get(); return; }
    // The owned real CPU job is gated; no main capture frame was submitted.
    AUREA_CHECK_EQ(fixture.backend->framesSubmitted,0u);
    const u64 callbackStarted=monotonic_ns();
    AUREA_CHECK(fixture.engine.render_frame(false).ok());
    std::vector<u8> thumbnail; u32 w=0,h=0;
    AUREA_CHECK_EQ(fixture.engine.render_effect_preview(effect_type_id(effect_keys::kGaussianBlur),32,18,
        thumbnail,w,h).code(),Errc::Busy);
    AUREA_CHECK(monotonic_ns()-callbackStarted<250'000'000ull);
    std::this_thread::sleep_for(std::chrono::milliseconds(5200));
    AUREA_CHECK_EQ(capture.wait_for(std::chrono::milliseconds(0)),std::future_status::timeout);
    AUREA_CHECK(!fixture.gate.left.load()); AUREA_CHECK_EQ(readbacks.load(),0u);
    fixture.gate.release=true;
    const bool finished=capture.wait_for(std::chrono::seconds(20))==std::future_status::ready;
    AUREA_CHECK(finished); const auto result=capture.get();
    const f64 elapsed=(monotonic_ns()-started)/1.e9;
    AUREA_CHECK(result.result.ok()); AUREA_CHECK(exact(result));
    AUREA_CHECK(elapsed>5.2 && elapsed<30.);
    AUREA_CHECK_EQ(fixture.gate.calls.load(),1u); AUREA_CHECK_EQ(readbacks.load(),1u);
    AUREA_CHECK_EQ(fixture.backend->framesSubmitted,1u);
    AUREA_CHECK_EQ(fixture.backend->acquires,0u); AUREA_CHECK_EQ(fixture.backend->presents,0u);
    AUREA_CHECK(!fixture.engine.renderer().scene_renderer().environment_pending());
    AUREA_CHECK_EQ(fixture.engine.read_status().previewDenominator,1u);
    std::printf("\n    capture CPU resources: seconds=%.3f worker_calls=%u one_caller=1 readbacks=%u exact_mock_rgba=%u main_submissions=%u acquires=%u presents=%u\n",
        elapsed,fixture.gate.calls.load(),readbacks.load(),exact(result),fixture.backend->framesSubmitted,
        fixture.backend->acquires,fixture.backend->presents);
}

AUREA_TEST(PreviewGraphLifecycle, CpuPreparationDoesNotTurnAStalledFirstGpuFenceIntoThirtySeconds) {
    using namespace preview_capture_resource_test;
    Fixture fixture; AUREA_CHECK(fixture.ok); if (!fixture.ok) return;
    std::atomic<u32> polls{0},readbacks{0}; std::atomic<u64> firstPoll{0};
    fixture.backend->beforeWaitFrame=[&](u64 frame,u64 timeout)->Status {
        if (frame && !firstPoll.load()) firstPoll=monotonic_ns();
        AUREA_CHECK_EQ(timeout,0ull); ++polls;
        return Errc::Timeout;
    };
    fixture.backend->beforeReadTexture=[&](TextureHandle,void*,u32) { ++readbacks; return OkStatus; };
    const u64 started=monotonic_ns(); auto capture=start(fixture);
    const bool entered=fixture.wait_for_worker(); AUREA_CHECK(entered);
    if (!entered) { fixture.gate.release=true; (void)capture.get(); return; }
    std::this_thread::sleep_for(std::chrono::milliseconds(5200));
    AUREA_CHECK_EQ(capture.wait_for(std::chrono::milliseconds(0)),std::future_status::timeout);
    fixture.gate.release=true;
    const bool finished=capture.wait_for(std::chrono::seconds(16))==std::future_status::ready;
    AUREA_CHECK(finished); const auto result=capture.get();
    const u64 ended=monotonic_ns(); const f64 gpuWait=(ended-firstPoll.load())/1.e9;
    AUREA_CHECK_EQ(result.result.code(),Errc::Timeout);
    AUREA_CHECK(firstPoll.load()>started+5'000'000'000ull);
    AUREA_CHECK(gpuWait>=3.8 && gpuWait<6.0);
    AUREA_CHECK_EQ(fixture.backend->framesSubmitted,1u);
    AUREA_CHECK(polls.load()>20); AUREA_CHECK_EQ(readbacks.load(),0u);
    AUREA_CHECK(result.pixels.empty());
    AUREA_CHECK_EQ(fixture.backend->acquires,0u); AUREA_CHECK_EQ(fixture.backend->presents,0u);
    std::printf("\n    capture CPU then stalled GPU: total_seconds=%.3f actual_gpu_inactivity_seconds=%.3f polls=%u completed=0 readbacks=%u main_submissions=%u\n",
        (ended-started)/1.e9,gpuWait,polls.load(),readbacks.load(),fixture.backend->framesSubmitted);
}

AUREA_TEST(PreviewGraphLifecycle, PendingCpuResourcesKeepTheFixedRequestHardCeilingWithoutFakeProgress) {
    using namespace preview_capture_resource_test;
    Fixture fixture; AUREA_CHECK(fixture.ok); if (!fixture.ok) return;
    std::atomic<u32> readbacks{0};
    fixture.backend->beforeReadTexture=[&](TextureHandle,void*,u32) { ++readbacks; return OkStatus; };
    const u64 started=monotonic_ns(); auto capture=start(fixture);
    const bool entered=fixture.wait_for_worker(); AUREA_CHECK(entered);
    if (!entered) { fixture.gate.release=true; (void)capture.get(); return; }
    const bool finished=capture.wait_for(std::chrono::seconds(32))==std::future_status::ready;
    AUREA_CHECK(finished);
    // Release explicitly even if a regression would hold the request forever.
    // Cancellation/timeout must return while its immutable CPU job is alive.
    if (!finished) fixture.gate.release=true;
    const auto result=capture.get(); const f64 elapsed=(monotonic_ns()-started)/1.e9;
    AUREA_CHECK_EQ(result.result.code(),Errc::Timeout);
    AUREA_CHECK(elapsed>=29.8 && elapsed<32.);
    AUREA_CHECK(!fixture.gate.left.load()); AUREA_CHECK_EQ(fixture.gate.calls.load(),1u);
    AUREA_CHECK_EQ(readbacks.load(),0u); AUREA_CHECK_EQ(fixture.backend->framesSubmitted,0u);
    AUREA_CHECK_EQ(fixture.backend->acquires,0u); AUREA_CHECK_EQ(fixture.backend->presents,0u);
    AUREA_CHECK(result.pixels.empty());
    std::printf("\n    capture pending CPU hard cap: seconds=%.3f worker_alive=%u real_main_submissions=%u readbacks=%u no_fake_gpu_progress=1\n",
        elapsed,!fixture.gate.left.load(),fixture.backend->framesSubmitted,readbacks.load());
    fixture.gate.release=true;
}

AUREA_TEST(PreviewGraphLifecycle, SeekCancelsPendingCpuResourcesWithoutDestroyingTheirWorker) {
    using namespace preview_capture_resource_test;
    Fixture fixture; AUREA_CHECK(fixture.ok); if (!fixture.ok) return;
    std::atomic<u32> readbacks{0};
    fixture.backend->beforeReadTexture=[&](TextureHandle,void*,u32) { ++readbacks; return OkStatus; };
    auto capture=start(fixture);
    const bool entered=fixture.wait_for_worker(); AUREA_CHECK(entered);
    if (!entered) { fixture.gate.release=true; (void)capture.get(); return; }
    const u64 started=monotonic_ns();
    Command seek; seek.type=CommandType::PlaybackSeek; seek.seek.time=tick_at(FrameIndex{10},30.);
    AUREA_CHECK_EQ(fixture.engine.submit_commands(&seek,1),1u);
    const bool finished=capture.wait_for(std::chrono::seconds(1))==std::future_status::ready;
    AUREA_CHECK(finished); if (!finished) fixture.gate.release=true;
    const auto result=capture.get();
    AUREA_CHECK_EQ(result.result.code(),Errc::Cancelled);
    AUREA_CHECK(monotonic_ns()-started<1'000'000'000ull);
    AUREA_CHECK(!fixture.gate.left.load()); AUREA_CHECK_EQ(readbacks.load(),0u);
    AUREA_CHECK_EQ(fixture.backend->framesSubmitted,0u);
    AUREA_CHECK_EQ(fixture.backend->acquires,0u); AUREA_CHECK_EQ(fixture.backend->presents,0u);
    AUREA_CHECK(result.pixels.empty());
    std::printf("\n    capture pending CPU cancellation: request_cancel_ms=%.3f worker_alive=%u readbacks=%u main_submissions=%u explicit_release_before_teardown=1\n",
        (monotonic_ns()-started)/1.e6,!fixture.gate.left.load(),readbacks.load(),fixture.backend->framesSubmitted);
    fixture.gate.release=true;
}


AUREA_TEST(PreviewGraphLifecycle, PreviousUnfinishedFrameKeepsFourSecondsBeforeCpuPreparationWhenCacheIsEmpty) {
    using namespace preview_capture_resource_test;
    Fixture fixture; AUREA_CHECK(fixture.ok); if (!fixture.ok) return;
    // An unfinished earlier submission exists even without any preview-cache
    // slots. It must complete before the CPU-resource phase gains its ceiling.
    FrameBegin previous;
    AUREA_CHECK(fixture.backend->begin_offscreen_frame(previous).ok());
    AUREA_CHECK(fixture.backend->end_frame().ok());
    const u32 submitted=fixture.backend->framesSubmitted;
    AUREA_CHECK_EQ(submitted,1u);
    std::atomic<u32> polls{0},unexpected{0},readbacks{0};
    fixture.backend->beforeWaitFrame=[&](u64 frame,u64 timeout)->Status {
        AUREA_CHECK_EQ(timeout,0ull);
        if (frame!=previous.frameNumber) { ++unexpected; return Errc::DeviceLost; }
        ++polls; return Errc::Timeout;
    };
    fixture.backend->beforeReadTexture=[&](TextureHandle,void*,u32) { ++readbacks; return OkStatus; };
    const u64 began=monotonic_ns(); auto capture=start(fixture);
    const bool finished=capture.wait_for(std::chrono::seconds(8))==std::future_status::ready;
    AUREA_CHECK(finished); if (!finished) fixture.gate.release=true;
    const auto result=capture.get(); const f64 seconds=(monotonic_ns()-began)/1.e9;
    AUREA_CHECK_EQ(result.result.code(),Errc::Timeout);
    AUREA_CHECK(seconds>=3.8 && seconds<6.);
    AUREA_CHECK(polls.load()>20); AUREA_CHECK_EQ(unexpected.load(),0u);
    AUREA_CHECK_EQ(fixture.gate.calls.load(),0u); AUREA_CHECK(!fixture.gate.entered.load());
    AUREA_CHECK_EQ(readbacks.load(),0u); AUREA_CHECK_EQ(fixture.backend->framesSubmitted,submitted);
    AUREA_CHECK_EQ(fixture.backend->acquires,0u); AUREA_CHECK_EQ(fixture.backend->presents,0u);
    AUREA_CHECK(result.pixels.empty());
    std::printf("\n    capture prior-frame guard: seconds=%.3f old_frame=%llu polls=%u cpu_jobs=%u readbacks=%u new_submissions=%u preview_cache_disabled=1\n",
        seconds,static_cast<unsigned long long>(previous.frameNumber),polls.load(),fixture.gate.calls.load(),
        readbacks.load(),fixture.backend->framesSubmitted-submitted);
}
