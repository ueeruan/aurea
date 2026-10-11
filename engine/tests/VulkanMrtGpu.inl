#if defined(AUREA_TEST_VULKAN) && !defined(AUREA_TEST_GLES)
namespace {
// The real DOF shader writes display and HDR radiance separately. At zero
// radius it copies both inputs exactly, so a dropped color attachment cannot
// hide behind a tone-map threshold or previously initialized allocation.
struct MrtWithoutDepthGpu {
    static constexpr u32 width = 48, height = 24;
    Gpu& gpu;
    u32 samples;
    TextureHandle scene, display, coc, poisonScene, poisonDisplay;
    TextureHandle color0, color1, output0, output1;
    PipelineHandle pipeline, partialGeometryPipeline;
    SamplerHandle sampler;
    std::vector<TextureHandle> owned;
    std::vector<u16> scenePixels, displayPixels, scenePoison, displayPoison;
    bool open = false;
    u32 submissions = 0;

    MrtWithoutDepthGpu(Gpu& backend, u32 sampleCount) : gpu(backend), samples(sampleCount) {}
    ~MrtWithoutDepthGpu() {
        if (open) (void)gpu.backend.end_frame();
        gpu.backend.wait_idle();
        for (const auto texture : owned) gpu.backend.destroy_texture(texture);
        gpu.backend.wait_idle();
    }
    static TextureDesc desc(u32 sampleCount = 1) {
        TextureDesc d; d.width = width; d.height = height;
        d.format = SurfaceFormat::RGBA16F; d.sampleCount = sampleCount;
        d.sampled = sampleCount == 1; d.renderTarget = true;
        d.transferSrc = d.transferDst = sampleCount == 1;
        return d;
    }
    TextureHandle make(u32 sampleCount = 1) {
        const auto result = gpu.backend.create_texture(desc(sampleCount));
        AUREA_CHECK(result.ok());
        if (!result.ok()) return {};
        owned.push_back(*result); return *result;
    }
    static std::vector<u16> constant(const std::array<f32, 4>& color) {
        std::vector<u16> pixels(width * height * 4);
        for (usize i = 0; i < pixels.size(); ++i)
            pixels[i] = scene3d::float_to_half(color[i % 4]);
        return pixels;
    }
    bool setup() {
        scenePixels.resize(width * height * 4); displayPixels.resize(scenePixels.size());
        for (u32 y = 0; y < height; ++y) for (u32 x = 0; x < width; ++x) {
            const usize p = (y * width + x) * 4;
            // Binary fractions survive RGBA16F storage and multisample resolve.
            scenePixels[p] = scene3d::float_to_half(2.f + (x % 7) * .125f);
            scenePixels[p + 1] = scene3d::float_to_half(.5f + (y % 5) * .0625f);
            scenePixels[p + 2] = scene3d::float_to_half(.25f + (x % 3 + y % 3) * .03125f);
            scenePixels[p + 3] = scene3d::float_to_half(.75f);
            displayPixels[p] = scene3d::float_to_half(.0625f + (x % 5) * .03125f);
            displayPixels[p + 1] = scene3d::float_to_half(.125f + (y % 7) * .03125f);
            displayPixels[p + 2] = scene3d::float_to_half(.5f + (x % 3 + y % 5) * .03125f);
            displayPixels[p + 3] = scene3d::float_to_half(.5f);
        }
        scenePoison = constant({13.f, 11.f, 9.f, .25f});
        displayPoison = constant({7.f, 5.f, 3.f, .125f});
        scene = make(); display = make(); coc = make();
        poisonScene = make(); poisonDisplay = make(); output0 = make(); output1 = make();
        if (!scene.valid() || !display.valid() || !coc.valid() || !poisonScene.valid()
            || !poisonDisplay.valid() || !output0.valid() || !output1.valid()) return false;
        color0 = samples == 1 ? output0 : make(samples);
        color1 = samples == 1 ? output1 : make(samples);
        if (!color0.valid() || !color1.valid()) return false;
        const auto zero = constant({0, 0, 0, 0});
        bool ok = gpu.backend.upload_texture(scene, scenePixels.data(), width * 8).ok();
        ok &= gpu.backend.upload_texture(display, displayPixels.data(), width * 8).ok();
        ok &= gpu.backend.upload_texture(coc, zero.data(), width * 8).ok();
        ok &= gpu.backend.upload_texture(poisonScene, scenePoison.data(), width * 8).ok();
        ok &= gpu.backend.upload_texture(poisonDisplay, displayPoison.data(), width * 8).ok();
        ok &= gpu.backend.upload_texture(output0, displayPoison.data(), width * 8).ok();
        ok &= gpu.backend.upload_texture(output1, scenePoison.data(), width * 8).ok();
        AUREA_CHECK(ok); if (!ok) return false;
        auto key = PipelineKey::fullscreen(ShaderId::scene3d_post_dof_frag, SurfaceFormat::RGBA16F);
        key.hasColor1 = true; key.hasDepth = false; key.sampleCount = static_cast<u8>(samples);
        const auto created = gpu.renderer.shaders().pipeline(key);
        AUREA_CHECK(created.ok()); if (!created.ok()) return false;
        pipeline = *created;
        if (samples > 1) {
            // Mesa 25.2.8's left MSAA scissor plane admits two samples in
            // pixel offset-1 (lp_setup.c:1559 / lp_rast_tri_tmp.h:68). The
            // unchanged failing scissor control is archived in diagnostic
            // 4eb8ac0416ee17d60873f479a82fa3768283b3a6. Test Load/Clear with
            // an actual clipped quad instead; 1x still tests the scissor.
            key.vertex = ShaderId::composite_layer_vert;
            const auto partial = gpu.renderer.shaders().pipeline(key);
            AUREA_CHECK(partial.ok()); if (!partial.ok()) return false;
            partialGeometryPipeline = *partial;
        }
        sampler = gpu.renderer.shaders().sampler(CommonSampler::NearestClamp);
        return true;
    }
    bool draw(bool poison, LoadOp load, bool partial, Vec4 clear = {}, bool resolve = true) {
        FrameGraph graph; TransientTexturePool pool;
        const auto s = graph.import_texture("DOF-HDR-input", poison ? poisonScene : scene, desc());
        const auto d = graph.import_texture("DOF-display-input", poison ? poisonDisplay : display, desc());
        const auto c = graph.import_texture("zero-CoC", coc, desc());
        const auto a = graph.import_texture("no-depth-color0", color0, desc(samples));
        const auto b = graph.import_texture("no-depth-color1", color1, desc(samples));
        const auto pass = graph.add_raster_pass("DOF-two-colors-no-depth", PassStage::Effects,
            a, load, clear, [&, partial](PassContext& pc) {
                const Vec4 params{1.f / width, 1.f / height, 0.f, 32.f};
                const bool geometry = partial && samples > 1;
                pc.cmds.bind_pipeline(geometry ? partialGeometryPipeline : pipeline);
                pc.cmds.bind_texture(0, pc.texture(s), sampler);
                pc.cmds.bind_texture(1, pc.texture(d), sampler);
                pc.cmds.bind_texture(2, pc.texture(c), sampler);
                pc.cmds.set_uniforms(&params, sizeof(params));
                if (geometry) {
                    struct QuadPush { Mat4 clipFromLayer; Vec4 region, uvRect, params; } push;
                    push.clipFromLayer.col[0] = Vec4{2.f / width, 0, 0, 0};
                    push.clipFromLayer.col[1] = Vec4{0, 2.f / height, 0, 0};
                    push.clipFromLayer.col[3] = Vec4{-1, -1, 0, 1};
                    push.region = Vec4{width / 2.f, 0, width / 2.f, height};
                    push.uvRect = Vec4{.5f, 0, .5f, 1};
                    push.params = Vec4{1, 0, 0, 0};
                    // uv.x=.5+(x-24+.5)/24*.5=(x+.5)/48. Thus each
                    // covered pixel samples its ORIGINAL input texel; x<24
                    // receives no primitive coverage, and remains Load/clear.
                    pc.cmds.push_constants(&push, sizeof(push));
                    pc.cmds.draw(6);
                } else {
                    if (partial) pc.cmds.set_scissor(width / 2, 0, width / 2, height);
                    pc.cmds.draw(3);
                }
            });
        graph.set_color1(pass, b);
        graph.read(pass, s); graph.read(pass, d); graph.read(pass, c);
        auto result0 = a, result1 = b;
        if (samples > 1 && resolve) {
            result0 = graph.import_texture("display-resolve", output0, desc());
            result1 = graph.import_texture("HDR-resolve", output1, desc());
            graph.set_resolve(pass, result0, result1);
        }
        const auto finalState = samples > 1 && !resolve ? ResourceState::ColorAttachment : ResourceState::ShaderRead;
        graph.set_output(result0, finalState);
        graph.set_output(result1, finalState);
        FrameBegin frame;
        const auto begun = gpu.backend.begin_offscreen_frame(frame);
        AUREA_CHECK(begun.ok()); if (!begun.ok()) return false;
        open = true; AUREA_CHECK(!frame.backbuffer.valid());
        pool.begin_frame(gpu.backend, frame.frameNumber);
        const auto compiled = graph.compile(pool);
        AUREA_CHECK(compiled.ok());
        if (compiled.ok()) graph.execute(*frame.commands, false);
        const auto ended = gpu.backend.end_frame(); open = false;
        AUREA_CHECK(ended.ok()); if (!ended.ok()) { gpu.backend.wait_idle(); graph.release(pool); return false; }
        ++submissions;
        const auto waited = gpu.backend.wait_frame(frame.frameNumber, 5'000'000'000ull);
        AUREA_CHECK(waited.ok());
        if (!waited.ok()) gpu.backend.wait_idle();
        graph.release(pool); pool.clear();
        return compiled.ok() && waited.ok();
    }
    bool pixels(const std::vector<u16>& expectDisplay, const std::vector<u16>& expectScene, const char* stage) {
        std::vector<u16> a(expectDisplay.size()), b(expectScene.size());
        const auto read0 = gpu.backend.read_texture(output0, a.data(), width * 8);
        const auto read1 = gpu.backend.read_texture(output1, b.data(), width * 8);
        AUREA_CHECK(read0.ok() && read1.ok());
        if (!read0.ok() || !read1.ok()) return false;
        u32 mismatches = 0, minX = width, maxX = 0, minY = height, maxY = 0;
        for (usize i = 0; i < a.size(); ++i) {
            const u32 x = static_cast<u32>(i / 4 % width), y = static_cast<u32>(i / 4 / width);
            for (u32 output = 0; output < 2; ++output) {
                const auto actual = output ? b[i] : a[i];
                const auto expected = output ? expectScene[i] : expectDisplay[i];
                if (actual == expected) continue;
                if (mismatches < 8) std::printf("\n    MRT mismatch samples=%u stage=%s output=%u xy=%u,%u channel=%zu actual_half=%u expected_half=%u actual=%.6f expected=%.6f",
                    samples, stage, output, x, y, i % 4, actual, expected,
                    half_to_float(actual), half_to_float(expected));
                ++mismatches; minX = std::min(minX, x); maxX = std::max(maxX, x);
                minY = std::min(minY, y); maxY = std::max(maxY, y);
            }
            AUREA_CHECK(std::isfinite(half_to_float(a[i])) && std::isfinite(half_to_float(b[i])));
            AUREA_CHECK_EQ(a[i], expectDisplay[i]); AUREA_CHECK_EQ(b[i], expectScene[i]);
        }
        std::printf("\n    MRT stage samples=%u stage=%s mismatches=%u bounds=%u,%u..%u,%u full_channels=%zu",
            samples, stage, mismatches, minX, minY, maxX, maxY, a.size() * 2);
        return a == expectDisplay && b == expectScene;
    }
    bool verify() {
        // Poison both outputs through actual MRT commands. A resolving pass
        // intentionally discards its MSAA storage, so the later Load control
        // first stores both multisampled colors in a pass WITHOUT resolves.
        if (!draw(true, LoadOp::Clear, false) || !pixels(displayPoison, scenePoison, "poison-full")) return false;
        if (!draw(false, LoadOp::DontCare, false) || !pixels(displayPixels, scenePixels, "pattern-full")) return false;
        if (!draw(true, LoadOp::Clear, false, {}, samples == 1)) return false;
        auto expectedDisplay = displayPoison, expectedScene = scenePoison;
        for (u32 y = 0; y < height; ++y) for (u32 x = width / 2; x < width; ++x)
            for (u32 channel = 0; channel < 4; ++channel) {
                const usize i = (y * width + x) * 4 + channel;
                expectedDisplay[i] = displayPixels[i]; expectedScene[i] = scenePixels[i];
            }
        if (!draw(false, LoadOp::Load, true) || !pixels(expectedDisplay, expectedScene, "pattern-partial-load")) return false;
        const Vec4 clear{.25f, .5f, .75f, 1.f};
        expectedDisplay = constant({clear.x, clear.y, clear.z, clear.w}); expectedScene = expectedDisplay;
        for (u32 y = 0; y < height; ++y) for (u32 x = width / 2; x < width; ++x)
            for (u32 channel = 0; channel < 4; ++channel) {
                const usize i = (y * width + x) * 4 + channel;
                expectedDisplay[i] = displayPixels[i]; expectedScene[i] = scenePixels[i];
            }
        if (!draw(false, LoadOp::Clear, true, clear) || !pixels(expectedDisplay, expectedScene, "pattern-partial-clear")) return false;
        std::vector<u16> original(scenePixels.size());
        AUREA_CHECK(gpu.backend.read_texture(scene, original.data(), width * 8).ok());
        AUREA_CHECK(original == scenePixels);
        AUREA_CHECK(gpu.backend.read_texture(display, original.data(), width * 8).ok());
        AUREA_CHECK(original == displayPixels);
        std::printf("\n    Vulkan MRT no depth: samples=%u submissions=%u readbacks=10 exact_channels=%zu load/clear/discard=verified validation=%u partial=%s",
            samples, submissions, scenePixels.size() * 8, gpu.backend.capabilities().validationEnabled,
            samples > 1 ? "geometry" : "scissor");
        return true;
    }
};

void verify_mrt_without_depth(u32 requestedSamples) {
    const u32 errors = vk::Backend::validation_errors();
    {
        Gpu isolated; AUREA_CHECK(isolated.ok); if (!isolated.ok) return;
#if defined(__linux__) && !defined(__ANDROID__)
        // The native CI must install the real Khronos layer; a disabled layer
        // cannot provide evidence of zero Vulkan validation errors.
        AUREA_CHECK_MSG(isolated.backend.capabilities().validationEnabled,
            "VulkanMrtGpu requires VK_LAYER_KHRONOS_validation on the native host");
        if (!isolated.backend.capabilities().validationEnabled) return;
#endif
        const u32 actual = isolated.backend.capabilities().msaa_samples(requestedSamples);
        AUREA_CHECK_EQ(actual, requestedSamples);
        if (actual != requestedSamples) return;
        MrtWithoutDepthGpu fixture{isolated, actual};
        if (fixture.setup()) AUREA_CHECK(fixture.verify());
    }
    const u32 newErrors = vk::Backend::validation_errors() - errors;
    AUREA_CHECK_EQ(newErrors, 0u);
    std::printf("\n    Vulkan MRT validation: requested_samples=%u errors=%u", requestedSamples, newErrors);
}
}

AUREA_TEST(VulkanMrtGpu, DepthlessDofWritesBothPoisonedOutputsAndPreservesLoadAndClear) {
    AUREA_REQUIRE_GPU(); verify_mrt_without_depth(1);
}

AUREA_TEST(VulkanMrtGpu, DepthlessMultisampleDofResolvesBothOutputsWithExactPixels) {
    AUREA_REQUIRE_GPU(); verify_mrt_without_depth(4);
}
#endif
