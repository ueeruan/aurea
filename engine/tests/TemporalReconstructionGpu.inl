namespace temporal_preview_test {

struct Fixture {
    TemporalReconstruction temporal;
    TransientTexturePool pool;
    TextureDesc colorDesc{}, depthDesc{};
    TextureHandle color{}, depth{};
    u32 width = 64, height = 32;

    Fixture() {
        colorDesc.width = width; colorDesc.height = height;
        colorDesc.format = SurfaceFormat::RGBA16F;
        colorDesc.sampled = colorDesc.transferDst = true;
        depthDesc = colorDesc; depthDesc.format = SurfaceFormat::R32F;
        const auto c = gpu().backend.create_texture(colorDesc);
        const auto d = gpu().backend.create_texture(depthDesc);
        AUREA_CHECK(c.ok() && d.ok());
        if (c) color = *c;
        if (d) depth = *d;
        AUREA_CHECK(temporal.admit(gpu().backend, gpu().renderer.shaders(), width, height, 1ull << 20));
    }
    ~Fixture() {
        gpu().backend.wait_idle();
        temporal.clear(&gpu().backend);
        pool.clear();
        if (color.valid()) gpu().backend.destroy_texture(color);
        if (depth.valid()) gpu().backend.destroy_texture(depth);
    }
    std::vector<f32> render(TemporalReconstruction::Frame frame, f32 offset,
                           f32 shift = 0.f, bool disocclusion = false, bool submitHistory = true) {
        auto& g = gpu();
        std::vector<u16> colors(static_cast<usize>(width) * height * 4);
        std::vector<f32> depths(static_cast<usize>(width) * height, .5f);
        for (u32 y = 0; y < height; ++y) for (u32 x = 0; x < width; ++x) {
            const usize pixel = static_cast<usize>(y) * width + x;
            colors[pixel * 4] = scene3d::float_to_half(.1f + .005f * (x - shift) + .003f * y + offset);
            colors[pixel * 4 + 1] = scene3d::float_to_half(.15f);
            colors[pixel * 4 + 2] = scene3d::float_to_half(.2f);
            colors[pixel * 4 + 3] = scene3d::float_to_half(1.f);
            if (disocclusion && x >= 24 && x < 40) depths[pixel] = .8f;
        }
        FrameBegin begin;
        AUREA_CHECK(g.backend.begin_frame(begin).ok());
        AUREA_CHECK(g.backend.upload_texture(color, colors.data(), width * 8).ok());
        AUREA_CHECK(g.backend.upload_texture(depth, depths.data(), width * 4).ok());
        FrameGraph graph;
        pool.begin_frame(g.backend, begin.frameNumber);
        const auto input = graph.import_texture("temporal test current", color, colorDesc);
        const auto z = graph.import_texture("temporal test depth", depth, depthDesc);
        const auto output = temporal.build(graph, g.renderer.shaders(), input, z, frame);
        AUREA_CHECK(output != input);
        const auto compiled = graph.compile(pool);
        AUREA_CHECK(compiled.ok());
        const auto texture = graph.physical(output);
        if (compiled.ok()) graph.execute(*begin.commands, true);
        graph.release(pool); pool.end_frame();
        const auto submitted = g.backend.end_frame();
        AUREA_CHECK(submitted.ok());
        temporal.complete(compiled.ok() && submitted.ok() && submitHistory);
        g.backend.wait_idle();
        std::vector<u16> half(colors.size());
        AUREA_CHECK(g.backend.read_texture(texture, half.data(), width * 8).ok());
        std::vector<f32> result(half.size());
        for (usize i = 0; i < half.size(); ++i) result[i] = half_to_float(half[i]);
        return result;
    }
};

} // namespace temporal_preview_test

AUREA_TEST(HyperAdaptive, TemporalCameraVectorsRejectDisocclusionAndResetOnSeek) {
    AUREA_REQUIRE_GPU();
    temporal_preview_test::Fixture fixture;
    TemporalReconstruction::Frame frame;
    frame.composition = 3; frame.revision = 4; frame.quality = 5;
    (void)fixture.render(frame, .004f);
    frame.time = 1;
    AUREA_CHECK(fixture.temporal.can_reuse(frame));
    constexpr f32 shift = 4.f;
    frame.viewProjection = Mat4::translation(Vec3{2.f * shift / fixture.width, 0, 0});
    frame.inverseJitteredViewProjection = Mat4::translation(Vec3{-2.f * shift / fixture.width, 0, 0});
    const auto image = fixture.render(frame, 0.f, shift, true);
    for (u32 x : {16u, 32u, 48u}) {
        const usize pixel = (16u * fixture.width + x) * 4u;
        const f32 current = half_to_float(scene3d::float_to_half(.1f + .005f * (x - shift) + .003f * 16));
        const f32 expected = current + (x == 32 ? 0.f : .004f * .85f);
        AUREA_CHECK_NEAR(image[pixel], expected, .0008f);
        AUREA_CHECK_EQ(image[pixel + 3], 1.f);
    }
    for (f32 value : image) AUREA_CHECK(std::isfinite(value));
    frame.time = 100;
    AUREA_CHECK(!fixture.temporal.can_reuse(frame));
    const auto reset = fixture.render(frame, 0.f, shift);
    const usize center = (16u * fixture.width + 16u) * 4u;
    AUREA_CHECK_NEAR(reset[center], .1f + .005f * 12 + .003f * 16, .0005f);
    ++frame.time; ++frame.revision;
    AUREA_CHECK(!fixture.temporal.can_reuse(frame));
    (void)fixture.render(frame, 0.f, shift, false, false);
    ++frame.time;
    AUREA_CHECK(!fixture.temporal.can_reuse(frame));
}

AUREA_TEST(HyperAdaptive, RendererUsesTemporalOnlyForSupportedReducedPreviewScenes) {
    AUREA_REQUIRE_GPU();
    using namespace velocity_blur;
    Rig rig(320, 180);
    rig.scene.comp->motion_blur().enabled = false;
    auto* model = rig.model(cube_asset(Vec4{.7f, .35f, .2f, 1}, false), Vec3{160, 90, 0}, 36, "static temporal cube");
    auto& g = gpu();
    RenderSettings settings;
    settings.previewDenominator = 2;
    settings.previewPlaying = true;
    settings.previewCacheRevision = 17;
    settings.previewCacheComposition = rig.scene.comp->id().pack();
    settings.dither = false;
    const auto draw = [&](i64 time, bool expectTemporal) {
        FrameSnapshot snapshot;
        g.renderer.prepare(*rig.scene.comp, rig.scene.project, FrameIndex{time}, nullptr, nullptr, nullptr,
            settings, static_cast<u64>(time + 10), 1, DecodeMode::Playback, 1.f, snapshot);
        FrameStats stats; RenderTimings timings;
        const u32 width = settings.finalQuality ? 320 : 160, height = settings.finalQuality ? 180 : 90;
        OffscreenTarget output{g.target(width, height), width, height};
        AUREA_CHECK(g.renderer.render(snapshot, settings, &output, stats, timings).ok());
        bool found = false;
        const auto& graph = g.renderer.frame_graph();
        for (u32 pass : graph.order())
            found |= std::string_view(graph.pass_name(pass)) == "preview-temporal-reconstruct";
        AUREA_CHECK_EQ(found, expectTemporal);
        g.backend.wait_idle();
        std::vector<u16> pixels(static_cast<usize>(width) * height * 4);
        AUREA_CHECK(g.backend.read_texture(output.texture, pixels.data(), width * 8).ok());
        for (u16 value : pixels) AUREA_CHECK(std::isfinite(half_to_float(value)));
        return pixels;
    };
    const bool capable = g.backend.capabilities().depthResolveSampleZero || g.backend.capabilities().msaa_samples(4) == 1;
    for (i64 frame = 0; frame < 8; ++frame) (void)draw(frame, capable);
    model->tracks.get_or_create(TrackProperty::PositionX).set(FrameIndex{0}, 100);
    model->tracks.get_or_create(TrackProperty::PositionX).set(FrameIndex{30}, 220);
    (void)draw(8, false);
    settings.finalQuality = true;
    settings.previewDenominator = 1;
    const auto reference = draw(9, false);
    settings.heavyScale = .25f; settings.heavyExplicit = true;
    settings.heavy = HeavyQuality::from_scale(.25f, 8);
    const auto stressed = draw(9, false);
    AUREA_CHECK(reference == stressed);
}
