#if defined(AUREA_TEST_VULKAN) || defined(AUREA_TEST_METAL)

#include "OldKeyframeProject.hpp"
#include "aurea/project/Serialization.hpp"

AUREA_TEST(ExportSetupErrors, TextureAndReadbackFailuresKeepTheirActualCause) {
    for (const bool texture : {true, false}) {
        for (const Errc expected : {Errc::NotSupported, Errc::DeviceLost, Errc::OutOfDeviceMemory, Errc::BudgetExceeded}) {
            SyntheticConfig cfg; cfg.width = 128; cfg.height = 72; cfg.frameCount = 3;
            auto* backend = new MockBackend(); backend->mapBuffers = true;
            if (texture) backend->beforeCreateTexture = [expected](const TextureDesc& description) {
                return description.debugName && std::strcmp(description.debugName, "export-y") == 0
                    ? Status{expected, "injected export Y-plane allocation failure"} : OkStatus;
            };
            else backend->beforeCreateBuffer = [expected](const BufferDesc& description) {
                return description.access == MemoryAccess::Readback
                    ? Status{expected, "injected export readback allocation failure"} : OkStatus;
            };
            Rig r(cfg, 30, 3, 1, nullptr, backend);
            AUREA_CHECK(r.ok); if (!r.ok) return;
            ExportSettings settings; settings.height = 72; settings.fps = 30; settings.dither = false;
            const Status started = r.e.start_export(settings, "injected-setup-failure.mp4");
            std::printf("setup-error route=%s expected=%d returned=%d detail=%.*s ",
                texture ? "texture" : "readback", static_cast<int>(expected), started.raw(),
                static_cast<int>(started.detail().size()), started.detail().data());
            AUREA_CHECK_EQ(started.code(), expected);
            AUREA_CHECK(started.detail().find("injected export") != std::string_view::npos);
            AUREA_CHECK_EQ(r.cap.writes, 0u);
            AUREA_CHECK(r.cap.aborted && !r.cap.finished);
        }
    }
}

// Export-only fault injection. The backend returns its documented Timeout;
// this exercises Engine's first-frame fence admission, not an OEM GPU driver
// that never returns from its API. Pixel transfer is covered by the real
// Vulkan/NV12 and independent native codec suites.
AUREA_TEST(ExportTransferDeadline, FirstFenceTimeoutFinishesWithoutEncodingAndKeepsPendingResources) {
    SyntheticConfig cfg; cfg.width = 128; cfg.height = 72; cfg.frameCount = 3;
    auto* backend = new MockBackend(); backend->mapBuffers = true;
    std::atomic<bool> completed{false}, waiting{false};
    std::atomic<u64> longestRequestedWait{0};
    backend->beforeWaitFrame = [&](u64 frame, u64 timeout) {
        if (!frame || completed.load(std::memory_order_acquire)) return OkStatus;
        if (timeout) {
            waiting.store(true, std::memory_order_release);
            u64 observed = longestRequestedWait.load();
            while (observed < timeout && !longestRequestedWait.compare_exchange_weak(observed, timeout)) {}
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        return Status{Errc::Timeout, "injected first export fence did not signal"};
    };
    Rig r(cfg, 30, 3, 1, nullptr, backend, 0, false, "sintetico",
        ExportExecutionProfile::Balanced, false, nullptr, nullptr, false, 0, true, 200);
    struct ReleaseOnExit {
        std::atomic<bool>& done;
        ~ReleaseOnExit() { done.store(true, std::memory_order_release); }
    } release{completed};
    AUREA_CHECK(r.ok); if (!r.ok) return;
    r.comp()->layer(r.video_layer())->visible = false;
    const Outcome outcome = run_export(r, 72, 30, false, 3);
    std::printf("first-fence result=%d elapsed=%.3fs encoded=%u max_wait_ns=%llu ",
        static_cast<int>(outcome.p.result), outcome.seconds, r.cap.writes,
        static_cast<unsigned long long>(longestRequestedWait.load()));
    AUREA_CHECK(waiting.load());
    AUREA_CHECK(outcome.finished);
    AUREA_CHECK_EQ(outcome.p.result, Errc::Timeout);
    AUREA_CHECK_EQ(outcome.p.framesDone, 0u);
    AUREA_CHECK_EQ(r.cap.writes, 0u);
    AUREA_CHECK(r.cap.aborted && !r.cap.finished);
    AUREA_CHECK(outcome.seconds >= .18 && outcome.seconds < 2.0);
    AUREA_CHECK(longestRequestedWait.load() <= 100'000'000ull);
    AUREA_CHECK_EQ(outcome.p.flags & kExportRetryMask, 0u);
    const u32 textures = backend->texturesCreated, submitted = backend->framesSubmitted;
    const u32 idleWaits = backend->idleWaits.load();
    ExportSettings settings; settings.height = 72; settings.fps = 30; settings.dither = false;
    AUREA_CHECK_EQ(r.e.start_export(settings, "first-fence-still-pending.mp4").code(), Errc::Timeout);
    AUREA_CHECK_EQ(backend->texturesCreated, textures);
    AUREA_CHECK_EQ(backend->framesSubmitted, submitted);
    AUREA_CHECK_EQ(backend->idleWaits.load(), idleWaits);
    AUREA_CHECK(!backend->deviceLost);
    completed.store(true, std::memory_order_release);
    const Outcome retry = run_export(r, 72, 30, false, 3);
    AUREA_CHECK(retry.finished && retry.p.result == Errc::Ok);
    AUREA_CHECK_EQ(retry.p.framesDone, 3u);
    AUREA_CHECK_EQ(r.cap.writes, 3u);
    AUREA_CHECK(r.cap.finished && !backend->deviceLost);
}

AUREA_TEST(ExportTransferDeadline, ValidFirstFenceDelayIsAcceptedBeforeDeadline) {
    SyntheticConfig cfg; cfg.width = 128; cfg.height = 72; cfg.frameCount = 3;
    auto* backend = new MockBackend(); backend->mapBuffers = true;
    std::atomic<u64> firstWaitNs{0};
    std::atomic<u32> polls{0};
    backend->beforeWaitFrame = [&](u64 frame, u64 timeout) {
        if (!frame || !timeout) return OkStatus;
        u64 began = firstWaitNs.load(std::memory_order_acquire);
        if (!began) { began = monotonic_ns(); firstWaitNs.store(began, std::memory_order_release); }
        ++polls;
        if (monotonic_ns() - began < 75'000'000ull) {
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
            return Status{Errc::Timeout};
        }
        return OkStatus;
    };
    Rig r(cfg, 30, 3, 1, nullptr, backend, 0, false, "sintetico",
        ExportExecutionProfile::Balanced, false, nullptr, nullptr, false, 0, true, 400);
    AUREA_CHECK(r.ok); if (!r.ok) return;
    r.comp()->layer(r.video_layer())->visible = false;
    const Outcome outcome = run_export(r, 72, 30, false, 3);
    std::printf("delayed-first-fence result=%d elapsed=%.3fs encoded=%u polls=%u ",
        static_cast<int>(outcome.p.result), outcome.seconds, r.cap.writes, polls.load());
    AUREA_CHECK(outcome.finished && outcome.p.result == Errc::Ok);
    AUREA_CHECK_EQ(outcome.p.framesDone, 3u);
    AUREA_CHECK_EQ(r.cap.writes, 3u);
    AUREA_CHECK(r.cap.finished && !r.cap.aborted);
    AUREA_CHECK(polls.load() > 1u);
    AUREA_CHECK(outcome.seconds >= .06 && outcome.seconds < 2.0);
    AUREA_CHECK_EQ(r.cap.pts.size(), usize{3});
    for (usize index = 0; index < r.cap.pts.size(); ++index)
        AUREA_CHECK_EQ(r.cap.pts[index], static_cast<i64>(std::llround(index * 1e6 / 30)));
}

#if defined(AUREA_HAS_FFMPEG) && defined(AUREA_TEST_DATA_DIR)
AUREA_TEST(ExportLegacyNativeGpu, HistoricalTimelineV34ExportsItsOriginalAnimated3DScene) {
    AUREA_CHECK(gpu_ok()); if (!gpu_ok()) return;
    // These are committed bytes produced by the earlier engine, not a current
    // serializer whose version field was changed. Keep both fixture and movie
    // inside the workspace's raw-reference directory for independent inspection.
    const auto workspace = std::filesystem::path(AUREA_TEST_DATA_DIR).parent_path().parent_path().parent_path();
    const auto folder = workspace / "build/reference/astra-export-20261013/gpu-legacy";
    std::error_code error;
    std::filesystem::create_directories(folder, error);
    AUREA_CHECK(!error); if (error) return;
    const std::string path = (folder / "timeline-v34-original.aurea").string();
    const std::string movie = (folder / "timeline-v34-export.mp4").string();
    AUREA_CHECK(fileio::write_atomic(path.c_str(), kOldProjectTimelineV34Keys,
                                    sizeof(kOldProjectTimelineV34Keys)).ok());
    Project decodedProject; LoadReport report;
    const Status decoded = ProjectSerializer::load(decodedProject, path, LoadOptions{}, &report);
    AUREA_CHECK(decoded.ok()); AUREA_CHECK_EQ(report.timelineVersion, 34u);
    AUREA_CHECK(report.olderFormat && report.sectionsCorrupt.empty());
    if (!decoded.ok()) return;
    SyntheticConfig cfg; cfg.width = 320; cfg.height = 180;
    Rig r(cfg, 30, 3, 1, nullptr, nullptr, 0, false, "sintetico",
        ExportExecutionProfile::Balanced, false, nullptr, nullptr, false, 0, true, 30000,
        &make_ffmpeg_export_sink);
    AUREA_CHECK(r.ok); if (!r.ok) return;
    AUREA_CHECK(r.e.load_project(path.c_str()).ok());
    AUREA_CHECK((r.e.last_load_notice() & Engine::kLoadOlderFormat) != 0u);
    const Composition* composition = r.comp();
    AUREA_CHECK(composition != nullptr); if (!composition) return;
    const i64 originalDuration = composition->duration().value;
    const f64 fps = composition->fps();
    const u32 layerCount = composition->order().size();
    u32 animated3d = 0;
    composition->layers().for_each([&](LayerId, const Layer& layer) {
        if (layer.threeD && layer.tracks.size() > 0) ++animated3d;
    });
    AUREA_CHECK(animated3d > 0u);
    AUREA_CHECK(originalDuration > 0 && originalDuration <= 1800);
    if (originalDuration <= 0 || originalDuration > 1800) return;
    ExportSettings settings; settings.height = 144; settings.fps = fps;
    settings.dither = false; settings.trimToContent = false;
    const auto size = export_frame_size(composition->width(), composition->height(), settings.height);
    const Status started = r.e.start_export(settings, movie.c_str());
    AUREA_CHECK(started.ok()); if (!started.ok()) return;
    const u64 began = monotonic_ns();
    const u64 deadline = began + 120'000'000'000ull;
    while (!r.e.export_progress().finished && monotonic_ns() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    const auto progress = r.e.export_progress();
    std::printf("legacy-native timeline=34 frames=%lld layers=%u animated3d=%u elapsed=%.3fs result=%d ",
        static_cast<long long>(originalDuration), layerCount, animated3d,
        static_cast<double>(monotonic_ns() - began) / 1e9, static_cast<int>(progress.result));
    AUREA_CHECK(progress.finished && progress.result == Errc::Ok);
    AUREA_CHECK_EQ(progress.framesDone, static_cast<u32>(originalDuration));
    AUREA_CHECK_EQ(progress.framesTotal, static_cast<u32>(originalDuration));
    AUREA_CHECK_EQ(r.comp()->duration().value, originalDuration);
    AUREA_CHECK_EQ(r.comp()->order().size(), layerCount);
    if (progress.finished && progress.result == Errc::Ok) {
        const auto pictures = verify_native_export(movie, static_cast<u32>(originalDuration),
            size.width, size.height, fps, false);
        AUREA_CHECK(pictures.ok);
        AUREA_CHECK(pictures.contrastingFrames > 0u);
        AUREA_CHECK(pictures.distinctFrameHashes > 1u);
    }
    if (!progress.finished) (void)r.e.cancel_export();
    std::vector<u8> publishedBefore, publishedAfter;
    if (progress.finished && progress.result == Errc::Ok)
        AUREA_CHECK(fileio::read_all(movie.c_str(), publishedBefore, 16u << 20));
    r.e.shutdown();
    if (progress.finished && progress.result == Errc::Ok) {
        AUREA_CHECK(fileio::read_all(movie.c_str(), publishedAfter, 16u << 20));
        AUREA_CHECK(!publishedBefore.empty() && publishedAfter == publishedBefore);
    }
    std::vector<u8> preserved;
    AUREA_CHECK(fileio::read_all(path.c_str(), preserved, sizeof(kOldProjectTimelineV34Keys)));
    AUREA_CHECK_EQ(preserved.size(), sizeof(kOldProjectTimelineV34Keys));
    AUREA_CHECK(preserved.size() == sizeof(kOldProjectTimelineV34Keys) &&
        std::memcmp(preserved.data(), kOldProjectTimelineV34Keys, preserved.size()) == 0);
}
#endif

#endif
