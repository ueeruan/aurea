// Ready proxies and final captures intentionally want different decoder paths.
// The synthetic proxy has different pixels so accidentally accepting it cannot
// satisfy the real-GPU readback contract.
#if defined(AUREA_TEST_VULKAN)
namespace {
struct CaptureProxySink final : ExportSink {
    std::ofstream file;
    Status open(const char* path, const VideoStreamConfig&, const AudioStreamConfig*) noexcept override {
        file.open(path, std::ios::binary); return file ? OkStatus : Status{Errc::IoError};
    }
    Status write_video(const u8* y, u32, const u8*, u32, i64) noexcept override { file.put(char(*y)); return OkStatus; }
    Status write_audio(const i16*, u32, i64) noexcept override { return OkStatus; }
    Status finish() noexcept override { file.close(); return OkStatus; }
    void abort() noexcept override { file.close(); }
};
std::unique_ptr<ExportSink> capture_proxy_sink(void*) { return std::make_unique<CaptureProxySink>(); }

struct CaptureProxyFactory final : VideoSourceFactory {
    SyntheticConfig config;
    CaptureDecodeGate gate;
    std::atomic<u32> originalOpens{0}, proxyOpens{0};
    CaptureProxyFactory() {
        config.width = 96; config.height = 54; config.frameCount = 8;
        config.pattern = SyntheticPattern::FrameGray;
    }
    bool probe(const char* path, MediaProbe& out) override { SyntheticFactory probe(config); return probe.probe(path, out); }
    std::unique_ptr<VideoDecoderBackend> open_video(const Asset& asset, MediaPriority priority) override {
        if (priority == MediaPriority::Thumbnail) return std::make_unique<SyntheticDecoder>(config);
        if (asset.sourcePath == "capture-proxy-original") {
            ++originalOpens; return std::make_unique<CaptureGatedDecoder>(config, gate);
        }
        ++proxyOpens;
        auto proxy = config; proxy.width = asset.video.width; proxy.height = asset.video.height;
        proxy.pattern = SyntheticPattern::Quadrants;
        return std::make_unique<SyntheticDecoder>(proxy);
    }
};
}

AUREA_TEST(CaptureProxyGpu, ReadyProxyCannotReplaceOriginalWhileFinalCaptureWaits) {
    AUREA_REQUIRE_GPU();
    CaptureValidationGuard validation;
    CaptureProxyFactory factory;
    Engine e;
    EngineConfig cfg; cfg.backend = new vk::Backend(); cfg.backendConfig.enableValidation = true;
    cfg.backendConfig.framesInFlight = 2; cfg.mediaFactory = &factory; cfg.workerCount = 2; cfg.disableAutosave = true;
    AUREA_CHECK(e.initialize(cfg).ok());
    AUREA_CHECK(e.new_project(96, 54, 30, "proxy capture").ok());
    VideoImport video; video.sourcePath = "capture-proxy-original"; video.displayName = "original";
    const auto id = e.import_video(video);
    AUREA_CHECK(id.ok()); if (!id.ok()) return;
    const auto* comp = e.project()->timeline().composition(e.project()->timeline().current());
    const auto* layer = comp->layer(LayerId::unpack(*id));
    const Asset asset = *e.project()->asset(layer->source);
    const auto directory = std::filesystem::absolute("../build/reference/heavy-stress-20261005") /
        ("capture-proxy-" + std::to_string(monotonic_ns()));
    e.media().proxies().configure(&factory, capture_proxy_sink, nullptr, directory.generic_string());
    e.media().proxies().set_policy(26);
    std::shared_ptr<const PreviewProxy> proxy;
    AUREA_CHECK(capture_epoch_wait([&] { proxy = e.media().proxies().request(asset); return !!proxy; }, 3000));
    if (!proxy) return;
    VideoSource* initial = nullptr;
    AUREA_CHECK(capture_epoch_wait([&] {
        initial = e.media().source_for(LayerId::unpack(*id), layer->source, asset, 1, false); return initial != nullptr;
    }, 1000));
    AUREA_CHECK_EQ(factory.proxyOpens.load(), 1u);
    factory.gate.blocked = true;
    AUREA_CHECK(capture_epoch_seek(e, 3));
    std::vector<u8> rgba; u32 width = 0, height = 0;
    Status result{Errc::InvalidState}; std::atomic<bool> done{false};
    std::thread capture([&] { result = e.capture_frame_rgba(96, rgba, width, height); done = true; });
    const bool waiting = capture_epoch_wait([&] { return factory.gate.entered.load() > 0; }, 1500);
    u32 previews = 0;
    for (; previews < 20 && !done.load(); ++previews) {
        AUREA_CHECK(e.render_frame(false).ok());
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    const u32 originalsWhileWaiting = factory.originalOpens.load(), proxiesWhileWaiting = factory.proxyOpens.load();
    factory.gate.blocked = false;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!done.load() && std::chrono::steady_clock::now() < deadline) {
        AUREA_CHECK(e.render_frame(false).ok()); ++previews;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    capture.join();
    std::printf("    capture with ready proxy: originals=%u proxies=%u during wait=%u/%u previews=%u status=%d\n",
        factory.originalOpens.load(), factory.proxyOpens.load(), originalsWhileWaiting, proxiesWhileWaiting, previews, result.raw());
    AUREA_CHECK(waiting); AUREA_CHECK(previews >= 20);
    AUREA_CHECK_EQ(originalsWhileWaiting, 1u); AUREA_CHECK_EQ(proxiesWhileWaiting, 1u);
    AUREA_CHECK_EQ(factory.originalOpens.load(), 1u);
    AUREA_CHECK(result.ok()); AUREA_CHECK_EQ(width, 96u); AUREA_CHECK_EQ(height, 54u);
    AUREA_CHECK_EQ(rgba.size(), usize(96 * 54 * 4));
    if (rgba.size() == 96 * 54 * 4) {
        const f32 expected = (frame_gray_code(3) - 16.f) * 255.f / 219.f;
        for (u32 y : {13u, 40u}) for (u32 x : {24u, 72u}) {
            const usize p = (y * 96 + x) * 4;
            for (u32 c = 0; c < 3; ++c) AUREA_CHECK_NEAR(rgba[p + c], expected, 1.5);
            AUREA_CHECK_EQ(rgba[p + 3], 255u);
        }
    }
    e.shutdown();
}
#endif
