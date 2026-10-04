#if defined(AUREA_TEST_VULKAN)
AUREA_TEST(PreviewBuffer, ComposedFramesReusePixelsAndInvalidateOnEdit) {
    AUREA_REQUIRE_GPU();
    Scene scene(64, 64);
    auto& g = gpu();
    auto& r = g.renderer;
    r.set_preview_cache_budget(64ull * 64 * 8 * 2);
    RenderSettings settings;
    settings.previewCacheRevision = 1;
    settings.previewCacheComposition = 1;
    settings.previewCacheOnly = true;
    settings.dither = false;
    scene.comp->set_background(Color{1, 0, 0, 1});
    AUREA_CHECK_EQ(r.configure_preview_cache(64, 64, settings), 2u);
    FrameSnapshot snap;
    r.prepare(*scene.comp, scene.project, FrameIndex{0}, nullptr, nullptr, nullptr,
        settings, 1, 0, DecodeMode::Still, 1.f, snap);
    FrameStats stats; RenderTimings timings;
    AUREA_CHECK(r.render(snap, settings, nullptr, stats, timings).ok());
    AUREA_CHECK(r.preview_cached(FrameIndex{0}));
    AUREA_CHECK_EQ(r.preview_cached_count(), 1u);
    i64 ranges[60]{};
    AUREA_CHECK_EQ(r.copy_preview_buffer_ranges(1, 1, ranges, 30), 1u);
    AUREA_CHECK_EQ(ranges[0], 0ll); AUREA_CHECK_EQ(ranges[1], 1ll);
    g.backend.wait_idle();

    // The engine discards the heavy snapshot on a cache hit. Verify that the
    // ordinary display pass still receives the previously composed red image.
    snap = FrameSnapshot{};
    snap.compWidth = snap.compHeight = 64; snap.time = FrameIndex{0};
    OffscreenTarget display;
    display.texture = g.target(64, 64); display.width = display.height = 64;
    TextureDesc desc; desc.width = desc.height = 64; desc.format = SurfaceFormat::RGBA16F;
    desc.renderTarget = true; desc.transferSrc = true; desc.sampled = true;
    auto out = g.backend.create_texture(desc);
    AUREA_CHECK(out.ok()); if (!out.ok()) return;
    display.display = *out; display.displayWidth = display.displayHeight = 64;
    settings.previewCacheOnly = false;
    AUREA_CHECK(r.render(snap, settings, &display, stats, timings).ok());
    AUREA_CHECK(r.last_preview_cache_hit());
    AUREA_CHECK_EQ(stats.passesExecuted, 1u);
    g.backend.wait_idle();
    std::vector<u16> pixels(64 * 64 * 4);
    AUREA_CHECK(g.backend.read_texture(*out, pixels.data(), 64 * 8).ok());
    AUREA_CHECK(half_to_float(pixels[(32 * 64 + 32) * 4]) > .95f);
    AUREA_CHECK(half_to_float(pixels[(32 * 64 + 32) * 4 + 1]) < .01f);

    // A changed project can never reuse an older image at the same playhead.
    settings.previewCacheRevision++;
    AUREA_CHECK_EQ(r.copy_preview_buffer_ranges(settings.previewCacheRevision, 1, ranges, 30), 0u);
    AUREA_CHECK_EQ(r.configure_preview_cache(64, 64, settings), 2u);
    AUREA_CHECK(!r.preview_cached(FrameIndex{0}));
    scene.comp->set_background(Color{0, 1, 0, 1});
    r.prepare(*scene.comp, scene.project, FrameIndex{0}, nullptr, nullptr, nullptr,
        settings, 2, 0, DecodeMode::Still, 1.f, snap);
    AUREA_CHECK(r.render(snap, settings, &display, stats, timings).ok());
    AUREA_CHECK(!r.last_preview_cache_hit());
    g.backend.wait_idle();
    AUREA_CHECK(g.backend.read_texture(*out, pixels.data(), 64 * 8).ok());
    AUREA_CHECK(half_to_float(pixels[(32 * 64 + 32) * 4 + 1]) > .95f);
    AUREA_CHECK(half_to_float(pixels[(32 * 64 + 32) * 4]) < .01f);
    (void)r.trim_memory(4, 3);
    AUREA_CHECK_EQ(r.preview_cached_count(), 0u);
    AUREA_CHECK_EQ(r.copy_preview_buffer_ranges(settings.previewCacheRevision, 1, ranges, 30), 0u);
    g.backend.destroy_texture(*out);
    r.set_preview_cache_budget(0);
}

AUREA_TEST(PreviewBuffer, CacheRejectsMissingMediaAndFinalExportBypassesIt) {
    AUREA_REQUIRE_GPU();
    Scene scene(32, 32);
    auto& r = gpu().renderer;
    r.set_preview_cache_budget(32ull * 32 * 8 * 2);
    RenderSettings settings;
    settings.previewCacheRevision = 1; settings.previewCacheOnly = true;
    AUREA_CHECK_EQ(r.configure_preview_cache(32, 32, settings), 2u);
    FrameSnapshot snap; FrameStats stats; RenderTimings timings;
    r.prepare(*scene.comp, scene.project, FrameIndex{0}, nullptr, nullptr, nullptr,
        settings, 1, 0, DecodeMode::Still, 1.f, snap);
    snap.missingVideoFrames = 1;
    AUREA_CHECK(r.render(snap, settings, nullptr, stats, timings).ok());
    AUREA_CHECK(!r.preview_cached(FrameIndex{0}));
    snap.missingVideoFrames = 0;
    AUREA_CHECK(r.render(snap, settings, nullptr, stats, timings).ok());
    AUREA_CHECK(r.preview_cached(FrameIndex{0}));
    settings.finalQuality = true;
    AUREA_CHECK(r.render(snap, settings, nullptr, stats, timings).ok());
    AUREA_CHECK(!r.last_preview_cache_hit());
    AUREA_CHECK(stats.passesExecuted > 0);
    r.set_preview_cache_budget(0);
}
#endif
