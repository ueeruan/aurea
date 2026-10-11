namespace {
struct ExclusivePruneAudit {
    MemoryManager& memory;
    u32 destroyed = 0;
    u64 releasedBackingBytes = 0;
    std::array<u64, 32> accountingAtDestruction{};
};

FrameRef owned_prune_frame(ExclusivePruneAudit& audit, i64 time, u32 width = 64, u32 height = 36) {
    struct OwnedFrame final : DecodedFrame {
        ExclusivePruneAudit& audit;
        std::vector<u8> backing;
        explicit OwnedFrame(ExclusivePruneAudit& value) : audit(value) {}
        bool owns_cpu_backing() const noexcept override { return true; }
        ~OwnedFrame() override {
            const u64 bytes = backing.size();
            std::vector<u8>{}.swap(backing); // release real CPU backing before audit/credit
            audit.releasedBackingBytes += bytes;
            audit.accountingAtDestruction[audit.destroyed++] = audit.memory.used(MemoryClass::DecodedFrames);
        }
    };
    auto* frame = new OwnedFrame(audit);
    frame->ptsUs = time; frame->durationUs = kFrame;
    frame->width = frame->visibleWidth = width; frame->height = frame->visibleHeight = height;
    frame->format = PixelFormat::NV12;
    frame->backing.assign(static_cast<usize>(frame->approx_bytes()), static_cast<u8>(time / kFrame));
    frame->planeCount = 2; frame->planes[0] = frame->backing.data(); frame->strides[0] = width;
    frame->planes[1] = frame->backing.data() + static_cast<usize>(width) * height;
    frame->strides[1] = ((width + 1) / 2) * 2;
    return FrameRef::adopt(frame);
}
} // namespace

AUREA_TEST(DecodedFrameCache, ExclusivePruneReturnsOnlyDestroyedOptionalFullHdBacking) {
    MemoryManager memory; ExclusivePruneAudit audit{memory};
    std::array<DecodedFrameCache, 4> caches;
    std::array<FrameRef, 4> exactFrames;
    const u64 bytes = 1920ull * 1080 * 3 / 2;
    memory.set_budget(MemoryClass::DecodedFrames, 11 * bytes);
    const i64 required = 0;
    for (u32 i = 0; i < caches.size(); ++i) {
        caches[i].attach(&memory); caches[i].configure({3, 3 * bytes});
        caches[i].set_required_times(&required, 1, kFrame / 2);
        for (u32 f = 0; f < (i == 3 ? 2u : 3u); ++f)
            AUREA_CHECK(caches[i].insert(owned_prune_frame(audit, f * kFrame, 1920, 1080)));
        bool exact = false; exactFrames[i] = caches[i].find(0, kFrame / 2, &exact);
        AUREA_CHECK(exact && exactFrames[i]);
    }
    AUREA_CHECK_EQ(memory.used(MemoryClass::DecodedFrames), 34'214'400u);
    u64 freed = 0;
    for (auto& cache : caches) freed += cache.reclaim_unused();
    AUREA_CHECK_EQ(freed, 21'772'800u); AUREA_CHECK_EQ(audit.destroyed, 7u);
    AUREA_CHECK_EQ(audit.releasedBackingBytes, freed);
    AUREA_CHECK_EQ(memory.used(MemoryClass::DecodedFrames), 4 * bytes);
    for (u32 i = 0; i < audit.destroyed; ++i)
        AUREA_CHECK_EQ(audit.accountingAtDestruction[i], (11 - i) * bytes);
    for (u32 i = 0; i < caches.size(); ++i) {
        AUREA_CHECK_EQ(caches[i].stats().frames, 1u);
        AUREA_CHECK_EQ(exactFrames[i]->reference_count(), 2u);
        AUREA_CHECK_EQ(exactFrames[i]->width, 1920u); AUREA_CHECK_EQ(exactFrames[i]->height, 1080u);
        AUREA_CHECK_EQ(exactFrames[i]->planes[0][0], 0u);
        AUREA_CHECK_EQ(caches[i].reclaim_unused(), usize{0});
    }
}

AUREA_TEST(DecodedFrameCache, ExclusivePruneProtectsVfrTemporalAndExternalLeases) {
    MemoryManager memory; ExclusivePruneAudit audit{memory}; DecodedFrameCache cache;
    cache.attach(&memory); cache.configure({6, 1u << 20});
    const u64 bytes = 64u * 36u * 3u / 2u;
    const i64 required[] = {100, 3 * kFrame + 100}; // within presentation intervals
    cache.set_required_times(required, 2, 0);
    for (u32 i = 0; i < 6; ++i) AUREA_CHECK(cache.insert(owned_prune_frame(audit, i * kFrame)));
    bool exact = false;
    FrameRef snapshot = cache.find(kFrame, kFrame / 2, &exact); AUREA_CHECK(exact);
    FrameRef gpu = cache.find(4 * kFrame, kFrame / 2, &exact); AUREA_CHECK(exact);
    const u64 snapshotId = snapshot->content_id(), gpuId = gpu->content_id();
    const u32 version = cache.stats().version;
    AUREA_CHECK_EQ(cache.reclaim_unused(), 2 * bytes);
    AUREA_CHECK_EQ(audit.destroyed, 2u); AUREA_CHECK_EQ(cache.stats().frames, 4u);
    AUREA_CHECK_EQ(memory.used(MemoryClass::DecodedFrames), 4 * bytes);
    AUREA_CHECK(cache.stats().version > version);
    AUREA_CHECK(cache.contains(required[0], 0)); AUREA_CHECK(cache.contains(required[1], 0));
    AUREA_CHECK_EQ(snapshot->content_id(), snapshotId); AUREA_CHECK_EQ(gpu->content_id(), gpuId);
    AUREA_CHECK_EQ(snapshot->planes[0][0], 1u); AUREA_CHECK_EQ(gpu->planes[0][0], 4u);
    AUREA_CHECK_EQ(cache.reclaim_unused(), usize{0});
    snapshot.reset(); gpu.reset();
    AUREA_CHECK_EQ(cache.reclaim_unused(), 2 * bytes); AUREA_CHECK_EQ(cache.stats().frames, 2u);
    cache.set_required_times(nullptr, 0, 0);
    AUREA_CHECK_EQ(cache.reclaim_unused(), bytes); AUREA_CHECK_EQ(cache.stats().frames, 1u);
    AUREA_CHECK_EQ(memory.used(MemoryClass::DecodedFrames), bytes);
    AUREA_CHECK_EQ(cache.reclaim_unused(), usize{0});
}

AUREA_TEST(DecodedFrameCache, ExclusivePruneNeverCreditsSeparatelyRetainedNativeBacking) {
    MemoryManager memory; ExclusivePruneAudit audit{memory}; DecodedFrameCache cache;
    cache.attach(&memory); cache.configure({3, 1u << 20});
    int nativeBuffer = 0;
    for (u32 i = 0; i < 3; ++i) {
        FrameRef native = owned_prune_frame(audit, i * kFrame);
        native->hardwareBuffer = &nativeBuffer;
        AUREA_CHECK(cache.insert(std::move(native)));
    }
    const u64 before = memory.used(MemoryClass::DecodedFrames);
    const u32 version = cache.stats().version;
    AUREA_CHECK_EQ(cache.reclaim_unused(), usize{0});
    AUREA_CHECK_EQ(audit.destroyed, 0u); AUREA_CHECK_EQ(audit.releasedBackingBytes, 0u);
    AUREA_CHECK_EQ(cache.stats().frames, 3u); AUREA_CHECK_EQ(cache.stats().version, version);
    AUREA_CHECK_EQ(memory.used(MemoryClass::DecodedFrames), before);
}

AUREA_TEST(DecodedFrameCache, ExclusivePruneRequiresAnExplicitOwnedCpuProvider) {
    MemoryManager memory; u32 destroyed = 0;
    struct BorrowedFrame final : DecodedFrame {
        u32& destroyed;
        explicit BorrowedFrame(u32& count) : destroyed(count) {}
        ~BorrowedFrame() override { ++destroyed; }
    };
    std::vector<u8> readerBacking(64u * 36u * 3u / 2u, 128);
    DecodedFrameCache cache; cache.attach(&memory); cache.configure({3, 1u << 20});
    for (u32 i = 0; i < 3; ++i) {
        auto* borrowed = new BorrowedFrame(destroyed);
        borrowed->width = 64; borrowed->height = 36; borrowed->format = PixelFormat::NV12;
        borrowed->ptsUs = i * kFrame; borrowed->planeCount = 2;
        borrowed->planes[0] = readerBacking.data(); borrowed->planes[1] = readerBacking.data() + 64u * 36u;
        borrowed->strides[0] = borrowed->strides[1] = 64;
        AUREA_CHECK(!borrowed->owns_cpu_backing()); AUREA_CHECK_EQ(borrowed->reference_count(), 1u);
        AUREA_CHECK(borrowed->hardwareBuffer == nullptr);
        AUREA_CHECK(cache.insert(FrameRef::adopt(borrowed)));
    }
    const u64 before = memory.used(MemoryClass::DecodedFrames);
    const u32 version = cache.stats().version;
    AUREA_CHECK_EQ(cache.reclaim_unused(), usize{0}); AUREA_CHECK_EQ(destroyed, 0u);
    AUREA_CHECK_EQ(cache.stats().frames, 3u); AUREA_CHECK_EQ(cache.stats().version, version);
    AUREA_CHECK_EQ(memory.used(MemoryClass::DecodedFrames), before);
    AUREA_CHECK_EQ(readerBacking[0], 128u);
}
