#if defined(AUREA_TEST_VULKAN)
namespace {
// Real commands, shader samples, render-pass loads and fences. The pool cap
// creates the same exact-descriptor residency problem as the large editor
// graph; it never substitutes a driver, removes a pass or scales an image.
struct SegmentPixels {
    std::vector<u16> output, retained, imported;
    u64 peakPool = 0;
    u32 callbacks = 0, passes = 0, submissions = 0;
    bool wholeOom = false, ok = false;
};

struct SegmentGpuGraph {
    static constexpr u32 width = 64, height = 32, scratchCount = 6;
    Gpu& gpu;
    FrameGraph graph;
    TransientTexturePool pool;
    TextureHandle input, background;
    FGTexture anchor, secondary, retained, output;
    PipelineHandle copy;
    SamplerHandle sampler;
    std::vector<u16> inputPixels, backgroundPixels;
    bool open = false;
    bool groupedSources = false;
    u32 callbacks = 0, executed = 0;

    explicit SegmentGpuGraph(Gpu& source, bool grouped = false) : gpu(source), groupedSources(grouped) {}
    ~SegmentGpuGraph() {
        if (open) (void)gpu.backend.end_frame();
        gpu.backend.wait_idle();
        graph.release(pool); pool.clear();
        if (input.valid()) gpu.backend.destroy_texture(input);
        if (background.valid()) gpu.backend.destroy_texture(background);
        gpu.backend.wait_idle();
    }
    static TextureDesc desc(u32 w = width, u32 h = height) {
        TextureDesc result; result.width = w; result.height = h;
        result.format = SurfaceFormat::RGBA16F;
        result.sampled = result.renderTarget = result.transferSrc = result.transferDst = true;
        return result;
    }
    static u64 pressure_limit() {
        return 2 * desc(512 + 32 * (scratchCount - 1), 256 + 16 * (scratchCount - 1)).estimated_bytes()
             + 5 * desc().estimated_bytes();
    }
    bool setup() {
        const auto a = gpu.backend.create_texture(desc());
        const auto b = gpu.backend.create_texture(desc());
        AUREA_CHECK(a.ok() && b.ok()); if (!a.ok() || !b.ok()) return false;
        input = *a; background = *b;
        inputPixels.resize(width * height * 4); backgroundPixels.resize(inputPixels.size());
        for (u32 y = 0; y < height; ++y) for (u32 x = 0; x < width; ++x) {
            const usize p = (y * width + x) * 4;
            inputPixels[p] = scene3d::float_to_half((x + 1.f) / (width + 1.f));
            inputPixels[p + 1] = scene3d::float_to_half((y + 1.f) / (height + 1.f));
            inputPixels[p + 2] = scene3d::float_to_half((x % 5 + y % 3 + 1.f) / 10.f);
            inputPixels[p + 3] = scene3d::float_to_half(1.f);
            backgroundPixels[p] = scene3d::float_to_half(.0625f);
            backgroundPixels[p + 1] = scene3d::float_to_half(.03125f);
            backgroundPixels[p + 2] = scene3d::float_to_half(.015625f);
            backgroundPixels[p + 3] = scene3d::float_to_half(1.f);
        }
        AUREA_CHECK(gpu.backend.upload_texture(input, inputPixels.data(), width * 8).ok());
        AUREA_CHECK(gpu.backend.upload_texture(background, backgroundPixels.data(), width * 8).ok());
        const auto pipeline = gpu.renderer.shaders().pipeline(
            PipelineKey::fullscreen(ShaderId::common_copy_frag, SurfaceFormat::RGBA16F));
        AUREA_CHECK(pipeline.ok()); if (!pipeline.ok()) return false;
        copy = *pipeline; sampler = gpu.renderer.shaders().sampler(CommonSampler::NearestClamp);
        build(); return true;
    }
    void draw(PassContext& pc, FGTexture source, i32 x, i32 y, u32 w, u32 h) {
        const Vec4 uv{1.f, 1.f, 0, 0};
        pc.cmds.bind_pipeline(copy); pc.cmds.bind_texture(0, pc.texture(source), sampler);
        pc.cmds.set_uniforms(&uv, sizeof(uv));
        pc.cmds.set_scissor(x, y, w, h); pc.cmds.draw(3); ++executed;
    }
    void build() {
        const auto source = graph.import_texture("pinned-source", input, desc());
        output = graph.import_texture("pinned-background-output", background, desc());
        anchor = graph.create_texture("future-reader-anchor", desc());
        secondary = graph.create_texture("future-reader-mrt", desc());
        auto depthDesc = desc(); depthDesc.format = SurfaceFormat::Depth32F;
        const auto depth = graph.create_texture("mrt-scene-depth", depthDesc);
        const auto first = graph.add_raster_pass_depth("mrt-clear", PassStage::Effects, anchor,
            LoadOp::Clear, Vec4{.125f, .25f, .5f, 1.f}, depth, LoadOp::Clear, false, 1.f,
            [this](PassContext&) { ++executed; });
        graph.set_color1(first, secondary);
        retained = graph.create_texture("early-retained-output", desc());
        const auto keep = graph.add_raster_pass("early-output-copy", PassStage::Effects, retained,
            LoadOp::Clear, {}, [this, source](PassContext& pc) { draw(pc, source, 0, 0, width, height); });
        graph.read(keep, source);
        graph.set_output(retained, ResourceState::ShaderRead);
        std::array<FGTexture, scratchCount> scratchSources;
        const auto consumeScratch = [&](FGTexture scratch, u32 i) {
            const auto consume = graph.add_raster_pass("preserve-background-load", PassStage::Composite,
                output, LoadOp::Load, {}, [this, scratch, i](PassContext& pc) {
                    draw(pc, scratch, 4 + static_cast<i32>(i) * 8, 4, 8, 16);
                });
            graph.read(consume, scratch);
        };
        for (u32 i = 0; i < scratchCount; ++i) {
            const auto d = desc(512 + i * 32, 256 + i * 16);
            const auto scratch = graph.create_texture("distinct-sized-scratch", d);
            const auto create = graph.add_raster_pass("scratch-sample", PassStage::Effects, scratch,
                LoadOp::Clear, {}, [this, source, d](PassContext& pc) { draw(pc, source, 0, 0, d.width, d.height); });
            graph.read(create, source);
            scratchSources[i] = scratch;
            if (!groupedSources) consumeScratch(scratch, i);
        }
        if (groupedSources) for (u32 i = 0; i < scratchCount; ++i) consumeScratch(scratchSources[i], i);
        const auto load = graph.add_raster_pass("late-anchor-load", PassStage::Effects,
            anchor, LoadOp::Load, {}, [this, source](PassContext& pc) { draw(pc, source, 0, 0, width / 2, height); });
        graph.read(load, source);
        const auto last = graph.add_raster_pass("future-reader-anchor-copy", PassStage::Composite,
            output, LoadOp::Load, {}, [this](PassContext& pc) { draw(pc, anchor, 4, 20, 28, 8); });
        graph.read(last, anchor);
        const auto mrt = graph.add_raster_pass("future-reader-mrt-copy", PassStage::Composite,
            output, LoadOp::Load, {}, [this](PassContext& pc) { draw(pc, secondary, 32, 20, 28, 8); });
        graph.read(mrt, secondary);
        graph.set_output(output, ResourceState::ShaderRead);
    }
    bool begin(FrameBegin& frame) {
        const auto status = gpu.backend.begin_offscreen_frame(frame);
        AUREA_CHECK(status.ok()); if (!status.ok()) return false;
        open = true; AUREA_CHECK(!frame.backbuffer.valid());
        pool.begin_frame(gpu.backend, frame.frameNumber); return true;
    }
    bool finish(const FrameBegin& frame) {
        gpu.backend.defer_until_gpu_done([](void* context) { ++*static_cast<u32*>(context); }, &callbacks);
        const auto end = gpu.backend.end_frame(); open = false;
        AUREA_CHECK(end.ok()); if (!end.ok()) return false;
        const auto completed = gpu.backend.wait_frame(frame.frameNumber, 5'000'000'000ull);
        AUREA_CHECK(completed.ok()); return completed.ok();
    }
    SegmentPixels run(u32 chunk, bool pressure) {
        SegmentPixels result;
        if (pressure) pool.set_allocation_limit(pressure_limit());
        FrameBegin frame; if (!begin(frame)) return result;
        if (!chunk) {
            const auto compiled = graph.compile(pool);
            AUREA_CHECK(compiled.ok()); if (!compiled.ok()) return result;
            result.peakPool = pool.stats().bytes;
            graph.execute(*frame.commands, false);
            if (!finish(frame)) return result;
            ++result.submissions;
        } else {
            if (pressure) {
                const auto full = graph.compile(pool);
                result.wholeOom = full.code() == Errc::OutOfDeviceMemory;
                AUREA_CHECK(result.wholeOom);
                // No callback/pass was executed. Withdraw the failed full
                // plan only after its normal submission/fence has completed.
                AUREA_CHECK_EQ(executed, 0u);
                graph.release(pool);
                if (!finish(frame)) return result;
                ++result.submissions;
                pool.trim_unreferenced(frame.frameNumber);
                AUREA_CHECK_EQ(pool.stats().bytes, 0u);
            } else {
                // Close the unused initial frame before opening segments.
                if (!finish(frame)) return result;
                ++result.submissions;
            }
            const auto planned = graph.prepare_segments();
            AUREA_CHECK(planned.ok()); if (!planned.ok()) return result;
            const u32 count = static_cast<u32>(graph.order().size());
            const auto position = [&](const char* name) {
                for (u32 i = 0; i < count; ++i)
                    if (std::strcmp(graph.pass_name(graph.order()[i]), name) == 0) return i;
                return kInvalidIndex;
            };
            const u32 mrtStart = position("mrt-clear");
            const u32 anchorLast = position("future-reader-anchor-copy");
            const u32 secondaryLast = position("future-reader-mrt-copy");
            const u32 retainedStart = position("early-output-copy");
            AUREA_CHECK(mrtStart < anchorLast && mrtStart < secondaryLast);
            for (u32 first = 0; first < count; first += chunk) {
                if (!begin(frame)) return result;
                const auto compiled = graph.compile_segment(pool, first, chunk);
                AUREA_CHECK(compiled.ok()); if (!compiled.ok()) return result;
                result.peakPool = std::max(result.peakPool, pool.stats().bytes);
                graph.execute(*frame.commands, false);
                if (!finish(frame)) return result;
                ++result.submissions;
                // These early allocations must outlive arbitrary intervening
                // scratch sizes and retain their exact native handles.
                const u32 end = std::min(count, first + chunk);
                // Recovery may postpone unrelated producers. Require the same
                // native allocations from their actual birth through their
                // future reader, including arbitrary intervening submissions.
                const bool anchorHeld = end > mrtStart && end <= anchorLast;
                const bool secondaryHeld = end > mrtStart && end <= secondaryLast;
                if (anchorHeld) AUREA_CHECK(graph.physical(anchor).valid());
                if (secondaryHeld) AUREA_CHECK(graph.physical(secondary).valid());
                graph.release_completed_segment(pool);
                pool.trim_unreferenced(frame.frameNumber);
                const u32 requiredHeld = static_cast<u32>(anchorHeld) + static_cast<u32>(secondaryHeld)
                    + static_cast<u32>(end > retainedStart);
                AUREA_CHECK(pool.stats().inUse >= requiredHeld);
            }
        }
        const auto kept = graph.physical(retained);
        AUREA_CHECK(kept.valid());
        result.output.resize(width * height * 4); result.retained.resize(result.output.size());
        result.imported.resize(result.output.size());
        const auto a = gpu.backend.read_texture(background, result.output.data(), width * 8);
        const auto b = gpu.backend.read_texture(kept, result.retained.data(), width * 8);
        const auto c = gpu.backend.read_texture(input, result.imported.data(), width * 8);
        AUREA_CHECK(a.ok() && b.ok() && c.ok());
        AUREA_CHECK(result.imported == inputPixels);
        AUREA_CHECK(result.retained == inputPixels);
        AUREA_CHECK(std::count_if(result.output.begin(), result.output.end(),
            [](u16 value) { return value != 0; }) > width * height);
        const std::array<f32, 4> mrtColor{.125f, .25f, .5f, 1.f};
        for (u32 channel = 0; channel < 4; ++channel) {
            AUREA_CHECK_EQ(result.output[(24 * width + 40) * 4 + channel],
                           scene3d::float_to_half(mrtColor[channel]));
            // The late Load preserves the unpainted anchor half, while its
            // painted half still contains the actual sampled source pixels.
            AUREA_CHECK_EQ(result.output[(24 * width + 16) * 4 + channel],
                           inputPixels[(24 * width + 16) * 4 + channel]);
        }
        // Outside all scissor rectangles, imported background is unchanged.
        for (u32 x = 0; x < width; ++x) for (u32 c0 = 0; c0 < 4; ++c0)
            AUREA_CHECK_EQ(result.output[x * 4 + c0], backgroundPixels[x * 4 + c0]);
        result.callbacks = callbacks; result.passes = executed;
        AUREA_CHECK_EQ(callbacks, result.submissions);
        AUREA_CHECK_EQ(executed, static_cast<u32>(graph.order().size()));
        graph.release(pool); pool.trim_unreferenced(frame.frameNumber);
        AUREA_CHECK_EQ(pool.stats().bytes, 0u);
        if (pressure) AUREA_CHECK(result.peakPool <= pressure_limit());
        result.ok = a.ok() && b.ok() && c.ok();
        std::printf("\n    framegraph segments: chunk=%u pressure=%u wholeOOM=%u passes=%u submissions=%u peak=%llu limit=%llu channels=%zu",
            chunk, pressure, result.wholeOom, result.passes, result.submissions,
            static_cast<unsigned long long>(result.peakPool),
            static_cast<unsigned long long>(pressure ? pressure_limit() : 0), result.output.size());
        return result;
    }
};
}

AUREA_TEST(FrameGraphSegmentsGpu, RealFencedSegmentsPreserveEveryPixelAndImportedBackground) {
    AUREA_REQUIRE_GPU();
    Gpu isolated; AUREA_CHECK(isolated.ok); if (!isolated.ok) return;
    SegmentPixels baseline;
    for (const u32 chunk : {0u, 1u, 2u, 3u}) {
        SegmentGpuGraph fixture(isolated); if (!fixture.setup()) return;
        const auto pixels = fixture.run(chunk, false);
        AUREA_CHECK(pixels.ok); if (!pixels.ok) return;
        if (!chunk) baseline = pixels;
        else {
            AUREA_CHECK(pixels.output == baseline.output);
            AUREA_CHECK(pixels.retained == baseline.retained);
            AUREA_CHECK_EQ(pixels.passes, baseline.passes);
        }
    }
}

AUREA_TEST(FrameGraphSegmentsGpu, PartialWholeGraphOomRecoversWithoutChangingPassesOrPixels) {
    AUREA_REQUIRE_GPU();
    Gpu isolated; AUREA_CHECK(isolated.ok); if (!isolated.ok) return;
    SegmentPixels baseline;
    {
        SegmentGpuGraph fixture(isolated); if (!fixture.setup()) return;
        baseline = fixture.run(0, false); AUREA_CHECK(baseline.ok); if (!baseline.ok) return;
    }
    for (const u32 chunk : {1u, 2u}) for (u32 repeat = 0; repeat < 2; ++repeat) {
        SegmentGpuGraph fixture(isolated); if (!fixture.setup()) return;
        const auto pixels = fixture.run(chunk, true);
        AUREA_CHECK(pixels.ok); AUREA_CHECK(pixels.wholeOom);
        AUREA_CHECK(pixels.output == baseline.output);
        AUREA_CHECK(pixels.retained == baseline.retained);
        AUREA_CHECK_EQ(pixels.passes, baseline.passes);
        AUREA_CHECK(pixels.peakPool < baseline.peakPool);
    }
}

AUREA_TEST(FrameGraphSegmentsGpu, MemoryOrderedProducerWavePreservesEveryPixelUnderPressure) {
    AUREA_REQUIRE_GPU();
    Gpu isolated; AUREA_CHECK(isolated.ok); if (!isolated.ok) return;
    SegmentPixels baseline;
    {
        SegmentGpuGraph fixture(isolated, true); if (!fixture.setup()) return;
        baseline = fixture.run(0, false); AUREA_CHECK(baseline.ok); if (!baseline.ok) return;
    }
    for (const u32 chunk : {1u, 2u}) {
        SegmentGpuGraph fixture(isolated, true); if (!fixture.setup()) return;
        const auto pixels = fixture.run(chunk, true);
        AUREA_CHECK(pixels.ok); AUREA_CHECK(pixels.wholeOom);
        AUREA_CHECK(pixels.output == baseline.output);
        AUREA_CHECK(pixels.retained == baseline.retained);
        AUREA_CHECK(pixels.imported == baseline.imported);
        AUREA_CHECK_EQ(pixels.passes, baseline.passes);
        AUREA_CHECK(fixture.graph.stats().segmentReorderedPasses > 0);
        AUREA_CHECK(pixels.peakPool < baseline.peakPool);
    }
}
#endif
