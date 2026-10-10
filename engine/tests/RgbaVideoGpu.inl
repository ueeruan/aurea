// The one-plane decoder path and failed native import share video color,
// orientation and visible-region math. A green coded border must never flash
// into the visible image, including preview downsampling.
AUREA_TEST(VideoPreviewGpu, CpuRgbaAndNativeFallbackPreserveColorCropAndEveryRotation) {
    AUREA_REQUIRE_GPU();
    constexpr u32 quadrants[4][4] = {{0,1,2,3}, {2,0,3,1}, {3,2,1,0}, {1,3,0,2}};
#if defined(__ANDROID__)
    // The host fixture's native handle is deliberately unimportable. Android
    // dereferences AHardwareBuffer handles, so never pass its fake pointer to
    // the real driver. Production native buffer color is tested by the GL
    // video bridge probe; this device test exercises CPU RGBA conversion.
    std::printf(" (Android: CPU RGBA cases; fake native handles excluded) ");
    constexpr bool nativeCases[] = {false};
#else
    constexpr bool nativeCases[] = {false, true};
#endif
    for (bool native : nativeCases) for (u32 rotation : {0u,90u,180u,270u})
    for (auto transfer : {TransferFunction::SRGB, TransferFunction::Linear}) {
        auto* raw = new RgbaVideoFrame(native);
        raw->rotation = rotation;
        raw->color.matrix = YCbCrMatrix::BT601; // already-RGB data must not reapply range/matrix
        raw->color.fullRange = false;
        raw->color.transfer = transfer;
        FrameRef frame = FrameRef::adopt(raw);
        const u32 width = rotation % 180 ? raw->visibleHeight : raw->visibleWidth;
        const u32 height = rotation % 180 ? raw->visibleWidth : raw->visibleHeight;
        Scene scene(width, height);
        scene.solid(static_cast<f32>(width), static_cast<f32>(height), Vec4{1,1,1,1}, width * .5f, height * .5f);
        for (u32 denominator : {1u, 2u}) {
            RenderSettings settings; settings.previewDenominator = denominator;
            settings.finalQuality = true; settings.dither = false;
            FrameSnapshot snapshot;
            gpu().renderer.prepare(*scene.comp, scene.project, FrameIndex{0}, nullptr, nullptr, nullptr,
                settings, 1, 0, DecodeMode::Still, 1.f, snapshot);
            AUREA_CHECK_EQ(snapshot.layers.size(), 1u); if (snapshot.layers.empty()) return;
            auto& layer = snapshot.layers[0];
            layer.compFromLayer = Mat4::identity();
            layer.source.kind = LayerSource::Kind::Video;
            layer.source.width = width; layer.source.height = height;
            layer.source.frame = frame; layer.source.frameExact = true;
            const u32 targetW = width / denominator, targetH = height / denominator;
            OffscreenTarget target{gpu().target(targetW, targetH), targetW, targetH};
            FrameStats stats; RenderTimings timings;
            AUREA_CHECK(gpu().renderer.render(snapshot, settings, &target, stats, timings).ok());
            AUREA_CHECK(!gpu().renderer.take_incomplete()); AUREA_CHECK_EQ(stats.layersRendered, 1u);
            gpu().backend.wait_idle();
            std::vector<u16> pixels(static_cast<usize>(targetW) * targetH * 4);
            AUREA_CHECK(gpu().backend.read_texture(target.texture, pixels.data(), targetW * 8).ok());
            for (u32 y = 0; y < targetH; ++y) for (u32 x = 0; x < targetW; ++x) {
                // The color transition may mix neighboring quadrants, while
                // every coded/visible border remains a constant exact color.
                if (std::abs(static_cast<i32>(x) - static_cast<i32>(targetW / 2)) <= 1
                    || std::abs(static_cast<i32>(y) - static_cast<i32>(targetH / 2)) <= 1) continue;
                const u32 quadrant = (x >= targetW / 2 ? 1u : 0u) + (y >= targetH / 2 ? 2u : 0u);
                const auto color = RgbaVideoFrame::colors[quadrants[rotation / 90][quadrant]];
                const usize offset = (static_cast<usize>(y) * targetW + x) * 4;
                for (u32 c = 0; c < 3; ++c) {
                    const f32 encoded = color[c] / 255.f;
                    const f32 expected = transfer == TransferFunction::Linear ? encoded : srgb_decode(encoded);
                    AUREA_CHECK_NEAR(half_to_float(pixels[offset + c]), expected, .002f);
                }
                AUREA_CHECK_NEAR(half_to_float(pixels[offset + 3]), 1.f, .001f);
            }
        }
        AUREA_CHECK(raw->prepareCalls >= 1);
    }
}
