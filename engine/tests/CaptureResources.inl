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
