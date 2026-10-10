namespace {
void check_capture_resource_retries(bool permanent) {
    auto* mock = new aurea::test::MockBackend();
    Engine e; auto cfg = headless_config(); cfg.backend = mock;
    AUREA_CHECK(e.initialize(cfg).ok());
    AUREA_CHECK(e.new_project(64, 64, 30., "capture resources").ok());
    std::vector<u8> rgba(64 * 64 * 4, 255);
    AUREA_CHECK(e.import_image(rgba.data(), 64, 64, "image").ok());
    TextureDesc desc; desc.width = desc.height = 64; desc.renderTarget = true; desc.format = SurfaceFormat::RGBA16F;
    const auto target = mock->create_texture(desc); AUREA_CHECK(target.ok()); if (!target.ok()) return;
    u32 uploads = 0;
    mock->beforeTextureUpload = [&](TextureHandle, const void*, u32) {
        return ++uploads <= 3 || permanent ? Status{Errc::OutOfDeviceMemory} : OkStatus;
    };
    const auto start = std::chrono::steady_clock::now();
    const auto result = e.render_offscreen(*target, 64, 64);
    const auto elapsed = std::chrono::duration<f64>(std::chrono::steady_clock::now() - start).count();
    std::printf("    capture resource retries: uploads=%u submitted=%u elapsed=%.3fs status=%d\n", uploads, mock->framesSubmitted, elapsed, result.raw());
    if (permanent) {
        AUREA_CHECK_EQ(result.code(), Errc::Timeout);
        AUREA_CHECK(elapsed >= 3.9 && elapsed < 5.5);
    } else { AUREA_CHECK(result.ok()); AUREA_CHECK(uploads >= 4); }
    mock->beforeTextureUpload = {};
    mock->destroy_texture(*target); e.shutdown();
}
}
AUREA_TEST(CaptureResources, RetriesIncompleteImageUploadsBeforeReturningSuccess) { check_capture_resource_retries(false); }
AUREA_TEST(CaptureResources, PermanentMissingImageFailsWithinTheCaptureDeadline) { check_capture_resource_retries(true); }

AUREA_TEST(CaptureResources, ResourceWaitYieldsToSurfaceDetachAndCancelsOnSeek) {
    auto* mock = new aurea::test::MockBackend();
    Engine e; auto cfg = headless_config(); cfg.backend = mock;
    AUREA_CHECK(e.initialize(cfg).ok()); AUREA_CHECK(e.new_project(64, 64, 30., "cancel resources").ok());
    int window = 0; AUREA_CHECK(e.attach_surface(&window, 64, 64).ok());
    std::vector<u8> rgba(64 * 64 * 4, 255);
    AUREA_CHECK(e.import_image(rgba.data(), 64, 64, "pending image").ok());
    TextureDesc desc; desc.width = desc.height = 64; desc.renderTarget = true; desc.format = SurfaceFormat::RGBA16F;
    const auto target = mock->create_texture(desc); AUREA_CHECK(target.ok()); if (!target.ok()) return;
    std::atomic<u32> uploads{0};
    mock->beforeTextureUpload = [&](TextureHandle, const void*, u32) {
        ++uploads; return Status{Errc::OutOfDeviceMemory};
    };
    auto capture = std::async(std::launch::async, [&] { return e.render_offscreen(*target, 64, 64); });
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(1);
    while (!uploads.load() && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    AUREA_CHECK(uploads.load() > 0);
    const auto start = std::chrono::steady_clock::now();
    e.detach_surface(); // surface destruction on the UI thread can pass the capture wait
    AUREA_CHECK(std::chrono::steady_clock::now() - start < std::chrono::milliseconds(250));
    Command seek; seek.type = CommandType::PlaybackSeek; seek.seek.time = tick_at(FrameIndex{3}, 30);
    AUREA_CHECK_EQ(e.submit_commands(&seek, 1), 1u);
    AUREA_CHECK(capture.wait_for(std::chrono::seconds(1)) == std::future_status::ready);
    AUREA_CHECK_EQ(capture.get().code(), Errc::Cancelled);
    mock->beforeTextureUpload = {}; mock->destroy_texture(*target); e.shutdown();
}

namespace {
void check_temporal_capture_upload(u32 mode) {
    aurea::test::SyntheticConfig video;
    video.width = 96; video.height = 54; video.frameCount = 8;
    video.pattern = aurea::test::SyntheticPattern::FrameGray;
    aurea::test::SyntheticFactory factory(video);
    auto* mock = new aurea::test::MockBackend();
    Engine e; auto cfg = headless_config(); cfg.backend = mock; cfg.mediaFactory = &factory;
    AUREA_CHECK(e.initialize(cfg).ok()); AUREA_CHECK(e.new_project(96, 54, 30., "pending next frame").ok());
    VideoImport imported; imported.sourcePath = "exact-next-frame"; imported.displayName = "video";
    const auto id = e.import_video(imported); AUREA_CHECK(id.ok()); if (!id.ok()) return;
    if (mode == 0) AUREA_CHECK(e.set_vector_blur(*id, 1));
    else {
        Command add; add.type = CommandType::EffectAdd; add.effect_add.layer = LayerId::unpack(*id);
        add.effect_add.effectType = e.effects().find_key(mode == 1 ? effect_keys::kTimeWarpRgb : effect_keys::kMotionDetect);
        add.effect_add.index = 0xffffffffu;
        AUREA_CHECK(e.apply_command(add).ok());
    }
    const u32 failedFrame = mode == 0 ? 4 : mode == 1 ? 6 : 2;
    Command seek; seek.type = CommandType::PlaybackSeek; seek.seek.time = tick_at(FrameIndex{3}, 30);
    AUREA_CHECK_EQ(e.submit_commands(&seek, 1), 1u);
    TextureDesc desc; desc.width = 96; desc.height = 54; desc.renderTarget = true; desc.format = SurfaceFormat::RGBA16F;
    const auto target = mock->create_texture(desc); AUREA_CHECK(target.ok()); if (!target.ok()) return;
    u32 nextUploads = 0;
    mock->beforeTextureUpload = [&](TextureHandle texture, const void* data, u32 stride) {
        if (mock->texture_desc(texture).format == SurfaceFormat::R8 && stride == 96
            && static_cast<const u8*>(data)[0] == aurea::test::frame_gray_code(failedFrame)) {
            if (++nextUploads <= 2) return Status{Errc::OutOfDeviceMemory};
        }
        return OkStatus;
    };
    AUREA_CHECK(e.render_offscreen(*target, 96, 54).ok());
    u32 hits = 0, computed = 0; e.flow_cache_stats(hits, computed);
    std::printf("    temporal mode=%u: upload attempts=%u, motion fields computed=%u\n", mode, nextUploads, computed);
    AUREA_CHECK(nextUploads >= 3);
    if (mode == 0) AUREA_CHECK(computed > 0);
    mock->beforeTextureUpload = {}; mock->destroy_texture(*target); e.shutdown();
}
}
AUREA_TEST(CaptureResources, ExactNextFrameUploadCannotSilentlyDisableVectorBlur) { check_temporal_capture_upload(0); }
AUREA_TEST(CaptureResources, ExactRgbChannelUploadCannotSilentlyUseTheCurrentFrame) { check_temporal_capture_upload(1); }
AUREA_TEST(CaptureResources, ExactHistoryUploadCannotSilentlyRemoveMotionDetection) { check_temporal_capture_upload(2); }

AUREA_TEST(CaptureResources, HomeThumbnailScalesExpandedEffectsWithoutChangingExactCapture) {
    for (const bool portrait : {false, true}) {
        u64 previewTileBytes = 0, exactTileBytes = 0;
        for (const bool preview : {true, false}) {
            // The failing Home card contained video. Vector shapes deliberately
            // retain a 1x raster-density floor and do not exercise that path.
            aurea::test::SyntheticConfig video;
            video.width = 1920; video.height = 1080; video.frameCount = 2;
            aurea::test::SyntheticFactory factory(video);
            auto* mock = new aurea::test::MockBackend();
            Engine engine; auto cfg = headless_config(); cfg.backend = mock; cfg.mediaFactory = &factory;
            AUREA_CHECK(engine.initialize(cfg).ok());
            const u32 compW = portrait ? 1080u : 1920u, compH = portrait ? 1920u : 1080u;
            AUREA_CHECK(engine.new_project(compW, compH, 30, "home thumbnail effects").ok());
            VideoImport imported; imported.sourcePath = "home-card-full-HD-video"; imported.displayName = "video";
            const auto id = engine.import_video(imported); AUREA_CHECK(id.ok()); if (!id.ok()) continue;
            auto* comp = engine.project()->timeline().composition(engine.project()->timeline().current());
            auto* layer = comp->layer(LayerId::unpack(*id));
            layer->transform.anchor = Vec3{960, 540, 0};
            layer->transform.position = Vec3{compW * .5f, compH * .5f, 0};
            layer->transform.scale = Vec3{.105f, .105f, 1};
            layer->transform.rotation.z = 90;
            Command add; add.type = CommandType::EffectAdd; add.effect_add.layer = LayerId::unpack(*id);
            add.effect_add.effectType = effect_type_id(effect_keys::kMotionTile); add.effect_add.index = kInvalidIndex;
            AUREA_CHECK(engine.apply_command(add).ok());
            auto& tile = layer->effects.back();
            tile.params[3].constant.v[0] = 150; tile.params[4].constant.v[0] = 150;
            tile.params[5].constant.v[0] = 1; tile.params[10].constant.v[0] = 70;
            std::vector<u8> rgba; u32 width = 0, height = 0;
            const Status status = preview
                ? engine.capture_preview_frame_rgba(320, rgba, width, height)
                : engine.capture_frame_rgba(320, rgba, width, height);
            std::printf("\n    Thumbnail fixture portrait=%d preview=%d status=%d frames=%u textures=%u",
                portrait, preview, status.raw(), mock->framesSubmitted, mock->texturesCreated);
            // MockBackend records real graph passes, but provides no pixel
            // readback. The expected error must occur after rendering, rather
            // than admitting an OOM as a successful thumbnail.
            AUREA_CHECK_EQ(status.code(), Errc::NotSupported);
            AUREA_CHECK(mock->framesSubmitted > 0);
            AUREA_CHECK_EQ(width, portrait ? 180u : 320u);
            AUREA_CHECK_EQ(height, portrait ? 320u : 180u);
            AUREA_CHECK(rgba.empty());
            u64 tileBytes = 0; u32 tiles = 0;
            for (const auto& texture : mock->textures) {
                if (!texture.debugName || std::strcmp(texture.debugName, "motion-tile") != 0) continue;
                ++tiles; tileBytes = std::max(tileBytes, texture.estimated_bytes());
                if (preview) { AUREA_CHECK(texture.width < 640); AUREA_CHECK(texture.height < 640); }
                else AUREA_CHECK(std::max(texture.width, texture.height) >= 1920);
            }
            AUREA_CHECK(tiles > 0);
            if (preview) previewTileBytes = tileBytes; else exactTileBytes = tileBytes;
            engine.shutdown();
        }
        std::printf("\n    Home thumbnail portrait=%d: preview tile=%llu bytes, exact tile=%llu bytes",
            portrait, static_cast<unsigned long long>(previewTileBytes), static_cast<unsigned long long>(exactTileBytes));
        AUREA_CHECK(previewTileBytes > 0 && exactTileBytes > 0);
        // Texel-density steps differ by four here; integer plane extents round
        // upward, so their area approaches 1/16 with a one-texel margin.
        AUREA_CHECK(previewTileBytes <= exactTileBytes / 15);
    }
}

AUREA_TEST(CaptureResources, HomeThumbnailCancelsOnSeekWhileResourceUploadIsPending) {
    auto* mock = new aurea::test::MockBackend();
    Engine engine; auto cfg = headless_config(); cfg.backend = mock;
    AUREA_CHECK(engine.initialize(cfg).ok());
    AUREA_CHECK(engine.new_project(64, 64, 30., "bound home thumbnail").ok());
    std::vector<u8> image(64 * 64 * 4, 255);
    AUREA_CHECK(engine.import_image(image.data(), 64, 64, "pending image").ok());
    std::atomic<u32> attempts{0};
    mock->beforeTextureUpload = [&](TextureHandle, const void*, u32) {
        ++attempts; return Status{Errc::OutOfDeviceMemory};
    };
    auto capture = std::async(std::launch::async, [&] {
        std::vector<u8> pixels; u32 width = 0, height = 0;
        return engine.capture_preview_frame_rgba(32, pixels, width, height);
    });
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(1);
    while (!attempts.load() && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    AUREA_CHECK(attempts.load() > 0);
    Command seek; seek.type = CommandType::PlaybackSeek; seek.seek.time = tick_at(FrameIndex{3}, 30);
    AUREA_CHECK_EQ(engine.submit_commands(&seek, 1), 1u);
    AUREA_CHECK(capture.wait_for(std::chrono::seconds(1)) == std::future_status::ready);
    AUREA_CHECK_EQ(capture.get().code(), Errc::Cancelled);
    mock->beforeTextureUpload = {}; engine.shutdown();
}

AUREA_TEST(CaptureResources, ExactCaptureReleasesAndDrainsUnusedDisplayFramesBeforeGraphAdmission) {
    auto* mock = new aurea::test::MockBackend();
    Engine engine; auto cfg = headless_config(); cfg.backend = mock; cfg.initialPreviewScale = PreviewScale::Full;
    AUREA_CHECK(engine.initialize(cfg).ok());
    AUREA_CHECK(engine.new_project(64, 64, 30., "exact capture display cache").ok());
    const auto shape = engine.add_shape(0); AUREA_CHECK(shape.ok()); if (!shape.ok()) return;
    auto* comp = engine.project()->timeline().composition(engine.project()->timeline().current());
    comp->set_duration(FrameIndex{30});
    engine.renderer().set_preview_cache_budget(3u * 64u * 64u * 4u);
    int window = 0; AUREA_CHECK(engine.attach_surface(&window, 64, 64).ok());
    AUREA_CHECK(engine.render_frame().ok());
    std::this_thread::sleep_for(std::chrono::milliseconds(680));
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (engine.renderer().preview_cached_count() < 3 && std::chrono::steady_clock::now() < deadline) {
        AUREA_CHECK(engine.render_frame(true).ok());
        std::this_thread::sleep_for(std::chrono::milliseconds(35));
    }
    AUREA_CHECK_EQ(engine.renderer().preview_cached_count(), 3u);
    std::vector<usize> displays;
    for (usize i = 0; i < mock->textures.size(); ++i) {
        const auto& desc = mock->textures[i];
        if (mock->textureAlive[i] && desc.format == SurfaceFormat::RGBA8_sRGB)
            std::printf("\n    Live SDR texture %zu: %u x %u name=%s", i, desc.width, desc.height,
                desc.debugName ? desc.debugName : "(none)");
        if (mock->textureAlive[i] && desc.width == 64 && desc.height == 64 && desc.format == SurfaceFormat::RGBA8_sRGB)
            displays.push_back(i);
    }
    AUREA_CHECK_EQ(displays.size(), usize{3});
    const u32 submitted = mock->framesSubmitted;
    bool drainedBeforeCapture = false, displaySeenInAdmission = false; u32 admissionQueries = 0;
    mock->beforeWaitIdle = [&] {
        if (mock->framesSubmitted == submitted) {
            drainedBeforeCapture = engine.renderer().preview_cached_count() == 0;
            for (const usize index : displays) drainedBeforeCapture &= !mock->textureAlive[index];
        }
    };
    mock->queryMemoryStats = [&] {
        GpuMemoryStats stats;
        for (usize i = 0; i < mock->textures.size(); ++i)
            if (mock->textureAlive[i]) stats.usedBytes += mock->textures[i].estimated_bytes();
        stats.reservedBytes = stats.usedBytes;
        if (mock->frameOpen) {
            ++admissionQueries;
            displaySeenInAdmission |= engine.renderer().preview_cached_count() != 0;
            displaySeenInAdmission |= !drainedBeforeCapture;
        }
        return stats;
    };
    engine.renderer().set_tracked_resource_budget(96ull << 20, nullptr, nullptr);
    std::vector<u8> pixels; u32 width = 0, height = 0;
    const Status result = engine.capture_frame_rgba(32, pixels, width, height);
    AUREA_CHECK_EQ(result.code(), Errc::NotSupported); // mock renders but has no readback
    AUREA_CHECK_EQ(width, 32u); AUREA_CHECK_EQ(height, 32u);
    AUREA_CHECK_EQ(mock->framesSubmitted, submitted + 1);
    AUREA_CHECK(drainedBeforeCapture); AUREA_CHECK(admissionQueries > 0);
    AUREA_CHECK(!displaySeenInAdmission); AUREA_CHECK_EQ(engine.renderer().preview_cached_count(), 0u);
    mock->beforeWaitIdle = {}; mock->queryMemoryStats = {};
    AUREA_CHECK(engine.render_frame().ok());
    AUREA_CHECK_EQ(engine.renderer().preview_cached_count(), 1u); // preview regenerates normally
    engine.shutdown();
}

AUREA_TEST(CaptureResources, ExactCaptureKeepsLeasedSamplesAndPrunesOptionalPrefetchBeforeAdmission) {
    aurea::test::SyntheticConfig video; video.frameCount = 8;
    video.pattern = aurea::test::SyntheticPattern::FrameGray;
    aurea::test::SyntheticFactory factory(video);
    auto* mock = new aurea::test::MockBackend();
    Engine engine; auto cfg = headless_config(); cfg.backend = mock; cfg.mediaFactory = &factory;
    AUREA_CHECK(engine.initialize(cfg).ok());
    AUREA_CHECK(engine.new_project(64, 36, 30., "exclusive decoded capture").ok());
    VideoImport imported; imported.sourcePath = "exclusive-decoded-fixture";
    const auto id = engine.import_video(imported); AUREA_CHECK(id.ok()); if (!id.ok()) return;
    const auto* composition = engine.project()->timeline().composition(engine.project()->timeline().current());
    const LayerId layerId = LayerId::unpack(*id);
    const auto* layer = composition->layer(layerId); const auto* asset = engine.project()->asset(layer->source);
    VideoSource* source = nullptr;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(1);
    while (!source && std::chrono::steady_clock::now() < deadline) {
        source = engine.media().source_for(layerId, layer->source, *asset, 1, true);
        if (!source) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    AUREA_CHECK(source != nullptr); if (!source) return;
    aurea::test::SyntheticDecoder fixture(video);
    for (u32 i = 0; i < 4; ++i) {
        FrameRef decoded; i64 pts = 0; bool eos = false;
        AUREA_CHECK(fixture.next_frame(-1, decoded, pts, eos).ok());
        AUREA_CHECK(source->cache().insert(std::move(decoded)));
    }
    bool exact = false;
    FrameRef external = source->frame_for(fixture.pts_of(2), &exact); AUREA_CHECK(exact && external);
    const u64 externalId = external->content_id();
    const usize bytes = static_cast<usize>(external->approx_bytes());
    AUREA_CHECK_EQ(engine.memory().used(MemoryClass::DecodedFrames), 4 * bytes);
    bool prunedBeforeAdmission = true; u32 queries = 0;
    mock->queryMemoryStats = [&] {
        if (mock->frameOpen) {
            ++queries;
            prunedBeforeAdmission &= source->cache().stats().frames == 2
                && engine.memory().used(MemoryClass::DecodedFrames) == 2 * bytes;
        }
        return GpuMemoryStats{};
    };
    engine.renderer().set_tracked_resource_budget(96ull << 20, nullptr, nullptr);
    std::vector<u8> pixels; u32 width = 0, height = 0;
    AUREA_CHECK_EQ(engine.capture_frame_rgba(32, pixels, width, height).code(), Errc::NotSupported);
    AUREA_CHECK(queries > 0 && prunedBeforeAdmission);
    AUREA_CHECK_EQ(source->cache().stats().frames, 2u);
    AUREA_CHECK_EQ(external->content_id(), externalId);
    AUREA_CHECK_EQ(external->planes[0][0], aurea::test::frame_gray_code(2));
    AUREA_CHECK(source->cache().contains(0, 1));
    AUREA_CHECK_EQ(factory.last.load()->seeks.load(), 0u);
    AUREA_CHECK_EQ(factory.last.load()->decoded.load(), 0u);
    // The discarded timestamp remains reproducible through the unchanged
    // decoder. Exact sample leases and current pixels were never substituted.
    DecodeRequest request; request.targetUs = fixture.pts_of(1); request.mode = DecodeMode::Still;
    source->request(request);
    const auto refillDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(1);
    while (!source->cache().contains(request.targetUs, 1) && std::chrono::steady_clock::now() < refillDeadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    FrameRef refilled = source->frame_for(request.targetUs, &exact);
    AUREA_CHECK(exact && refilled); if (refilled) AUREA_CHECK_EQ(refilled->planes[0][0], aurea::test::frame_gray_code(1));
    mock->queryMemoryStats = {}; external.reset(); refilled.reset(); engine.shutdown();
}

AUREA_TEST(CaptureResources, PreviewGpuPressurePrunesOnlyIdlePrefetchAndUsesTheNormalNextRender) {
    aurea::test::SyntheticConfig video;
    video.width = 1920; video.height = 1080; video.pattern = aurea::test::SyntheticPattern::FrameGray;
    aurea::test::SyntheticFactory factory(video);
    auto* mock = new aurea::test::MockBackend();
    Engine engine; auto cfg = headless_config(); cfg.backend = mock; cfg.mediaFactory = &factory;
    cfg.initialPreviewScale = PreviewScale::Full;
    AUREA_CHECK(engine.initialize(cfg).ok());
    AUREA_CHECK(engine.new_project(1920, 1080, 30., "preview pressure keeps Full").ok());
    VideoImport imported; imported.sourcePath = "preview-pressure-owned-fixture";
    const auto id = engine.import_video(imported); AUREA_CHECK(id.ok()); if (!id.ok()) return;
    const auto* comp = engine.project()->timeline().composition(engine.project()->timeline().current());
    const auto layerId = LayerId::unpack(*id);
    const auto* layer = comp->layer(layerId); const auto* asset = engine.project()->asset(layer->source);
    VideoSource* source = nullptr;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (!source && std::chrono::steady_clock::now() < deadline) {
        source = engine.media().source_for(layerId, layer->source, *asset, 1, true);
        if (!source) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    AUREA_CHECK(source != nullptr); if (!source) return;
    // The generic headless policy may reserve less than three FullHD frames.
    // This fixture needs that real optional working set before GPU pressure.
    engine.memory().set_budget(MemoryClass::DecodedFrames, 12ull << 20);
    source->cache().configure({3, 12ull << 20});
    const DecodeRequest request{0, DecodeMode::Playback, 1, 1.f}; source->request(request);
    AUREA_CHECK(source->wait_for(66'667, 2000));
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    AUREA_CHECK_EQ(source->cache().stats().frames, 3u);
    const u64 before = engine.memory().used(MemoryClass::DecodedFrames);
    AUREA_CHECK_EQ(before, 3 * 1920ull * 1080 * 3 / 2);
    bool exact = false; FrameRef leased = source->frame_for(0, &exact);
    AUREA_CHECK(exact && leased); const u64 content = leased ? leased->content_id() : 0;
    // Fixed external GPU occupancy plus the actual decoded cache and pool
    // admissions: the full source target does not fit until unused CPU backing
    // is destroyed. This does not inject a pretend render success/failure.
    mock->queryMemoryStats = [] { GpuMemoryStats stats; stats.usedBytes = stats.reservedBytes = 24ull << 20; return stats; };
    engine.renderer().set_tracked_resource_budget(44ull << 20,
        [](void* context) noexcept { return static_cast<MemoryManager*>(context)->used(MemoryClass::DecodedFrames); },
        &engine.memory());
    int window = 0; AUREA_CHECK(engine.attach_surface(&window, 1920, 1080).ok());
    bool admissionFailed = false; Status rendered{Errc::OutOfDeviceMemory};
    const auto retryDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    do {
        rendered = engine.render_frame();
        admissionFailed |= rendered.code() == Errc::OutOfDeviceMemory;
        if (rendered.ok() && source->cache().stats().frames == 1) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    } while (std::chrono::steady_clock::now() < retryDeadline);
    const u32 fullHdTargets = static_cast<u32>(std::count_if(mock->textures.begin(), mock->textures.end(), [](const auto& desc) {
        return desc.width == 1920 && desc.height == 1080 && desc.format == SurfaceFormat::RGBA16F;
    }));
    std::printf("\n    preview pressure: before=%llu after=%llu cache_frames=%u status=%u admitted_fullhd=%u\n",
        static_cast<unsigned long long>(before),
        static_cast<unsigned long long>(engine.memory().used(MemoryClass::DecodedFrames)),
        source->cache().stats().frames, rendered.raw(), fullHdTargets);
    AUREA_CHECK(admissionFailed); AUREA_CHECK(rendered.ok());
    AUREA_CHECK(fullHdTargets > 0);
    AUREA_CHECK_EQ(source->cache().stats().frames, 1u);
    AUREA_CHECK_EQ(engine.memory().used(MemoryClass::DecodedFrames), before / 3);
    AUREA_CHECK(leased && leased->content_id() == content);
    if (leased) { AUREA_CHECK_EQ(leased->width, 1920u); AUREA_CHECK_EQ(leased->height, 1080u); }
    AUREA_CHECK_EQ(engine.read_status().previewDenominator, 1u);
    const auto requests = source->stats().requests; const auto decoded = factory.last.load()->decoded.load();
    AUREA_CHECK(engine.render_frame().ok());
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    AUREA_CHECK_EQ(source->stats().requests, requests); AUREA_CHECK_EQ(factory.last.load()->decoded.load(), decoded);
    AUREA_CHECK_EQ(source->cache().stats().frames, 1u);
    engine.renderer().set_tracked_resource_budget(0); mock->queryMemoryStats = {}; leased.reset(); engine.shutdown();
}

AUREA_TEST(CaptureResources, FailedGraphUsesABoundedFenceAndPreservesItsOriginalError) {
    auto* mock = new aurea::test::MockBackend();
    Engine engine; auto cfg = headless_config(); cfg.backend = mock;
    AUREA_CHECK(engine.initialize(cfg).ok());
    AUREA_CHECK(engine.new_project(1920, 1080, 30., "bounded failed capture").ok());
    const auto shape = engine.add_shape(0); AUREA_CHECK(shape.ok()); if (!shape) return;
    auto* comp = engine.project()->timeline().composition(engine.project()->timeline().current());
    comp->layer(LayerId::unpack(*shape))->shape.bounds = Rect{0, 0, 1024, 1024};
    TextureDesc desc; desc.width = 320; desc.height = 180; desc.renderTarget = desc.sampled = true;
    desc.format = SurfaceFormat::RGBA16F;
    const auto target = mock->create_texture(desc); AUREA_CHECK(target.ok()); if (!target) return;
    engine.renderer().set_transient_allocation_limit(4ull << 20);
    const u32 idleBefore = mock->idleWaits.load();
    u32 fenceCalls = 0;
    mock->beforeWaitFrame = [&](u64, u64 timeout) {
        ++fenceCalls; AUREA_CHECK(timeout <= 100'000'000);
        std::this_thread::sleep_for(std::chrono::nanoseconds(timeout));
        return Status{Errc::Timeout};
    };
    const auto begin = std::chrono::steady_clock::now();
    const Status failed = engine.render_offscreen(*target, 320, 180);
    const auto elapsed = std::chrono::steady_clock::now() - begin;
    AUREA_CHECK_EQ(failed.code(), Errc::OutOfDeviceMemory); // A fence timeout does not replace the real failure.
    AUREA_CHECK(elapsed >= std::chrono::milliseconds(90) && elapsed < std::chrono::seconds(1));
    AUREA_CHECK_EQ(fenceCalls, 1u); AUREA_CHECK_EQ(mock->idleWaits.load(), idleBefore);
    const u32 submitted = mock->framesSubmitted, created = mock->texturesCreated;
    AUREA_CHECK_EQ(engine.render_offscreen(*target, 320, 180).code(), Errc::Timeout);
    AUREA_CHECK_EQ(mock->framesSubmitted, submitted); AUREA_CHECK_EQ(mock->texturesCreated, created);
    AUREA_CHECK_EQ(mock->idleWaits.load(), idleBefore);
    mock->beforeWaitFrame = {};
    engine.renderer().set_transient_allocation_limit(~u64{0});
    AUREA_CHECK(engine.render_offscreen(*target, 320, 180).ok());
    AUREA_CHECK(mock->idleWaits.load() > idleBefore); // Successful capture retains its established readback wait.
    AUREA_CHECK(mock->textureAlive[target->id - 1]);
    mock->destroy_texture(*target); engine.shutdown();
}
