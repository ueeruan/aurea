// Ordering/alias ownership checks use the recording backend. Native full-pixel
// equality and physical fence retirement are covered separately by GPU tests.
AUREA_TEST(FrameGraphSegments, MemoryOrderNeverExpandsAnAlreadyStreamedMrtBranch) {
    GraphFixture f;
    const auto target = f.backend.create_texture(rt(64, 32));
    AUREA_CHECK(target.ok()); if (!target) return;
    const auto output = f.graph.import_texture("backdrop", *target, rt(64, 32));
    const auto primary = f.graph.create_texture("costly scene", rt(512, 256));
    const auto secondary = f.graph.create_texture("scene HDR", rt(512, 256));
    auto depthDesc = rt(512, 256); depthDesc.format = SurfaceFormat::Depth32F;
    const auto depth = f.graph.create_texture("scene depth", depthDesc);
    const auto scene = f.graph.add_raster_pass_depth("scene MRT", PassStage::Scene3D,
        primary, LoadOp::Clear, {}, depth, LoadOp::Clear, false, 1.f, {});
    f.graph.set_color1(scene, secondary);
    const auto consumeScene = f.graph.add_raster_pass("consume scene first", PassStage::Composite,
        output, LoadOp::Clear, {}, {});
    f.graph.read(consumeScene, primary);
    for (u32 i = 0; i < 6; ++i) {
        const auto source = f.graph.create_texture("later independent source", rt(256 + 32 * i, 128 + 16 * i));
        f.graph.add_raster_pass("cheap later producer", PassStage::Effects, source, LoadOp::Clear, {}, {});
        const auto consumer = f.graph.add_raster_pass("backdrop consumer", PassStage::Composite,
            output, LoadOp::Load, {}, {});
        f.graph.read(consumer, source);
    }
    // A later real reader preserves HDR across arbitrary small-source stages.
    const auto last = f.graph.add_raster_pass("HDR future reader", PassStage::Composite,
        output, LoadOp::Load, {}, {});
    f.graph.read(last, secondary);
    f.graph.set_output(output, ResourceState::ShaderRead);
    AUREA_CHECK(f.graph.prepare_segments().ok());
    const u64 scenePeak = 2 * rt(512, 256).estimated_bytes() + depthDesc.estimated_bytes();
    AUREA_CHECK_EQ(f.graph.stats().segmentBaselinePeakBytes, scenePeak);
    AUREA_CHECK(f.graph.stats().segmentGreedyPeakBytes > scenePeak);
    AUREA_CHECK_EQ(f.graph.stats().segmentEstimatedPeakBytes, scenePeak);
    AUREA_CHECK_EQ(f.graph.stats().segmentReorderedPasses, 0u);
    AUREA_CHECK_EQ(f.graph.order()[0], scene);
    AUREA_CHECK_EQ(f.graph.order()[1], consumeScene);
    AUREA_CHECK_EQ(f.graph.order().back(), last);
    f.backend.destroy_texture(*target);
}

AUREA_TEST(FrameGraphSegments, MemoryOrderConsumesIndependentSourcesBeforeAllocatingTheNext) {
    GraphFixture f;
    const auto target = f.backend.create_texture(rt(32, 16));
    AUREA_CHECK(target.ok()); if (!target) return;
    const auto output = f.graph.import_texture("external output", *target, rt(32, 16));
    std::array<FGTexture, 3> sources;
    std::array<u32, 3> writers, readers;
    for (u32 i = 0; i < sources.size(); ++i) {
        sources[i] = f.graph.create_texture("producer wave", rt(512 + i * 64, 256 + i * 32));
        writers[i] = f.graph.add_raster_pass("source", PassStage::Effects, sources[i], LoadOp::Clear, {}, {});
    }
    for (u32 i = 0; i < sources.size(); ++i) {
        readers[i] = f.graph.add_raster_pass("consume", PassStage::Composite, output, LoadOp::Load, {}, {});
        f.graph.read(readers[i], sources[i]);
    }
    const auto unused = f.graph.create_texture("culled", rt(4096, 4096));
    const auto dead = f.graph.add_raster_pass("culled", PassStage::Effects, unused, LoadOp::Clear, {}, {});
    f.graph.set_output(output, ResourceState::ShaderRead);
    FrameBegin frame; AUREA_CHECK(f.backend.begin_offscreen_frame(frame).ok());
    f.pool.begin_frame(f.backend, frame.frameNumber);
    const u64 limit = rt(640, 320).estimated_bytes();
    f.pool.set_allocation_limit(limit);
    AUREA_CHECK_EQ(f.graph.compile(f.pool).code(), Errc::OutOfDeviceMemory);
    const auto ordinary = f.graph.order();
    AUREA_CHECK_EQ(ordinary[0], writers[0]); AUREA_CHECK_EQ(ordinary[1], writers[1]);
    f.graph.release(f.pool); AUREA_CHECK(f.backend.end_frame().ok());
    AUREA_CHECK(f.backend.wait_frame(frame.frameNumber, 1).ok());
    f.pool.trim_unreferenced(frame.frameNumber);
    AUREA_CHECK(f.graph.prepare_segments().ok());
    AUREA_CHECK(f.graph.pass_culled(dead));
    AUREA_CHECK(f.graph.stats().segmentReorderedPasses > 0);
    AUREA_CHECK_EQ(f.graph.stats().segmentEstimatedPeakBytes, limit);
    for (u32 i = 0; i < sources.size(); ++i) {
        AUREA_CHECK_EQ(f.graph.order()[i * 2], writers[i]);
        AUREA_CHECK_EQ(f.graph.order()[i * 2 + 1], readers[i]);
    }
    for (u32 pos = 0; pos < f.graph.order().size(); ++pos) {
        AUREA_CHECK(f.backend.begin_offscreen_frame(frame).ok());
        f.pool.begin_frame(f.backend, frame.frameNumber);
        AUREA_CHECK(f.graph.compile_segment(f.pool, pos, 1).ok());
        AUREA_CHECK(f.pool.stats().bytes <= limit);
        f.graph.execute(*frame.commands, false);
        AUREA_CHECK(f.backend.end_frame().ok()); AUREA_CHECK(f.backend.wait_frame(frame.frameNumber, 1).ok());
        f.graph.release_completed_segment(f.pool); f.pool.trim_unreferenced(frame.frameNumber);
    }
    f.graph.release(f.pool); f.pool.clear(); f.backend.destroy_texture(*target);
}

AUREA_TEST(FrameGraphSegments, MemoryOrderPreservesSideEffectsWithLateDeclaredProducers) {
    GraphFixture f;
    const auto source = f.graph.create_texture("late producer", rt(128, 128));
    const auto out = f.graph.create_texture("output", rt());
    const auto consumer = f.graph.add_raster_pass("side consumer", PassStage::Effects, out, LoadOp::Clear, {}, {});
    f.graph.read(consumer, source); f.graph.mark_side_effect(consumer);
    const auto observer = f.graph.add_transfer_pass("side observer", PassStage::Effects, {});
    f.graph.mark_side_effect(observer);
    const auto producer = f.graph.add_raster_pass("side producer", PassStage::Effects, source, LoadOp::Clear, {}, {});
    f.graph.mark_side_effect(producer); f.graph.set_output(out, ResourceState::ShaderRead);
    AUREA_CHECK(f.graph.prepare_segments().ok());
    AUREA_CHECK_EQ(f.graph.order().size(), 3u);
    AUREA_CHECK_EQ(f.graph.order()[0], observer);
    AUREA_CHECK_EQ(f.graph.order()[1], producer);
    AUREA_CHECK_EQ(f.graph.order()[2], consumer);
}

AUREA_TEST(FrameGraphSegments, MemoryOrderPreservesImportedAliasesAndFutureVersionReaders) {
    GraphFixture f;
    const auto target = f.backend.create_texture(rt()); AUREA_CHECK(target.ok()); if (!target) return;
    const auto a = f.graph.import_texture("native attachment A", *target, rt());
    const auto b = f.graph.import_texture("same native attachment B", *target, rt());
    const auto first = f.graph.add_raster_pass("initial A write", PassStage::Effects, a, LoadOp::Clear, {}, {});
    const auto load = f.graph.add_raster_pass("B load", PassStage::Effects, b, LoadOp::Load, {}, {});
    const auto kept = f.graph.create_texture("marked future reader", rt(80, 80));
    const auto read = f.graph.add_raster_pass("read A version", PassStage::Effects, kept, LoadOp::Clear, {}, {});
    f.graph.read(read, a); f.graph.set_output(kept, ResourceState::ShaderRead);
    const auto rewrite = f.graph.add_raster_pass("rewrite A after reader", PassStage::Effects, a, LoadOp::Clear, {}, {});
    const auto last = f.graph.add_raster_pass("B load after rewrite", PassStage::Composite, b, LoadOp::Load, {}, {});
    f.graph.read(last, kept); f.graph.set_output(a, ResourceState::ShaderRead); f.graph.set_output(b, ResourceState::ShaderRead);
    AUREA_CHECK(f.graph.prepare_segments().ok());
    const std::array<u32, 5> expected{first, load, read, rewrite, last};
    AUREA_CHECK_EQ(f.graph.order().size(), expected.size());
    for (u32 i = 0; i < expected.size(); ++i) AUREA_CHECK_EQ(f.graph.order()[i], expected[i]);
    AUREA_CHECK_EQ(f.graph.stats().passesCulled, 0u);
    f.backend.destroy_texture(*target);
}
