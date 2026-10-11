// The sole offline pipeline admits slow cold sources and bounds stalled sources.
// These tests use the real GPU renderer and a byte-preserving host sink. They
// do not claim native MP4 codec validation (covered by the device suites).
namespace export_source_startup_test {

struct OpeningState {
    std::atomic<bool> release{false}, factoryAlive{true}, armed{false};
    std::atomic<u64> dueNs{0};
    std::atomic<u32> entered{0}, active{0}, returned{0}, ready{0}, offCaller{0};
};
enum class Mode { Immediate, Delayed, Null, Held, Stalled, StalledAndDelayed };

class OpeningFactory final : public VideoSourceFactory {
public:
    OpeningFactory(const SyntheticConfig& cfg, Mode mode)
        : cfg_(cfg), mode_(mode), caller_(std::this_thread::get_id()) {}
    ~OpeningFactory() override { state->factoryAlive.store(false); }
    bool probe(const char* path, MediaProbe& out) override {
        return SyntheticFactory(cfg_).probe(path, out);
    }
    std::unique_ptr<audio::AudioDecoderBackend> open_audio(const char*) override {
        return cfg_.audioRate ? std::make_unique<SyntheticAudioDecoder>(cfg_) : nullptr;
    }
    std::unique_ptr<VideoDecoderBackend> open_video(const Asset& asset, MediaPriority priority) override {
        // MediaManager's frame source opens with Preview priority even for an
        // export. Arm after import, clear its sources, and cover this actual
        // path; thumbnail/proxy jobs are not the export's blocking opener.
        if (priority == MediaPriority::Thumbnail || !state->armed.load(std::memory_order_acquire))
            return std::make_unique<SyntheticDecoder>(cfg_);
        ++state->entered;
        if (std::this_thread::get_id() != caller_) ++state->offCaller;
        const bool delayed = mode_ == Mode::Delayed || mode_ == Mode::Held ||
            (mode_ == Mode::StalledAndDelayed && asset.sourcePath.find("delayed") != std::string::npos);
        if (delayed) {
            ++state->active;
            // Safety release only for a failing test. Normal release is the
            // explicit scope guard below, before Engine joins its workers.
            const u64 safety = monotonic_ns() + 15'000'000'000ull;
            while (!state->release.load(std::memory_order_acquire) && monotonic_ns() < safety) {
                const u64 due = state->dueNs.load(std::memory_order_acquire);
                if (mode_ != Mode::Held && due && monotonic_ns() >= due) break;
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            --state->active;
        }
        ++state->returned;
        if (mode_ == Mode::Null) return nullptr;
        ++state->ready;
        if (mode_ == Mode::Stalled || (mode_ == Mode::StalledAndDelayed && !delayed))
            return std::make_unique<StallingDecoder>(cfg_, 0);
        return std::make_unique<SyntheticDecoder>(cfg_);
    }
    void arm(u64 delayNs) {
        state->dueNs.store(monotonic_ns() + delayNs, std::memory_order_release);
        state->armed.store(true, std::memory_order_release);
    }
    std::shared_ptr<OpeningState> state = std::make_shared<OpeningState>();
private:
    SyntheticConfig cfg_;
    Mode mode_;
    std::thread::id caller_;
};

struct ReleaseBeforeRigDestruction {
    std::shared_ptr<OpeningState> state;
    ~ReleaseBeforeRigDestruction() { state->release.store(true, std::memory_order_release); }
};
struct OwnedFiles {
    static constexpr const char* primary = "aurea_source_startup_stalled.bin";
    static constexpr const char* delayed = "aurea_source_startup_delayed.bin";
    bool create() {
        const char bytes[] = "owned source identity; pixels provided by OpeningFactory";
        return fileio::write_atomic(primary, bytes, sizeof(bytes)).ok() &&
            fileio::write_atomic(delayed, bytes, sizeof(bytes)).ok();
    }
    ~OwnedFiles() {
        for (const char* path : {primary, delayed, "nao-usado.mp4", "nao-usado.mp4.aurea-export",
                                "nao-usado.mp4.aurea-export.project.aurea"})
            (void)fileio::remove_file(path);
    }
};
SyntheticConfig config(bool audio = false) {
    SyntheticConfig cfg;
    cfg.width = 64; cfg.height = 36; cfg.pattern = SyntheticPattern::FrameGray;
    cfg.audioRate = audio ? 48000 : 0; cfg.audioSeconds = 1;
    return cfg;
}
bool wait_active(const OpeningState& state) {
    const u64 until = monotonic_ns() + 2'000'000'000ull;
    while (!state.active.load(std::memory_order_acquire) && monotonic_ns() < until)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    return state.active.load(std::memory_order_acquire) > 0;
}
Outcome wait_finished(Rig& rig, u64 startNs, u64 limitNs) {
    Outcome out;
    while (monotonic_ns() - startNs < limitNs) {
        out.p = rig.e.export_progress();
        if (out.p.finished) { out.finished = true; break; }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    out.seconds = static_cast<f64>(monotonic_ns() - startNs) / 1e9;
    return out;
}
void check_media_failure(const Rig& rig, const Outcome& out) {
    AUREA_CHECK(out.finished);
    AUREA_CHECK_EQ(out.p.result, Errc::DecodeFailed);
    AUREA_CHECK_EQ(out.p.failure, static_cast<u32>(ExportFailure::Media));
    AUREA_CHECK(rig.cap.aborted && !rig.cap.finished);
}
struct Captured {
    std::vector<std::vector<u8>> nv12, rgba;
    std::vector<i64> pts;
    i64 audioFrames = 0;
    u64 audioHash = 0;
};
bool export_and_capture(Mode mode, Captured& output) {
    constexpr i64 frames = 4;
    const SyntheticConfig cfg = config(true);
    OpeningFactory factory(cfg, mode);
    Rig rig(cfg, 30, frames, 3, &factory, nullptr, 0, false, OwnedFiles::delayed,
            ExportExecutionProfile::Balanced, false, nullptr, nullptr, false, 0, true, 6000);
    ReleaseBeforeRigDestruction guard{factory.state};
    AUREA_CHECK(rig.ok); if (!rig.ok) return false;
    rig.cap.keepFrames = true;
    rig.cap.measureLifecycle = true;
    rig.cap.persistFinishMarker = true; rig.cap.validateCapturedPlanes = true;
    // Each invocation must publish its own result; a previous marker is
    // not evidence that this export reached finish/validation/publication.
    (void)fileio::remove_file("nao-usado.mp4");
    rig.e.media().close_all();
    factory.arm(mode == Mode::Delayed ? 5'200'000'000ull : 0);
    const Outcome out = run_export(rig, 36, 30, false, 15);
    std::printf("source-open route=offline delayed=%d elapsed=%.3fs ready=%u worker=%u ",
        mode == Mode::Delayed, out.seconds,
        factory.state->ready.load(), factory.state->offCaller.load());
    AUREA_CHECK(out.finished); AUREA_CHECK_EQ(out.p.result, Errc::Ok);
    AUREA_CHECK_EQ(out.p.framesDone, u32{frames}); AUREA_CHECK_EQ(out.p.framesTotal, u32{frames});
    AUREA_CHECK_EQ(out.p.flags & Engine::kExportFrameFallback, 0u);
    AUREA_CHECK(rig.cap.finished);
    AUREA_CHECK_EQ(rig.cap.finishCalls, 1u);
    AUREA_CHECK_EQ(rig.cap.abortBeforeFinish, 0u);
    AUREA_CHECK_EQ(rig.cap.validationCalls, 1u);
    // Successful publication detaches the completed sink without an abort.
    AUREA_CHECK_EQ(rig.cap.abortAfterFinish, 0u);
    const char expectedMarker[] = "finished benchmark sink";
    std::vector<u8> committed;
    const bool published = fileio::read_all("nao-usado.mp4", committed, sizeof(expectedMarker))
        && committed.size() == sizeof(expectedMarker)
        && std::memcmp(committed.data(), expectedMarker, sizeof(expectedMarker)) == 0;
    AUREA_CHECK(published);
    std::printf("source-sink finish=%u validation=%u abort_unfinished=%u cleanup_after_finish=%u published_marker=%d ",
        rig.cap.finishCalls, rig.cap.validationCalls, rig.cap.abortBeforeFinish,
        rig.cap.abortAfterFinish, published);
    AUREA_CHECK_EQ(rig.cap.frames.size(), usize{frames});
    AUREA_CHECK_EQ(rig.cap.video.width, 64u); AUREA_CHECK_EQ(rig.cap.video.height, 36u);
    AUREA_CHECK(rig.cap.ptsMonotonic && rig.cap.audioContiguous && rig.cap.hasAudio);
    AUREA_CHECK_EQ(rig.cap.audioFrames, audio::frame_to_sample(frames, 30));
    AUREA_CHECK(factory.state->entered.load() > 0);
    AUREA_CHECK_EQ(factory.state->offCaller.load(), factory.state->entered.load());
    AUREA_CHECK_EQ(factory.state->active.load(), 0u);
    if (mode == Mode::Delayed) AUREA_CHECK(out.seconds >= 5.1 && out.seconds < 13);
    if (!out.finished || out.p.result != Errc::Ok || rig.cap.frames.size() != usize{frames}) return false;
    output.nv12 = rig.cap.frames; output.pts = rig.cap.pts;
    output.audioFrames = rig.cap.audioFrames; output.audioHash = rig.cap.audioHash;
    for (i64 i = 0; i < frames; ++i) {
        AUREA_CHECK_EQ(output.nv12[static_cast<usize>(i)].size(), usize{64 * 36 * 3 / 2});
        AUREA_CHECK_EQ(output.pts[static_cast<usize>(i)], static_cast<i64>(std::llround(i * 1e6 / 30)));
        Command seek; seek.type = CommandType::PlaybackSeek; seek.seek.time = tick_at(FrameIndex{i}, 30);
        AUREA_CHECK(rig.e.apply_command(seek).ok());
        std::vector<u8> pixels; u32 w = 0, h = 0;
        // Exactly one capture invocation per requested time, no caller retry.
        const Status capture = rig.e.capture_frame_rgba(64, pixels, w, h);
        AUREA_CHECK(capture.ok()); AUREA_CHECK_EQ(w, 64u); AUREA_CHECK_EQ(h, 36u);
        AUREA_CHECK_EQ(pixels.size(), usize{64 * 36 * 4});
        if (!capture.ok()) return false;
        output.rgba.push_back(std::move(pixels));
    }
    AUREA_CHECK(output.nv12[0] != output.nv12[1] && output.nv12[2] != output.nv12[3]);
    AUREA_CHECK(output.rgba[0] != output.rgba[1] && output.rgba[2] != output.rgba[3]);
    return true;
}
} // namespace export_source_startup_test

AUREA_TEST(ExportSourceStartupGpu, ColdOpeningBeyondFourSecondsPreservesEveryPixelPtsAndAudioSample) {
    if (!gpu_ok()) { std::printf("(sem GPU Vulkan: pulado) "); return; }
    using namespace export_source_startup_test;
    OwnedFiles files; AUREA_CHECK(files.create());
    {
        Captured immediate, delayed;
        const bool reference = export_and_capture(Mode::Immediate, immediate);
        const bool candidate = export_and_capture(Mode::Delayed, delayed);
        AUREA_CHECK(reference && candidate);
        if (!reference || !candidate) return;
        AUREA_CHECK(immediate.nv12 == delayed.nv12); AUREA_CHECK(immediate.rgba == delayed.rgba);
        AUREA_CHECK(immediate.pts == delayed.pts);
        AUREA_CHECK_EQ(immediate.audioFrames, delayed.audioFrames);
        AUREA_CHECK_EQ(immediate.audioHash, delayed.audioHash);
    }
}

AUREA_TEST(ExportSourceStartupGpu, FailedOpeningAndReadyStallKeepTheirOwnShortDeadline) {
    if (!gpu_ok()) { std::printf("(sem GPU Vulkan: pulado) "); return; }
    using namespace export_source_startup_test;
    OwnedFiles files; AUREA_CHECK(files.create());
    for (Mode mode : {Mode::Null, Mode::Stalled, Mode::StalledAndDelayed}) {
        OpeningFactory factory(config(), mode);
        Rig rig(config(), 30, 4, 3, &factory, nullptr, 0, false, OwnedFiles::primary,
            ExportExecutionProfile::Balanced, false, nullptr, nullptr, false, 0, true, 500);
        ReleaseBeforeRigDestruction guard{factory.state};
        AUREA_CHECK(rig.ok); if (!rig.ok) continue;
        if (mode == Mode::StalledAndDelayed) {
            VideoImport second; second.sourcePath = OwnedFiles::delayed; second.displayName = "opening separately";
            const auto added = rig.e.import_video(second);
            AUREA_CHECK(added.ok());
            if (!added.ok()) continue;
            // Both sources contribute visible pixels: a full-size opaque
            // second clip must not let an occlusion optimization skip the
            // stalled first source, defeating the independent-clock proof.
            Layer* layer = rig.comp()->layer(LayerId::unpack(*added));
            AUREA_CHECK(layer != nullptr);
            if (layer) {
                layer->transform.scale = Vec3{0.5f, 0.5f, 1.0f};
                layer->transform.position.x = 48.0f;
            }
            AUREA_CHECK(cut_frame_test::set_duration(rig.e, 4));
        }
        rig.e.media().close_all();
        factory.arm(7'000'000'000ull);
        const Outcome out = run_export(rig, 36, 30, false, 13);
        check_media_failure(rig, out);
        std::printf("source-failure mode=%u elapsed=%.3fs active=%u ready=%u ",
            static_cast<u32>(mode), out.seconds, factory.state->active.load(), factory.state->ready.load());
        AUREA_CHECK(out.seconds >= 0.4 && out.seconds < 2.0);
        AUREA_CHECK(factory.state->returned.load() > 0);
        if (mode == Mode::StalledAndDelayed) {
            AUREA_CHECK(factory.state->ready.load() > 0);
            AUREA_CHECK(factory.state->active.load() > 0); // other opening cannot renew this READY deadline
        }
    }
}

AUREA_TEST(ExportSourceStartupGpu, ConfiguredOpeningCeilingDoesNotDestroyThePlatformOpener) {
    if (!gpu_ok()) { std::printf("(sem GPU Vulkan: pulado) "); return; }
    using namespace export_source_startup_test;
    OwnedFiles files; AUREA_CHECK(files.create());
    OpeningFactory factory(config(), Mode::Held);
    Rig rig(config(), 30, 4, 3, &factory, nullptr, 0, false, OwnedFiles::delayed,
        ExportExecutionProfile::Balanced, false, nullptr, nullptr, false, 0, true, 500);
    ReleaseBeforeRigDestruction guard{factory.state};
    AUREA_CHECK(rig.ok); if (!rig.ok) return;
    rig.e.media().close_all(); factory.arm(0);
    const Outcome out = run_export(rig, 36, 30, false, 13);
    check_media_failure(rig, out);
    AUREA_CHECK(out.seconds >= 0.4 && out.seconds < 2.0);
    AUREA_CHECK(factory.state->active.load() > 0 && factory.state->factoryAlive.load());
    AUREA_CHECK_EQ(factory.state->returned.load(), 0u);
    // The synchronous platform open itself is non-preemptible; release it
    // explicitly before Engine teardown joins the still-owned worker.
    factory.state->release.store(true, std::memory_order_release);
    rig.e.shutdown();
    AUREA_CHECK_EQ(factory.state->active.load(), 0u);
    AUREA_CHECK_EQ(factory.state->returned.load(), factory.state->entered.load());
    AUREA_CHECK(factory.state->factoryAlive.load());
}

AUREA_TEST(ExportSourceStartupGpu, CancelReturnsPromptlyWhileTheUninterruptibleOpenerRemainsOwned) {
    if (!gpu_ok()) { std::printf("(sem GPU Vulkan: pulado) "); return; }
    using namespace export_source_startup_test;
    OwnedFiles files; AUREA_CHECK(files.create());
    OpeningFactory factory(config(), Mode::Held);
    Rig rig(config(), 30, 4, 3, &factory, nullptr, 0, false, OwnedFiles::delayed,
        ExportExecutionProfile::Balanced, false, nullptr, nullptr, false, 0, true, 500);
    ReleaseBeforeRigDestruction guard{factory.state};
    AUREA_CHECK(rig.ok); if (!rig.ok) return;
    rig.e.media().close_all(); factory.arm(0);
    ExportSettings settings; settings.height = 36; settings.fps = 30; settings.dither = false;
    AUREA_CHECK(rig.e.start_export(settings, "nao-usado.mp4").ok());
    AUREA_CHECK(wait_active(*factory.state));
    const u64 cancelStart = monotonic_ns();
    AUREA_CHECK(rig.e.cancel_export().ok());
    const Outcome out = wait_finished(rig, cancelStart, 2'000'000'000ull);
    AUREA_CHECK(out.finished); AUREA_CHECK_EQ(out.p.result, Errc::Cancelled);
    AUREA_CHECK(out.seconds < 2.0);
    AUREA_CHECK(rig.cap.aborted && !rig.cap.finished);
    AUREA_CHECK(factory.state->active.load() > 0 && factory.state->factoryAlive.load());
    AUREA_CHECK_EQ(factory.state->returned.load(), 0u);
    factory.state->release.store(true, std::memory_order_release);
    rig.e.shutdown();
    AUREA_CHECK_EQ(factory.state->active.load(), 0u);
    AUREA_CHECK_EQ(factory.state->returned.load(), factory.state->entered.load());
    AUREA_CHECK(factory.state->factoryAlive.load());
}
