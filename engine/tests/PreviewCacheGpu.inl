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
AUREA_TEST(PreviewFidelity, NestedProjectedCardsKeepContentAcrossResolutionAndCache) {
    AUREA_REQUIRE_GPU();
    Scene scene(320, 240);
    auto& timeline = scene.project.timeline();
    const auto cardId = timeline.create_composition("two colors", 128, 128, 30);
    auto* card = timeline.composition(cardId);
    card->set_background(Color{0, 0, 0, 0});
    card->set_transparent_background(true);
    for (u32 color = 0; color < 2; ++color) {
        auto* layer = card->layer(card->add_layer(LayerKind::Shape, "card"));
        layer->shape.bounds = Rect{0, 0, 44, 88};
        layer->shape.fillColor = color ? Vec4{0, 1, 0, 1} : Vec4{1, 0, 0, 1};
        layer->transform.anchor = Vec3{22, 44, 0};
        layer->transform.position = Vec3{color ? 92.f : 36.f, 64, 0};
    }
    const auto groupId = timeline.create_composition("nested cards", 128, 128, 30);
    auto* group = timeline.composition(groupId);
    group->set_background(Color{0, 0, 0, 0});
    group->set_transparent_background(true);
    auto* inner = group->layer(group->add_layer(LayerKind::Composition, "inner"));
    inner->nested.composition = cardId;
    inner->transform.anchor = inner->transform.position = Vec3{64, 64, 0};
    inner->masks.push_back(rect_mask(1, -128, -128, 256, 256));
    scene.comp = timeline.composition(timeline.root());
    for (u32 side = 0; side < 2; ++side) {
        auto* layer = scene.comp->layer(scene.comp->add_layer(LayerKind::Composition, "projected group"));
        layer->nested.composition = groupId;
        layer->threeD = true;
        layer->transform.anchor = Vec3{64, 64, 0};
        layer->transform.position = Vec3{side ? 232.f : 88.f, 120, 0};
        layer->transform.rotation = Vec3{12, side ? -35.f : 35.f, side ? -8.f : 8.f};
        layer->transform.scale = Vec3{.8f, .8f, 1};
        layer->masks.push_back(rect_mask(2, -128, -128, 256, 256));
    }
    auto& g = gpu();
    auto& r = g.renderer;
    TextureDesc desc;
    desc.width = 320; desc.height = 240; desc.format = SurfaceFormat::RGBA16F;
    desc.renderTarget = desc.sampled = desc.transferSrc = true;
    const auto displayTexture = g.backend.create_texture(desc);
    AUREA_CHECK(displayTexture.ok()); if (!displayTexture.ok()) return;
    OffscreenTarget display;
    display.display = *displayTexture; display.displayWidth = 320; display.displayHeight = 240;
    auto read = [&]() {
        g.backend.wait_idle();
        std::vector<u16> pixels(320 * 240 * 4);
        AUREA_CHECK(g.backend.read_texture(*displayTexture, pixels.data(), 320 * 8).ok());
        return pixels;
    };
    std::array<f64, 8> referenceCentroids{};
    for (const u32 denominator : {1u, 2u, 4u}) {
        display.width = 320 / denominator; display.height = 240 / denominator;
        display.texture = g.target(display.width, display.height);
        RenderSettings settings;
        settings.dither = false; settings.previewDenominator = denominator;
        settings.previewCacheRevision = 41; settings.previewCacheComposition = timeline.root().pack();
        r.set_preview_cache_budget(4ull << 20);
        AUREA_CHECK(r.configure_preview_cache(320, 240, settings) > 0);
        FrameSnapshot snapshot;
        r.prepare(*scene.comp, scene.project, FrameIndex{15}, nullptr, nullptr, nullptr,
                  settings, denominator, 0, DecodeMode::Still, 1, snapshot);
        FrameStats stats; RenderTimings timings;
        AUREA_CHECK(r.render(snapshot, settings, &display, stats, timings).ok());
        AUREA_CHECK(!r.last_preview_cache_hit());
        AUREA_CHECK(r.preview_cached(FrameIndex{15}));
        const auto first = read();
        // Replaying a composed frame must retain every child, even when the
        // engine intentionally discards the prepared layer list on a cache hit.
        snapshot = FrameSnapshot{};
        snapshot.compWidth = 320; snapshot.compHeight = 240; snapshot.time = FrameIndex{15};
        AUREA_CHECK(r.render(snapshot, settings, &display, stats, timings).ok());
        AUREA_CHECK(r.last_preview_cache_hit());
        AUREA_CHECK(first == read());
        r.set_preview_cache_budget(0);
        r.prepare(*scene.comp, scene.project, FrameIndex{15}, nullptr, nullptr, nullptr,
                  settings, denominator + 10, 0, DecodeMode::Still, 1, snapshot);
        AUREA_CHECK(r.render(snapshot, settings, &display, stats, timings).ok());
        AUREA_CHECK(!r.last_preview_cache_hit());
        AUREA_CHECK(first == read());
        // Independently require both colored objects in both instances. Pixel
        // equality alone would also pass if all three paths lost the objects.
        for (u32 side = 0; side < 2; ++side) for (u32 color = 0; color < 2; ++color) {
            f64 mass = 0, xMoment = 0, yMoment = 0;
            for (u32 y = 0; y < 240; ++y) for (u32 x = side * 160; x < (side + 1) * 160; ++x) {
                const usize p = (y * 320 + x) * 4;
                const f64 value = std::max(0.f, half_to_float(first[p + color]) - half_to_float(first[p + 1 - color]));
                mass += value; xMoment += value * x; yMoment += value * y;
            }
            AUREA_CHECK(mass > 400);
            const usize index = (side * 2 + color) * 2;
            const f64 cx = xMoment / std::max(1., mass), cy = yMoment / std::max(1., mass);
            if (denominator == 1) { referenceCentroids[index] = cx; referenceCentroids[index + 1] = cy; }
            AUREA_CHECK_NEAR(cx, referenceCentroids[index], 2.5);
            AUREA_CHECK_NEAR(cy, referenceCentroids[index + 1], 2.5);
            std::printf("    preview 1/%u instance %u color %u: mass %.1f center %.2f,%.2f\n", denominator, side, color, mass, cx, cy);
        }
    }
    g.backend.destroy_texture(*displayTexture);
}
#endif
