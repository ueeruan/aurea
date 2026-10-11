// Shared Android/iOS scheduling contract. The mock executes real Engine and
// Renderer cache decisions; pixel fidelity has separate GPU-cache regressions.
AUREA_TEST(PreviewRetry, FailedFramesArePacedAndAnEditRecoversImmediately) {
    Engine engine;
    auto* backend = new test::MockBackend;
    auto config = headless_config();
    config.backend = backend;
    config.initialPreviewScale = PreviewScale::Full;
    AUREA_CHECK(engine.initialize(config).ok());
    AUREA_CHECK(engine.new_project(64, 64, 30., "paced GPU retry").ok());
    engine.renderer().set_preview_cache_budget(0);
    int window = 0;
    AUREA_CHECK(engine.attach_surface(&window, 64, 64).ok());
    backend->beforeEndFrame = [] { return Status{Errc::Timeout}; };
    AUREA_CHECK(engine.render_frame(true).code() == Errc::Timeout);
    const u32 first = backend->framesSubmitted;
    for (u32 poll = 0; poll < 20; ++poll) (void)engine.render_frame(true);
    AUREA_CHECK_EQ(backend->framesSubmitted, first);

    // A failed submission does not block on vsync. The real paused render
    // thread must still wait between attempts, instead of spinning through
    // repeated commands/presents until a resource becomes available.
    engine.start_render_thread();
    std::this_thread::sleep_for(std::chrono::milliseconds(175));
    engine.stop_render_thread();
    const u32 paced = backend->framesSubmitted;
    AUREA_CHECK(paced > first);
    AUREA_CHECK(paced <= first + 4);

    Command seek;
    seek.type = CommandType::PlaybackSeek;
    seek.seek.time = tick_at(FrameIndex{1}, 30.);
    AUREA_CHECK_EQ(engine.submit_commands(&seek, 1), 1u);
    AUREA_CHECK(engine.render_frame(true).code() == Errc::Timeout);
    AUREA_CHECK_EQ(backend->framesSubmitted, paced + 1);
    backend->beforeEndFrame = [] { return OkStatus; };
    engine.request_render();
    AUREA_CHECK(engine.render_frame(true).ok());
    const u32 recovered = backend->framesSubmitted;
    AUREA_CHECK_EQ(recovered, paced + 2);
    for (u32 poll = 0; poll < 20; ++poll) AUREA_CHECK(engine.render_frame(true).ok());
    AUREA_CHECK_EQ(backend->framesSubmitted, recovered);
    engine.shutdown();
}

namespace {
struct IdlePreviewFixture {
    Engine engine;
    aurea::test::MockBackend* backend = new aurea::test::MockBackend;
    int window = 0;
    explicit IdlePreviewFixture(u32 capacity = 4) {
        auto config = headless_config(); config.backend = backend;
        config.initialPreviewScale = PreviewScale::Full;
        AUREA_CHECK(engine.initialize(config).ok());
        AUREA_CHECK(engine.new_project(64, 64, 30., "idle cache").ok());
        engine.project()->timeline().composition(engine.project()->timeline().current())->set_duration(FrameIndex{90});
        engine.renderer().set_preview_cache_budget(u64(capacity) * 64 * 64 * 4);   // RGBA8 sRGB slots
        AUREA_CHECK(engine.attach_surface(&window, 64, 64).ok());
    }
    ~IdlePreviewFixture() { engine.shutdown(); }
    u64 add_future_image() {
        std::vector<u8> rgba(64 * 64 * 4, 255);
        const auto image = engine.import_image(rgba.data(), 64, 64, "future pixels");
        AUREA_CHECK(image.ok());
        if (!image.ok()) return 0;
        auto* comp = engine.project()->timeline().composition(engine.project()->timeline().current());
        comp->layer(LayerId::unpack(*image))->start = FrameIndex{1};
        return *image;
    }
    void settle() { std::this_thread::sleep_for(std::chrono::milliseconds(680)); }
    void fill(u32 count) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
        while (engine.renderer().preview_cached_count() < count && std::chrono::steady_clock::now() < deadline) {
            AUREA_CHECK(engine.render_frame(true).ok());
            std::this_thread::sleep_for(std::chrono::milliseconds(35));
        }
        AUREA_CHECK_EQ(engine.renderer().preview_cached_count(), count);
    }
};
}

AUREA_TEST(PreviewIdle, WindowRespectsExistingByteBudgetFpsAndEndOfTimeline) {
    constexpr u64 budget = 48ull * 1024 * 1024;
    AUREA_CHECK_EQ(preview_idle_target(30, preview_cache_capacity(1920, 1080, budget), 300), 3u);
    AUREA_CHECK_EQ(preview_idle_target(30, preview_cache_capacity(960, 540, budget), 300), 12u);
    AUREA_CHECK_EQ(preview_idle_target(30, 30, 300), 30u);
    AUREA_CHECK_EQ(preview_idle_target(24, 30, 300), 30u);   // capacity, not one second
    AUREA_CHECK_EQ(preview_idle_target(30, 120, 300), 120u);
    AUREA_CHECK_EQ(preview_idle_target(120, 30, 2), 2u);
    AUREA_CHECK_EQ(preview_idle_target(30, 0, 300), 0u);
    AUREA_CHECK_EQ(preview_idle_target(30, 30, 0), 0u);
    AUREA_CHECK_EQ(preview_idle_target(std::numeric_limits<double>::infinity(), 30, 300), 30u);
}

AUREA_TEST(PreviewIdle, RamTieredBudgetHoldsFarMoreThanOneSecond) {
    constexpr u64 MiB = 1ull << 20, GiB = 1ull << 30;
    AUREA_CHECK_EQ(preview_cache_budget(2 * GiB, 0), 32 * MiB);
    AUREA_CHECK_EQ(preview_cache_budget(3800 * MiB, 0), 64 * MiB);
    AUREA_CHECK_EQ(preview_cache_budget(6 * GiB, 0), 320 * MiB);
    AUREA_CHECK_EQ(preview_cache_budget(12 * GiB, 0), 512 * MiB);
    AUREA_CHECK_EQ(preview_cache_budget(12 * GiB, 1 * GiB), 256 * MiB);     // 1/4 of the process
    AUREA_CHECK_EQ(preview_cache_budget(6 * GiB, 0, .55f), 160 * MiB);       // pressure shrinks
    AUREA_CHECK_EQ(preview_cache_budget(6 * GiB, 0, .9f), 0ull);
    // 6 GB phone, viewport-sized preview (540x960), RGBA8: far beyond 30 frames.
    const u32 frames = preview_cache_capacity(540, 960, preview_cache_budget(6 * GiB, 0), 4);
    AUREA_CHECK_EQ(frames, 161u);                                            // 5.4 s at 30 fps
    AUREA_CHECK_EQ(preview_idle_target(30, frames, 1000), 161u);           // look-ahead fills it
    AUREA_CHECK_EQ(preview_cache_capacity(540, 960, preview_cache_budget(12 * GiB, 0), 4), 258u);
    AUREA_CHECK_EQ(preview_cache_capacity(320, 568, preview_cache_budget(12 * GiB, 0), 4), kPreviewCacheMaxFrames);
    AUREA_CHECK_EQ(preview_cache_capacity(1920, 1080, 320 * MiB, 4), 40u); // still bounded by bytes
    AUREA_CHECK_EQ(preview_cache_capacity(1920, 1080, 320 * MiB, 8), 20u);
}

AUREA_TEST(PreviewIdle, QuietClockParksPendingWorkAndCancelsEveryIdentityChange) {
    PreviewIdleBuffer idle;
    PreviewIdleKey key{1, 2, 3, 4, 5};
    idle.observe(true, key, 1, false);
    AUREA_CHECK(!idle.ready(650'000'000, 7));
    AUREA_CHECK(idle.ready(650'000'001, 7));
    idle.observe(true, key, 650'000'001, false); // polling does not postpone useful work
    AUREA_CHECK(idle.ready(650'000'001, 7));
    idle.attempted(700'000'000, 0, false, 7);
    AUREA_CHECK_EQ(idle.deadline(7), 0ull); // no repeated GPU wakes for missing resources
    AUREA_CHECK(!idle.ready(799'999'999, 8));
    AUREA_CHECK(idle.ready(800'000'000, 8));
    idle.finish();
    AUREA_CHECK_EQ(idle.deadline(8), 0ull);
    for (u32 field = 0; field < 5; ++field) {
        auto replacement = key;
        if (field == 0) ++replacement.session;
        if (field == 1) ++replacement.revision;
        if (field == 2) ++replacement.composition;
        if (field == 3) ++replacement.mediaEpoch;
        if (field == 4) ++replacement.playhead;
        idle.observe(true, replacement, 900'000'000, false);
        AUREA_CHECK(!idle.ready(1'000'000'000, 8));
        AUREA_CHECK(idle.ready(1'550'000'000, 8));
        idle.finish();
    }
    idle.observe(false, key, 2'000'000'000, false);
    AUREA_CHECK_EQ(idle.deadline(9), 0ull);
    idle.observe(true, key, 3'000'000'000, false);
    idle.observe(true, key, 3'600'000'000, true); // same-time edit/redraw restarts quiet interval
    AUREA_CHECK(!idle.ready(3'700'000'000, 9));
    AUREA_CHECK(idle.ready(4'250'000'000, 9));
}

AUREA_TEST(PreviewIdle, IdleWindowMakesPlayImmediateWithoutPresentingOrAdvancingFutureFrames) {
    IdlePreviewFixture cold;
    AUREA_CHECK(cold.engine.render_frame().ok());
    Command play; play.type = CommandType::PlaybackPlay;
    AUREA_CHECK(cold.engine.apply_command(play).ok());
    AUREA_CHECK(cold.engine.render_frame().ok());
    AUREA_CHECK_EQ(cold.backend->offscreenFrames, 1u); // cold Play still needs preparation

    IdlePreviewFixture warm;
    AUREA_CHECK(warm.engine.render_frame().ok());
    AUREA_CHECK_EQ(warm.engine.renderer().preview_cached_count(), 1u);
    AUREA_CHECK(warm.engine.render_frame(true).ok());
    AUREA_CHECK_EQ(warm.backend->offscreenFrames, 0u); // never during the quiet interval
    warm.settle(); warm.fill(4);
    AUREA_CHECK_EQ(warm.backend->offscreenFrames, 3u);
    AUREA_CHECK_EQ(warm.backend->acquires, 1u);
    AUREA_CHECK_EQ(warm.engine.read_status().playhead.value, 0ll);
    for (i64 i = 0; i < 4; ++i) AUREA_CHECK(warm.engine.renderer().preview_cached(FrameIndex{i}));
    AUREA_CHECK(warm.engine.apply_command(play).ok());
    AUREA_CHECK(warm.engine.render_frame().ok());
    bridge::EngineStatusPOD status{}; warm.engine.fill_status(status);
    AUREA_CHECK_EQ(status.playing, 1u);
    AUREA_CHECK(!(status.previewBufferStatus & 0x80000000u));
    AUREA_CHECK_EQ(warm.backend->offscreenFrames, 3u); // Play reused the entire prepared window
    AUREA_CHECK_EQ(warm.backend->acquires, 2u);
    AUREA_CHECK(warm.engine.renderer().last_preview_cache_hit());
}

AUREA_TEST(PreviewIdle, BackgroundThreadStopsSubmittingOnceItsWindowIsFull) {
    IdlePreviewFixture fixture;
    fixture.engine.start_render_thread();
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(4);
    i64 ranges[60]{}; bool full = false;
    while (std::chrono::steady_clock::now() < deadline) {
        full = fixture.engine.copy_preview_buffer_ranges(ranges, 30) == 1 && ranges[0] == 0 && ranges[1] == 4;
        if (full) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    AUREA_CHECK(full);
    // Let the next paced wake observe completion, then inspect only atomics
    // while the render thread is alive. Mock counters are read after joining.
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    const auto before = fixture.engine.render_wakeups();
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    AUREA_CHECK(fixture.engine.render_wakeups() - before <= 1);
    fixture.engine.stop_render_thread();
    AUREA_CHECK_EQ(fixture.backend->offscreenFrames, 3u);
    AUREA_CHECK_EQ(fixture.engine.read_status().playhead.value, 0ll);
}

AUREA_TEST(PreviewIdle, FailedFutureUploadNeverEntersCacheAndWaitsForReadiness) {
    IdlePreviewFixture fixture;
    fixture.add_future_image();
    AUREA_CHECK(fixture.engine.render_frame().ok());
    bool blocked = true;
    fixture.backend->beforeTextureUpload = [&](TextureHandle, const void*, u32) {
        return blocked ? Status{Errc::OutOfDeviceMemory} : OkStatus;
    };
    fixture.settle();
    AUREA_CHECK(fixture.engine.render_frame(true).ok());
    AUREA_CHECK(!fixture.engine.renderer().preview_cached(FrameIndex{1}));
    AUREA_CHECK(fixture.engine.renderer().preview_cached(FrameIndex{0}));
    const u32 submitted = fixture.backend->framesSubmitted;
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    for (u32 i = 0; i < 10; ++i) AUREA_CHECK(fixture.engine.render_frame(true).ok());
    AUREA_CHECK_EQ(fixture.backend->framesSubmitted, submitted);
    blocked = false;
    void (*ready)(void*) = nullptr; void* context = nullptr;
    fixture.engine.media().ready_callback(ready, context);
    AUREA_CHECK(ready != nullptr); if (ready) ready(context);
    fixture.fill(4);
    AUREA_CHECK_EQ(fixture.backend->acquires, 1u);
    AUREA_CHECK_EQ(fixture.engine.read_status().playhead.value, 0ll);
}

AUREA_TEST(PreviewIdle, SeekAndEditDuringPreparationDiscardTheOldGeneration) {
    for (const bool seek : {true, false}) {
        IdlePreviewFixture fixture;
        const u64 image = fixture.add_future_image();
        AUREA_CHECK(fixture.engine.render_frame().ok());
        fixture.settle();
        std::promise<void> entered, release;
        auto enteredFuture = entered.get_future(); auto releaseFuture = release.get_future();
        bool first = true;
        fixture.backend->beforeTextureUpload = [&](TextureHandle, const void*, u32) {
            if (first) { first = false; entered.set_value(); releaseFuture.wait(); }
            return OkStatus;
        };
        auto rendering = std::async(std::launch::async, [&] { return fixture.engine.render_frame(true); });
        const bool reached = enteredFuture.wait_for(std::chrono::seconds(3)) == std::future_status::ready;
        AUREA_CHECK(reached);
        if (reached) {
            Command command;
            if (seek) { command.type = CommandType::PlaybackSeek; command.seek.time = tick_at(FrameIndex{45}, 30.); }
            else {
                command.type = CommandType::LayerLayoutTransform;
                command.shape_param = {LayerId::unpack(image), 0, 19};
            }
            // Model commands do not wait on the speculative GPU recording.
            auto changing = std::async(std::launch::async, [&] { return fixture.engine.apply_command(command); });
            const bool quick = changing.wait_for(std::chrono::seconds(1)) == std::future_status::ready;
            release.set_value();
            AUREA_CHECK(quick); AUREA_CHECK(changing.get().ok());
        } else release.set_value();
        AUREA_CHECK(rendering.get().ok());
        fixture.backend->beforeTextureUpload = {};
        // A seek keeps the prepared frame (same revision); an edit discards it.
        AUREA_CHECK_EQ(fixture.engine.renderer().preview_cached_count(), seek ? 2u : 0u);
        AUREA_CHECK_EQ(fixture.backend->acquires, 1u);
        AUREA_CHECK(fixture.engine.render_frame(true).ok());
        AUREA_CHECK(fixture.engine.renderer().preview_cached(FrameIndex{seek ? 45 : 0}));
        AUREA_CHECK_EQ(fixture.engine.renderer().preview_cached(FrameIndex{1}), seek);
    }
}

AUREA_TEST(PreviewIdle, NoCapacityAndThermalPressureDoNotSubmitSpeculativeFrames) {
    for (const bool heated : {false, true}) {
        IdlePreviewFixture fixture(heated ? 4 : 0);
        AUREA_CHECK(fixture.engine.render_frame().ok());
        if (heated) {
            fixture.engine.set_thermal(static_cast<u32>(ThermalState::Level::Serious), true);
            AUREA_CHECK(fixture.engine.render_frame().ok());
        }
        fixture.settle();
        for (u32 i = 0; i < 5; ++i) AUREA_CHECK(fixture.engine.render_frame(true).ok());
        AUREA_CHECK_EQ(fixture.backend->offscreenFrames, 0u);
        AUREA_CHECK_EQ(fixture.engine.read_status().playhead.value, 0ll);
    }
}
