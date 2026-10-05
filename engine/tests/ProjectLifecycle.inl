namespace {
struct ProjectCloseGate {
    std::atomic<bool> armed{false}, entered{false}, release{false}, drainedBeforeClose{false};
    std::atomic<bool> suspended{false}, drainedBeforeSuspend{false};
    aurea::test::MockBackend* backend = nullptr;
    u32 previousIdleWaits = 0;
};

class ProjectCloseDecoder final : public VideoDecoderBackend {
public:
    ProjectCloseDecoder(const aurea::test::SyntheticConfig& config, ProjectCloseGate& gate)
        : decoder_(config), gate_(gate) {}
    ~ProjectCloseDecoder() {
        if (!gate_.armed.load()) return;
        gate_.drainedBeforeClose = gate_.backend->idleWaits.load() > gate_.previousIdleWaits;
        gate_.entered = true;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (!gate_.release.load() && std::chrono::steady_clock::now() < deadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    const VideoStreamInfo& info() const noexcept override { return decoder_.info(); }
    Status seek_to_keyframe(i64 time) noexcept override { return decoder_.seek_to_keyframe(time); }
    Status next_frame(i64 time, FrameRef& frame, i64& pts, bool& eos) noexcept override {
        return decoder_.next_frame(time, frame, pts, eos);
    }
    u32 max_live_frames() const noexcept override { return decoder_.max_live_frames(); }
    i64 keyframe_interval_us() const noexcept override { return decoder_.keyframe_interval_us(); }
    void suspend() noexcept override {
        gate_.drainedBeforeSuspend = gate_.backend->idleWaits.load() > gate_.previousIdleWaits;
        gate_.suspended = true;
    }
private:
    aurea::test::SyntheticDecoder decoder_;
    ProjectCloseGate& gate_;
};

class ProjectCloseFactory final : public VideoSourceFactory {
public:
    explicit ProjectCloseFactory(ProjectCloseGate& gate) : gate_(gate), probe_(config_) {}
    bool probe(const char* path, MediaProbe& out) override { return probe_.probe(path, out); }
    std::unique_ptr<VideoDecoderBackend> open_video(const Asset&, MediaPriority) override {
        return std::make_unique<ProjectCloseDecoder>(config_, gate_);
    }
private:
    ProjectCloseGate& gate_;
    aurea::test::SyntheticConfig config_;
    aurea::test::SyntheticFactory probe_;
};

void check_project_transition(bool reopen) {
    const auto path = std::filesystem::temp_directory_path() / "aurea-project-transition-test.aurea";
    if (reopen) {
        Engine writer;
        AUREA_CHECK(writer.initialize(headless_config()).ok());
        AUREA_CHECK(writer.new_project(64, 36, 30., "replacement").ok());
        AUREA_CHECK(writer.save_project(path.string().c_str()).ok());
        writer.shutdown();
    }
    ProjectCloseGate gate;
    ProjectCloseFactory factory(gate);
    auto* backend = new aurea::test::MockBackend();
    gate.backend = backend;
    Engine engine;
    auto config = headless_config();
    config.backend = backend;
    config.mediaFactory = &factory;
    AUREA_CHECK(engine.initialize(config).ok());
    AUREA_CHECK(engine.new_project(64, 36, 30., "old video project").ok());
    VideoImport video;
    video.sourcePath = "lifecycle-synthetic-video";
    video.displayName = "old decoder";
    AUREA_CHECK(engine.import_video(video).ok());
    int window = 0;
    AUREA_CHECK(engine.attach_surface(&window, 64, 36).ok());
    const auto readyDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (!engine.media().stats().cachedFrames && std::chrono::steady_clock::now() < readyDeadline) {
        AUREA_CHECK(engine.render_frame(false).ok());
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    AUREA_CHECK(engine.media().stats().cachedFrames > 0);
    gate.previousIdleWaits = backend->idleWaits.load();
    gate.armed = true;
    auto transition = std::async(std::launch::async, [&] {
        return reopen ? engine.load_project(path.string().c_str())
                      : engine.new_project(64, 36, 30., "replacement");
    });
    const auto closeDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (!gate.entered.load() && std::chrono::steady_clock::now() < closeDeadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    AUREA_CHECK(gate.entered.load());
    AUREA_CHECK(gate.drainedBeforeClose.load());

    // A concurrent preview may request work while the platform decoder is
    // closing, but must not prepare the old project or recreate its sources.
    std::string renderedTitle;
    backend->beforeBeginFrame = [&] { renderedTitle = engine.project()->metadata().title; };
    std::atomic<bool> renderStarted{false};
    auto rendering = std::async(std::launch::async, [&] {
        renderStarted = true;
        return engine.render_frame(false);
    });
    const auto renderDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(1);
    while (!renderStarted.load() && std::chrono::steady_clock::now() < renderDeadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    const bool excluded = rendering.wait_for(std::chrono::milliseconds(50)) == std::future_status::timeout;
    gate.release = true; // release before checking/joining, even on a regression
    AUREA_CHECK(renderStarted.load());
    AUREA_CHECK(excluded);
    AUREA_CHECK(transition.get().ok());
    AUREA_CHECK(rendering.get().ok());
    AUREA_CHECK_EQ(renderedTitle, std::string("replacement"));
    AUREA_CHECK_EQ(engine.media().stats().sources, 0u);
    backend->beforeBeginFrame = {};
    gate.armed = false;
    engine.shutdown();
    if (reopen) { std::error_code error; std::filesystem::remove(path, error); }
}

void check_platform_release_after_gpu(bool suspending) {
    ProjectCloseGate gate;
    ProjectCloseFactory factory(gate);
    auto* backend = new aurea::test::MockBackend();
    gate.backend = backend;
    Engine engine;
    auto config = headless_config(); config.backend = backend; config.mediaFactory = &factory;
    AUREA_CHECK(engine.initialize(config).ok());
    AUREA_CHECK(engine.new_project(64, 36, 30., "native resource lifetime").ok());
    VideoImport video; video.sourcePath = "lifecycle-synthetic-video"; video.displayName = "decoder";
    AUREA_CHECK(engine.import_video(video).ok());
    int window = 0;
    AUREA_CHECK(engine.attach_surface(&window, 64, 36).ok());
    const auto readyDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (!engine.media().stats().cachedFrames && std::chrono::steady_clock::now() < readyDeadline) {
        AUREA_CHECK(engine.render_frame(false).ok());
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    AUREA_CHECK(engine.media().stats().cachedFrames > 0);
    gate.previousIdleWaits = backend->idleWaits.load();
    gate.armed = true;
    gate.release = true;
    std::promise<void> waitEntered, finishGpu;
    auto entered = waitEntered.get_future();
    auto finished = finishGpu.get_future().share();
    std::atomic<bool> firstWait{true};
    backend->beforeWaitIdle = [&] {
        if (!firstWait.exchange(false)) return;
        waitEntered.set_value();
        (void)finished.wait_for(std::chrono::seconds(5));
    };
    auto operation = std::async(std::launch::async, [&] {
        if (suspending) return engine.suspend();
        engine.shutdown();
        return OkStatus;
    });
    const bool waiting = entered.wait_for(std::chrono::seconds(3)) == std::future_status::ready;
    // The simulated GPU is deliberately still busy. Let the decoder worker
    // run: neither platform suspend nor platform destruction may begin yet.
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    const bool retained = !gate.suspended.load() && !gate.entered.load();
    finishGpu.set_value();
    AUREA_CHECK(waiting);
    AUREA_CHECK(retained);
    AUREA_CHECK(operation.get().ok());
    if (suspending) {
        const auto suspendedDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        while (!gate.suspended.load() && std::chrono::steady_clock::now() < suspendedDeadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        AUREA_CHECK(gate.suspended.load());
        AUREA_CHECK(gate.drainedBeforeSuspend.load());
        backend->beforeWaitIdle = {};
        gate.armed = false;
        engine.shutdown();
    } else {
        AUREA_CHECK(gate.entered.load());
        AUREA_CHECK(gate.drainedBeforeClose.load());
    }
}
}

AUREA_TEST(ProjectLifecycle, NewProjectDrainsGpuBeforeDecoderCloseAndExcludesOldPreview) {
    check_project_transition(false);
}
AUREA_TEST(ProjectLifecycle, LoadProjectDrainsGpuBeforeDecoderCloseAndExcludesOldPreview) {
    check_project_transition(true);
}
AUREA_TEST(ProjectLifecycle, SuspendKeepsDecoderAliveUntilGpuDrainCompletes) {
    check_platform_release_after_gpu(true);
}
AUREA_TEST(ProjectLifecycle, ShutdownKeepsDecoderAliveUntilGpuDrainCompletes) {
    check_platform_release_after_gpu(false);
}

AUREA_TEST(ProjectLifecycle, NativeImportCacheIsReleasedOnReloadSuspendAndPressure) {
    const auto path = std::filesystem::temp_directory_path() / "aurea-native-import-cache-test.aurea";
    auto* backend = new aurea::test::MockBackend();
    Engine engine;
    auto config = headless_config(); config.backend = backend;
    AUREA_CHECK(engine.initialize(config).ok());
    AUREA_CHECK(engine.new_project(64, 36, 30., "native imports").ok());
    AUREA_CHECK(engine.save_project(path.string().c_str()).ok());
    u32 retained = 0, released = 0, previousWaits = 0;
    bool drained = true, betweenFrames = true;
    backend->beforeTrimExternalImages = [&] {
        drained = drained && backend->idleWaits.load() > previousWaits;
        betweenFrames = betweenFrames && !backend->frameOpen;
        const u32 count = retained;
        released += count; retained = 0;
        return count;
    };
    const auto seedClosedDecoderBuffers = [&] {
        retained = 12; previousWaits = backend->idleWaits.load();
    };
    seedClosedDecoderBuffers();
    AUREA_CHECK(engine.load_project(path.string().c_str()).ok());
    AUREA_CHECK_EQ(retained, 0u);
    seedClosedDecoderBuffers();
    AUREA_CHECK(engine.suspend().ok());
    AUREA_CHECK_EQ(retained, 0u);
    AUREA_CHECK(engine.resume().ok());
    seedClosedDecoderBuffers();
    (void)engine.trim_memory(80);
    AUREA_CHECK_EQ(retained, 0u);
    seedClosedDecoderBuffers();
    AUREA_CHECK(engine.new_project(64, 36, 30., "replacement").ok());
    AUREA_CHECK_EQ(retained, 0u);
    AUREA_CHECK_EQ(released, 48u);
    AUREA_CHECK(drained);
    AUREA_CHECK(betweenFrames);
    backend->beforeTrimExternalImages = {};
    engine.shutdown();
    std::error_code error; std::filesystem::remove(path, error);
}
