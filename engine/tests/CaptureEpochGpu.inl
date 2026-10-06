// Capture and preview share asynchronous VideoSources. Exercise that boundary
// with actual NV12 frames and Vulkan readback, not a mocked render result.
#if defined(AUREA_TEST_VULKAN)
namespace {
struct CaptureValidationGuard {
#if !defined(AUREA_TEST_GLES)
    u32 errors = vk::Backend::validation_errors();
    ~CaptureValidationGuard() { AUREA_CHECK_EQ(vk::Backend::validation_errors() - errors, 0u); }
#endif
};

struct CaptureDecodeGate {
    std::atomic<bool> blocked{false};
    std::atomic<u32> entered{0};
};

class CaptureGatedDecoder final : public VideoDecoderBackend {
public:
    CaptureGatedDecoder(const SyntheticConfig& cfg, CaptureDecodeGate& gate) : decoder_(cfg), gate_(gate) {}
    const VideoStreamInfo& info() const noexcept override { return decoder_.info(); }
    Status seek_to_keyframe(i64 us) noexcept override { return decoder_.seek_to_keyframe(us); }
    Status next_frame(i64 from, FrameRef& frame, i64& pts, bool& eos) noexcept override {
        if (gate_.blocked.load()) {
            ++gate_.entered;
            // Bounded even when an assertion fails: a regression must not hang
            // decoder teardown and the remainder of the test executable.
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(8);
            while (gate_.blocked.load() && std::chrono::steady_clock::now() < deadline)
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        return decoder_.next_frame(from, frame, pts, eos);
    }
    u32 max_live_frames() const noexcept override { return decoder_.max_live_frames(); }
    i64 keyframe_interval_us() const noexcept override { return decoder_.keyframe_interval_us(); }
private:
    SyntheticDecoder decoder_;
    CaptureDecodeGate& gate_;
};

class CaptureGatedFactory final : public VideoSourceFactory {
public:
    explicit CaptureGatedFactory(const SyntheticConfig& cfg) : cfg_(cfg), probe_(cfg) {}
    bool probe(const char* path, MediaProbe& out) override { return probe_.probe(path, out); }
    std::unique_ptr<VideoDecoderBackend> open_video(const Asset&, MediaPriority) override {
        return std::make_unique<CaptureGatedDecoder>(cfg_, gate);
    }
    CaptureDecodeGate gate;
private:
    SyntheticConfig cfg_;
    SyntheticFactory probe_;
};

bool capture_epoch_setup(Engine& e, VideoSourceFactory& factory, u32 layers = 1) {
    EngineConfig config;
    config.backend = new vk::Backend();
    config.backendConfig.framesInFlight = 2;
    config.backendConfig.enableValidation = true;
    config.mediaFactory = &factory;
    config.workerCount = 2;
    config.disableAutosave = true;
    if (!e.initialize(config).ok() || !e.new_project(1920, 1080, 30, "capture epochs").ok()) return false;
    for (u32 i = 0; i < layers; ++i) {
        VideoImport video;
        video.sourcePath = "capture-epoch-source-" + std::to_string(i);
        video.displayName = video.sourcePath;
        const auto id = e.import_video(video);
        if (!id.ok()) return false;
        auto* comp = e.project()->timeline().composition(e.project()->timeline().current());
        auto* layer = comp->layer(LayerId::unpack(*id));
        if (layers == 4) {
            layer->transform.anchor = Vec3{48, 27, 0};
            layer->transform.position = Vec3{480.f + 960.f * (i % 2), 270.f + 540.f * (i / 2), 0};
            layer->transform.scale = Vec3{10, 10, 1};
        }
    }
    return true;
}

bool capture_epoch_seek(Engine& e, i64 frame) {
    Command seek;
    seek.type = CommandType::PlaybackSeek;
    seek.seek.time = tick_at(FrameIndex{frame}, 30);
    return e.submit_commands(&seek, 1) == 1;
}

void capture_epoch_check_pixels(const std::vector<u8>& rgba, u32 width, u32 height, u32 frame) {
    AUREA_CHECK_EQ(width, 192u);
    AUREA_CHECK_EQ(height, 108u);
    AUREA_CHECK_EQ(rgba.size(), usize(width) * height * 4);
    if (rgba.size() != usize(width) * height * 4 || !width || !height) return;
    const f32 expected = (frame_gray_code(frame) - 16.f) * 255.f / 219.f;
    for (const u32 y : {height / 4, height * 3 / 4}) for (const u32 x : {width / 4, width * 3 / 4}) {
        const usize p = (usize(y) * width + x) * 4;
        for (u32 channel = 0; channel < 3; ++channel) AUREA_CHECK_NEAR(rgba[p + channel], expected, 1.5);
        AUREA_CHECK_EQ(rgba[p + 3], 255u);
    }
}

template <typename Predicate>
bool capture_epoch_wait(Predicate ready, u32 timeoutMs) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    while (!ready() && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    return ready();
}
} // namespace

AUREA_TEST(CaptureEpochGpu, FourVideoSeekCompletesWhilePreviewRequestsFrames) {
    AUREA_REQUIRE_GPU();
    CaptureValidationGuard validation;
    SyntheticConfig cfg;
    cfg.width = 96; cfg.height = 54; cfg.gop = 90;
    cfg.decodeCostUs = 8'000;
    cfg.pattern = SyntheticPattern::FrameGray;
    SyntheticFactory factory(cfg);
    Engine e;
    const bool setup = capture_epoch_setup(e, factory, 4);
    AUREA_CHECK(setup);
    if (!setup) return;
    std::vector<u8> rgba;
    u32 width = 0, height = 0;
    AUREA_CHECK(e.capture_frame_rgba(192, rgba, width, height).ok());
    capture_epoch_check_pixels(rgba, width, height, 0);
    AUREA_CHECK(capture_epoch_seek(e, 45));
    std::atomic<bool> stop{false};
    std::atomic<u32> ticks{0}, errors{0};
    std::thread preview([&] {
        while (!stop.load()) {
            if (!e.render_frame(false).ok()) ++errors;
            ++ticks;
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    });
    const bool previewRan = capture_epoch_wait([&] { return ticks.load() >= 2; }, 1000);
    const auto start = std::chrono::steady_clock::now();
    rgba.clear();
    const Status result = e.capture_frame_rgba(192, rgba, width, height);
    const auto elapsed = std::chrono::duration<f64, std::milli>(std::chrono::steady_clock::now() - start).count();
    stop = true;
    preview.join();
    const auto media = e.media().stats();
    std::printf("    capture45: %.1f ms, %u preview requests, %llu seeks, status %d\n",
                elapsed, ticks.load(), static_cast<unsigned long long>(media.seeks), result.raw());
    AUREA_CHECK(previewRan);
    AUREA_CHECK(ticks.load() >= 10); // decoder work really overlapped repeated preview preparation
    AUREA_CHECK_EQ(errors.load(), 0u);
    AUREA_CHECK_EQ(media.sources, 4u);
    AUREA_CHECK(result.ok());
    capture_epoch_check_pixels(rgba, width, height, 45);
    e.shutdown();
}

AUREA_TEST(CaptureEpochGpu, NewSeekCancelsOldThumbnailBeforeItsDecoderUnblocks) {
    AUREA_REQUIRE_GPU();
    CaptureValidationGuard validation;
    SyntheticConfig cfg;
    cfg.width = 96; cfg.height = 54; cfg.gop = 90;
    cfg.decodeCostUs = 2'000;
    cfg.pattern = SyntheticPattern::FrameGray;
    CaptureGatedFactory factory(cfg);
    Engine e;
    const bool setup = capture_epoch_setup(e, factory);
    AUREA_CHECK(setup);
    if (!setup) return;
    factory.gate.blocked = true;
    AUREA_CHECK(capture_epoch_seek(e, 30));
    Status oldResult{Errc::InvalidState}, newResult{Errc::InvalidState};
    std::atomic<bool> oldDone{false};
    std::vector<u8> oldRgba, newRgba;
    u32 oldWidth = 0, oldHeight = 0, newWidth = 0, newHeight = 0;
    std::thread oldCapture([&] {
        oldResult = e.capture_frame_rgba(192, oldRgba, oldWidth, oldHeight);
        oldDone = true;
    });
    const bool waiting = capture_epoch_wait([&] { return factory.gate.entered.load() != 0; }, 1500);
    AUREA_CHECK(capture_epoch_seek(e, 45));
    AUREA_CHECK(e.render_frame(false).ok()); // drain the seek while the first capture is waiting
    std::thread newCapture([&] { newResult = e.capture_frame_rgba(192, newRgba, newWidth, newHeight); });
    const bool cancelledWhileBlocked = capture_epoch_wait([&] { return oldDone.load(); }, 500);
    factory.gate.blocked = false;
    oldCapture.join();
    newCapture.join();
    AUREA_CHECK(waiting);
    AUREA_CHECK(cancelledWhileBlocked);
    AUREA_CHECK_EQ(oldResult.code(), Errc::Cancelled);
    AUREA_CHECK(oldRgba.empty());
    AUREA_CHECK(newResult.ok());
    capture_epoch_check_pixels(newRgba, newWidth, newHeight, 45);
    e.shutdown();
}

AUREA_TEST(CaptureEpochGpu, NaturalPlaybackAdvanceDoesNotCancelBoundCapture) {
    AUREA_REQUIRE_GPU();
    CaptureValidationGuard validation;
    SyntheticConfig cfg;
    cfg.width = 96; cfg.height = 54;
    cfg.pattern = SyntheticPattern::FrameGray;
    CaptureGatedFactory factory(cfg);
    Engine e;
    const bool setup = capture_epoch_setup(e, factory);
    AUREA_CHECK(setup);
    if (!setup) return;
    factory.gate.blocked = true;
    Status result{Errc::InvalidState};
    std::vector<u8> rgba;
    u32 width = 0, height = 0;
    std::thread capture([&] { result = e.capture_frame_rgba(192, rgba, width, height); });
    const bool waiting = capture_epoch_wait([&] { return factory.gate.entered.load() != 0; }, 1500);
    Command play;
    play.type = CommandType::PlaybackPlay;
    AUREA_CHECK_EQ(e.submit_commands(&play, 1), 1u);
    // Use the production warm-up and clock, not a seek disguised as progress.
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    i64 advancedTo = 0;
    while (advancedTo < 3 && std::chrono::steady_clock::now() < deadline) {
        AUREA_CHECK(e.render_frame(false).ok());
        advancedTo = e.read_status().playhead.value;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    factory.gate.blocked = false;
    capture.join();
    std::printf("    bound frame0 survived natural playback to frame %lld; status %d\n",
                static_cast<long long>(advancedTo), result.raw());
    AUREA_CHECK(waiting);
    AUREA_CHECK(advancedTo >= 3);
    AUREA_CHECK(result.ok());
    capture_epoch_check_pixels(rgba, width, height, 0);
    e.shutdown();
}
#endif
