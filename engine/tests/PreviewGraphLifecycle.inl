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
