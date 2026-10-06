#if defined(AUREA_TEST_VULKAN)
namespace {
// The level stores current luminance in R and next luminance in G. Test the
// actual LK shader at its single-mip boundary, before warping/compositing can
// hide non-finite or incorrect vectors in untextured areas.
FloatImage sample_optical_flow_level(u32 mode) {
    auto& g = gpu();
    constexpr u32 width = 80, height = 48;
    TextureDesc desc;
    desc.width = width; desc.height = height;
    desc.format = SurfaceFormat::RGBA16F;
    desc.sampled = desc.transferSrc = desc.transferDst = true;
    const auto input = g.backend.create_texture(desc);
    AUREA_CHECK(input.ok()); if (!input.ok()) return {};
    desc.renderTarget = true;
    const auto output = g.backend.create_texture(desc);
    AUREA_CHECK(output.ok());
    if (!output.ok()) { g.backend.destroy_texture(*input); return {}; }
    std::vector<u16> pixels(width * height * 4);
    auto signal = [](f32 x, f32 y) { return .5f + .15f * std::sin(.35f * x) + .15f * std::cos(.35f * y); };
    for (u32 y = 0; y < height; ++y) for (u32 x = 0; x < width; ++x) {
        const bool flat = mode == 0 || (mode == 2 && x < 28);
        const bool moving = mode == 2 && !flat && y >= 24;
        const f32 current = flat ? .5f : signal(static_cast<f32>(x), static_cast<f32>(y));
        const f32 next = flat ? .5f : signal(static_cast<f32>(x) - (moving ? .5f : 0.f),
                                             static_cast<f32>(y) + (moving ? .25f : 0.f));
        const usize offset = (static_cast<usize>(y) * width + x) * 4;
        pixels[offset] = scene3d::float_to_half(current);
        pixels[offset + 1] = scene3d::float_to_half(next);
        pixels[offset + 2] = 0;
        pixels[offset + 3] = scene3d::float_to_half(1.f);
    }
    AUREA_CHECK(g.backend.upload_texture(*input, pixels.data(), width * 8).ok());
    FrameBegin frame;
    const bool began = g.backend.begin_offscreen_frame(frame).ok();
    AUREA_CHECK(began);
    if (began) {
        FrameGraph graph;
        TransientTexturePool pool;
        Arena arena;
        const auto source = graph.import_texture("LK luminance pair", *input, desc);
        const auto target = graph.import_texture("LK vectors", *output, desc);
        EffectBuildContext context(graph, g.renderer.shaders(), arena, g.renderer, desc.format, 4096);
        const struct { Vec4 texel; Vec4 flags; } params{{1.f / width, 1.f / height, width, height}, {0, 6, 0, 0}};
        AUREA_CHECK(context.fullscreen_pass("LK sampling regression", PassStage::Decode, target,
                        ShaderId::video_flow_lk_frag, {PassTexture{source}, PassTexture{source}},
                        &params, sizeof(params)) != kInvalidIndex);
        graph.set_output(target, ResourceState::ShaderRead);
        pool.begin_frame(g.backend, 1);
        const bool compiled = graph.compile(pool).ok();
        AUREA_CHECK(compiled);
        if (compiled) graph.execute(*frame.commands, false);
        graph.release(pool);
        pool.end_frame();
        AUREA_CHECK(g.backend.end_frame().ok());
        pool.clear();
    }
    FloatImage result;
    if (began && g.backend.read_texture(*output, pixels.data(), width * 8).ok()) {
        result.width = width; result.height = height;
        result.px.resize(pixels.size());
        for (usize i = 0; i < pixels.size(); ++i) result.px[i] = half_to_float(pixels[i]);
    }
    g.backend.destroy_texture(*input);
    g.backend.destroy_texture(*output);
    return result;
}
}

AUREA_TEST(OpticalFlowSamplingGpu, FlatAndStationaryLevelsKeepZeroMotion) {
    AUREA_REQUIRE_GPU();
#if !defined(AUREA_TEST_GLES)
    const u32 errors = vk::Backend::validation_errors();
#endif
    for (u32 mode = 0; mode < 2; ++mode) {
        const auto image = sample_optical_flow_level(mode);
        AUREA_CHECK_EQ(image.width, 80u); if (image.width != 80) continue;
        f32 maxFlow = 0, maxConfidence = 0;
        bool finite = true;
        for (usize i = 0; i < image.px.size(); i += 4) {
            for (u32 c = 0; c < 4; ++c) finite &= std::isfinite(image.px[i + c]);
            maxFlow = std::max(maxFlow, std::max(std::fabs(image.px[i]), std::fabs(image.px[i + 1])));
            maxConfidence = std::max(maxConfidence, image.px[i + 2]);
        }
        std::printf("    LK %s: max motion %.6f, confidence %.6f\n", mode ? "stationary texture" : "flat", maxFlow, maxConfidence);
        AUREA_CHECK(finite);
        AUREA_CHECK(maxFlow < .001f);
        if (mode == 0) AUREA_CHECK_NEAR(maxConfidence, 0.f, .00001f);
        else AUREA_CHECK(maxConfidence > .001f);
    }
#if !defined(AUREA_TEST_GLES)
    AUREA_CHECK_EQ(vk::Backend::validation_errors() - errors, 0u);
#endif
}

AUREA_TEST(OpticalFlowSamplingGpu, DivergentFlatStationaryAndMovingRegionsRecoverSubpixelMotion) {
    AUREA_REQUIRE_GPU();
#if !defined(AUREA_TEST_GLES)
    const u32 errors = vk::Backend::validation_errors();
#endif
    const auto image = sample_optical_flow_level(2);
    AUREA_CHECK_EQ(image.width, 80u); if (image.width != 80) return;
    bool finite = true;
    for (f32 value : image.px) finite &= std::isfinite(value);
    AUREA_CHECK(finite);
    f32 stillError = 0, movingError = 0;
    Vec2 mean{};
    u32 count = 0;
    for (u32 y = 5; y < 43; ++y) for (u32 x = 5; x < 73; ++x) {
        const auto v = image.v(x, y);
        if (x < 22 || (x >= 36 && y < 18))
            stillError = std::max(stillError, std::max(std::fabs(v.x), std::fabs(v.y)));
        if (x >= 36 && y >= 31) {
            movingError = std::max(movingError, std::max(std::fabs(v.x - .5f), std::fabs(v.y + .25f)));
            mean.x += v.x; mean.y += v.y; ++count;
        }
    }
    AUREA_CHECK(count > 0);
    mean.x /= count; mean.y /= count;
    std::printf("    LK divergent: still error %.6f; translated mean %.5f, %.5f, max error %.5f\n",
                stillError, mean.x, mean.y, movingError);
    AUREA_CHECK(stillError < .001f);
    AUREA_CHECK_NEAR(mean.x, .5f, .04f);
    AUREA_CHECK_NEAR(mean.y, -.25f, .04f);
    AUREA_CHECK(movingError < .12f);
#if !defined(AUREA_TEST_GLES)
    AUREA_CHECK_EQ(vk::Backend::validation_errors() - errors, 0u);
#endif
}
#endif
