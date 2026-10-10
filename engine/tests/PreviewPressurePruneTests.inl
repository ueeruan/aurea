AUREA_TEST(DecodedFrameCache, PressurePruneProtectsTheCurrentIntervalAndReportsOnlyItsOwnVersion) {
    MemoryManager memory; ExclusivePruneAudit audit{memory}; DecodedFrameCache cache;
    cache.attach(&memory); cache.configure({5, 1u << 20});
    const u64 bytes = 64u * 36u * 3u / 2u;
    for (u32 i = 0; i < 5; ++i) AUREA_CHECK(cache.insert(owned_prune_frame(audit, i * kFrame)));
    const i64 target = 2 * kFrame + 700; // VFR interval, not a nominal PTS match
    const u32 before = cache.stats().version;
    auto missing = cache.reclaim_unused_for(30 * kFrame, 0);
    AUREA_CHECK(!missing.retainedTarget); AUREA_CHECK_EQ(missing.freed, usize{0});
    AUREA_CHECK_EQ(missing.versionBefore, before); AUREA_CHECK_EQ(missing.versionAfter, before);
    const auto result = cache.reclaim_unused_for(target, 0);
    AUREA_CHECK(result.retainedTarget); AUREA_CHECK_EQ(result.freed, 4 * bytes);
    AUREA_CHECK_EQ(result.versionBefore, before); AUREA_CHECK_EQ(result.versionAfter, before + 1);
    AUREA_CHECK_EQ(audit.destroyed, 4u); AUREA_CHECK_EQ(audit.releasedBackingBytes, result.freed);
    AUREA_CHECK(cache.contains(target, 0)); AUREA_CHECK_EQ(cache.stats().frames, 1u);
    AUREA_CHECK_EQ(memory.used(MemoryClass::DecodedFrames), bytes);
}

AUREA_TEST(VideoSource, PressurePruneDoesNotRefillTheSameCompletedPlaybackRequest) {
    SyntheticConfig cfg; cfg.pattern = SyntheticPattern::FrameGray;
    auto decoder = std::make_unique<SyntheticDecoder>(cfg); auto* raw = decoder.get();
    VideoSource source(std::move(decoder), MediaPriority::Preview);
    source.cache().configure({3, 1u << 20}); source.start();
    const i64 target = raw->pts_of(60);
    const DecodeRequest request{target, DecodeMode::Playback, 1, 1.f};
    source.request(request);
    AUREA_CHECK(source.wait_for(raw->pts_of(62), 1000));
    // A normal settled preview repeats the current request before pressure.
    // This acknowledges no unrelated invalidation and creates no new work.
    source.request(request);
    usize freed = 0;
    wait_until([&] { freed += source.reclaim_idle_prefetch(); return freed > 0; }, 1000);
    AUREA_CHECK_EQ(freed, 2 * 64u * 36u * 3u / 2u);
    AUREA_CHECK_EQ(source.cache().stats().frames, 1u);
    const auto settled = source.stats(); const u32 decoded = raw->decoded.load();
    for (u32 i = 0; i < 20; ++i) {
        source.request(request); std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(60));
    AUREA_CHECK_EQ(source.stats().requests, settled.requests);
    AUREA_CHECK_EQ(raw->decoded.load(), decoded);
    AUREA_CHECK_EQ(source.cache().stats().frames, 1u);
    bool exact = false; FrameRef displayed = source.frame_for(target, &exact);
    AUREA_CHECK(displayed && exact);
    if (displayed) AUREA_CHECK_EQ(displayed->planes[0][0], frame_gray_code(60));
    displayed.reset();
    // A different target gets the unchanged decoder/prefetch behavior.
    const DecodeRequest next{raw->pts_of(61), DecodeMode::Playback, 1, 1.f};
    source.request(next); AUREA_CHECK(source.wait_for(next.targetUs, 1000));
    AUREA_CHECK(source.wait_for(raw->pts_of(63), 1000));
    AUREA_CHECK(source.stats().requests > settled.requests);
    // A real invalidation, even while the target survives initially, must
    // remain distinguishable from the pressure-prune transition.
    source.cache().set_required_times(&next.targetUs, 1, 1);
    const auto changed = source.stats().requests;
    wait_until([&] { source.request(next); return source.stats().requests > changed; }, 1000);
    AUREA_CHECK(source.stats().requests > changed);
    source.cache().clear(); source.request(next);
    AUREA_CHECK(source.wait_for(next.targetUs, 1000));
    const auto epochRequests = source.stats().requests;
    source.set_epoch(1); source.request(next);
    AUREA_CHECK(source.stats().requests > epochRequests);
    source.stop();
}

AUREA_TEST(VideoSource, PressurePruneNeverRacesAnActiveCodecOutputOrAcknowledgesAnotherTrim) {
    struct GatedDecoder final : VideoDecoderBackend {
        SyntheticDecoder inner{SyntheticConfig{}};
        std::atomic<bool> entered{false}, release{false};
        u32 calls = 0;
        const VideoStreamInfo& info() const noexcept override { return inner.info(); }
        u32 max_live_frames() const noexcept override { return inner.max_live_frames(); }
        Status seek_to_keyframe(i64 time) noexcept override { return inner.seek_to_keyframe(time); }
        Status next_frame(i64 from, FrameRef& out, i64& pts, bool& eos) noexcept override {
            if (++calls == 2) {
                entered.store(true);
                while (!release.load()) std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            return inner.next_frame(from, out, pts, eos);
        }
    };
    auto decoder = std::make_unique<GatedDecoder>(); auto* raw = decoder.get();
    VideoSource source(std::move(decoder), MediaPriority::Preview);
    source.cache().configure({4, 1u << 20}); source.start();
    const DecodeRequest request{0, DecodeMode::Playback, 1, 1.f}; source.request(request);
    wait_until([&] { return raw->entered.load(); }, 1000);
    const bool entered = raw->entered.load();
    AUREA_CHECK(entered);
    const auto busy = source.cache().stats();
    AUREA_CHECK_EQ(source.reclaim_idle_prefetch(), usize{0});
    AUREA_CHECK_EQ(source.cache().stats().version, busy.version);
    AUREA_CHECK_EQ(source.cache().stats().bytes, busy.bytes);
    raw->release.store(true); // unblock before any test exit/cleanup
    AUREA_CHECK(source.wait_for(raw->inner.pts_of(3), 1000));
    // A separate invalidation precedes the prune. Acknowledging it would
    // accidentally coalesce the next request and swallow legitimate work.
    const i64 protectedTime = raw->inner.pts_of(1);
    source.cache().set_required_times(&protectedTime, 1, 1);
    usize freed = 0;
    wait_until([&] { freed += source.reclaim_idle_prefetch(); return freed > 0; }, 1000);
    AUREA_CHECK(freed > 0); AUREA_CHECK(source.cache().contains(0, 1));
    AUREA_CHECK(source.cache().contains(protectedTime, 1));
    const auto before = source.stats().requests; source.request(request);
    AUREA_CHECK_EQ(source.stats().requests, before + 1);
    source.stop();
}
