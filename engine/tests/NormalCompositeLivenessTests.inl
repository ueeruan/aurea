// Included by test_render.cpp. Exercise the actual effect graph and physical
// texture aliasing, rather than estimating memory from a compositor batch size.
AUREA_TEST(MemoryPressure, ExpandedNormalEffectOutputsAreConsumedBeforeTheNextLayer) {
    for (const u32 denominator : {1u, 2u}) {
        RenderFixture fixture;
        constexpr u32 count = 8;
        for (u32 i = 0; i < count; ++i) {
            const auto id = fixture.solid("tiled full-HD source", Vec4{.3f, .6f, .9f, .65f},
                                          120.f + 240.f * (i % 8), 180.f);
            auto* layer = fixture.comp->layer(id);
            layer->shape.bounds = Rect{0, 0, 1920, 1080};
            layer->transform.anchor = Vec3{960, 540, 0};
            layer->transform.scale = Vec3{.105f, .105f, 1};
            auto tile = make_effect(fixture.effects, effect_keys::kMotionTile, 0);
            tile.params[motion_tile::kOutputWidth].constant.v[0] = 150;
            tile.params[motion_tile::kOutputHeight].constant.v[0] = 150;
            tile.params[motion_tile::kMirror].constant.v[0] = 1;
            tile.params[motion_tile::kScale].constant.v[0] = 70;
            layer->effects.push_back(std::move(tile));
            auto blur = make_effect(fixture.effects, effect_keys::kGaussianBlur, 1);
            blur.params[0].constant.v[0] = 3;
            layer->effects.push_back(std::move(blur));
            auto glow = make_effect(fixture.effects, effect_keys::kGlow, 2);
            glow.params[0].constant.v[0] = 20;
            layer->effects.push_back(std::move(glow));
        }
        RenderSettings settings;
        settings.previewDenominator = denominator;
        settings.finalQuality = true;
        settings.gpuTimers = true;
        const u64 limit = (160ull << 20) / (denominator * denominator);
        // This is a deliberately tight fixture envelope, not a larger device
        // budget or a lower-resolution substitute for an expanded effect.
        fixture.renderer.set_transient_allocation_limit(limit);
        for (u64 frame = 1; frame <= 3; ++frame) {
            FrameSnapshot snapshot;
            fixture.renderer.prepare(*fixture.comp, fixture.project, FrameIndex{0}, nullptr, nullptr, nullptr,
                                     settings, frame, 0, DecodeMode::Still, 1, snapshot);
            AUREA_CHECK_EQ(snapshot.layers.size(), static_cast<usize>(count));
            for (const auto& plan : snapshot.plans) AUREA_CHECK_EQ(plan.evals.size(), usize{3});
            fixture.backend.events.clear();
            FrameStats stats; RenderTimings timings;
            const Status rendered = fixture.renderer.render(snapshot, settings, nullptr, stats, timings);
            std::printf("\n    Normal expanded effects den=%u frame=%llu: pool=%llu slots=%u status=%u",
                        denominator, static_cast<unsigned long long>(frame),
                        static_cast<unsigned long long>(fixture.renderer.pool_stats().bytes),
                        fixture.renderer.graph_stats().physicalTextures, static_cast<unsigned>(rendered.code()));
            AUREA_CHECK(rendered.ok());
            if (!rendered.ok()) continue;
            AUREA_CHECK_EQ(stats.layersRendered, count);
            AUREA_CHECK(fixture.renderer.graph_stats().transientBytes <= limit);
            u32 tiles = 0, completed = 0, compositePasses = 0;
            u64 awaitingOutput = 0;
            const char* label = "";
            for (const auto& event : fixture.backend.events) {
                if (event.kind == MockBackend::Event::Timer) label = event.label ? event.label : "";
                if (event.kind == MockBackend::Event::BeginPass && std::strcmp(label, "motion-tile") == 0) {
                    // The preceding expanded result must have reached its
                    // consumer before the next layer allocates its effect chain.
                    AUREA_CHECK_EQ(awaitingOutput, u64{0});
                    ++tiles;
                    const auto desc = fixture.backend.texture_desc(TextureHandle{event.texture});
                    AUREA_CHECK(desc.width >= 1920 / denominator);
                    AUREA_CHECK(desc.height >= 1080 / denominator);
                    AUREA_CHECK(desc.format == SurfaceFormat::RGBA16F);
                }
                if (event.kind == MockBackend::Event::BeginPass
                    && (std::strcmp(label, "glow-soma") == 0 || std::strcmp(label, "brilho-oitavas") == 0)) {
                    AUREA_CHECK_EQ(awaitingOutput, u64{0});
                    awaitingOutput = event.texture;
                    ++completed;
                }
                if (event.kind == MockBackend::Event::BeginPass && std::strcmp(label, "composicao") == 0) {
                    AUREA_CHECK(event.load == (compositePasses ? LoadOp::Load : LoadOp::Clear));
                    ++compositePasses;
                }
                if (event.kind == MockBackend::Event::BindTexture && std::strcmp(label, "composicao") == 0
                    && event.slot == 0 && event.texture == awaitingOutput) awaitingOutput = 0;
            }
            AUREA_CHECK_EQ(tiles, count);
            AUREA_CHECK_EQ(completed, count);
            AUREA_CHECK_EQ(awaitingOutput, u64{0});
            if (frame == 3) AUREA_CHECK_EQ(fixture.renderer.pool_stats().createdThisFrame, 0u);
        }
    }
}
