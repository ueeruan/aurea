AUREA_TEST(PreviewGraphLifecycle, PendingPictureKeepsItsSnapshotAndCapturesReportBusyUntilSeekCancelsIt) {
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
    engine.renderer().set_transient_allocation_limit(20ull << 20);
    int window = 0;
    AUREA_CHECK(engine.attach_surface(&window, 320, 180).ok());
    AUREA_CHECK(engine.render_frame(false).ok());
    AUREA_CHECK(engine.renderer().preview_graph_pending());
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
    AUREA_CHECK_EQ(engine.capture_frame_rgba(64, pixels, width, height).code(), Errc::Busy);
    AUREA_CHECK_EQ(engine.capture_preview_frame_rgba(64, pixels, width, height).code(), Errc::Busy);
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
    engine.renderer().set_transient_allocation_limit(20ull << 20);
    int window = 0; AUREA_CHECK(engine.attach_surface(&window,320,180).ok());
    AUREA_CHECK_EQ(engine.read_status().previewDenominator, 1u);
    AUREA_CHECK(engine.render_frame(false).ok());
    AUREA_CHECK(engine.renderer().preview_graph_pending());
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
