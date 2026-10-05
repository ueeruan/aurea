#if defined(AUREA_TEST_VULKAN)
AUREA_TEST(FlowCacheSourceGpu, ReopenedOriginalCannotReuseProxyMotionAtIdenticalTimestamps) {
    AUREA_REQUIRE_GPU();
    CaptureValidationGuard validation;
    struct Factory final : VideoSourceFactory {
        SyntheticConfig config;
        bool original = false;
        Factory() { config.width = 96; config.height = 54; config.pattern = SyntheticPattern::FastSquare; }
        bool probe(const char* path, MediaProbe& out) override { SyntheticFactory p(config); return p.probe(path, out); }
        std::unique_ptr<VideoDecoderBackend> open_video(const Asset&, MediaPriority) override {
            auto c = config;
            // A ready low-resolution proxy has stationary content here. Using
            // its zero flow on the sharp moving original produces an observable
            // missing trail, not just a different cache counter.
            if (!original) { c.width = 48; c.height = 26; c.pattern = SyntheticPattern::Quadrants; }
            return std::make_unique<SyntheticDecoder>(c);
        }
    } factory;
    Scene s(96, 54);
    const LayerId id = s.video(factory.config, 48, 27);
    s.media.set_factory(&factory);
    s.comp->layer(id)->vectorBlur = 1;
    (void)s.render(FrameIndex{50});
    u32 hits0 = 0, misses0 = 0;
    gpu().renderer.flow_cache_stats(hits0, misses0);
    s.media.close_all(); // real proxy/original switches retire the old decoder
    factory.original = true;
    const FloatImage actual = s.render(FrameIndex{50}, 1, true);
    u32 hits1 = 0, misses1 = 0;
    gpu().renderer.flow_cache_stats(hits1, misses1);
    gpu().renderer.set_flow_cache_enabled(false);
    const FloatImage reference = s.render(FrameIndex{50}, 1, true);
    gpu().renderer.set_flow_cache_enabled(true);
    f32 maxError = 0; u32 changed = 0;
    AUREA_CHECK_EQ(actual.px.size(), reference.px.size());
    for (usize i = 0; i < std::min(actual.px.size(), reference.px.size()); ++i) {
        const f32 d = std::abs(actual.px[i] - reference.px[i]);
        maxError = std::max(maxError, d); if (d > .001f) ++changed;
    }
    std::printf("    proxy/original same PTS: cache +%u hits +%u computes; max pixel difference %.6f, changed channels %u\n",
        hits1 - hits0, misses1 - misses0, maxError, changed);
    AUREA_CHECK(misses1 > misses0);
    AUREA_CHECK_EQ(hits1, hits0);
    AUREA_CHECK(maxError <= .001f);
    u32 hits2 = 0, misses2 = 0;
    gpu().renderer.flow_cache_stats(hits2, misses2);
    const FloatImage repeated = s.render(FrameIndex{50}, 1, true);
    u32 hits3 = 0, misses3 = 0;
    gpu().renderer.flow_cache_stats(hits3, misses3);
    AUREA_CHECK(hits3 > hits2);
    AUREA_CHECK_EQ(misses3, misses2);
    AUREA_CHECK(repeated.px == reference.px);
}
#endif
