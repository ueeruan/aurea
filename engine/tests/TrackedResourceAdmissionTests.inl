// Included by test_render.cpp; exercises the real pool without a device GPU.
#include "aurea/memory/MemoryManager.hpp"

namespace {
u64 live_mock_texture_bytes(const MockBackend& backend) noexcept {
    u64 bytes = 0;
    for (usize i = 0; i < backend.textures.size(); ++i)
        if (backend.textureAlive[i]) bytes += backend.textures[i].estimated_bytes();
    return bytes;
}
u64 known_cache_usage(void* context) noexcept {
    return static_cast<MemoryManager*>(context)->total_used();
}
}

AUREA_TEST(TrackedResourceAdmission, FullResolutionShadowBorrowsUnusedShareWithoutIncreasingTotalBudget) {
    constexpr u64 budget = 322174976; // measured device policy; original pool received half
    TextureDesc shadow = rt(4096, 4096); shadow.format = SurfaceFormat::Depth32F;
    shadow.debugName = "3d-sombra";
    const auto effect = rt(2048, 2048);
    MemoryManager known; known.commit(MemoryClass::DecodedFrames, 40ull << 20);
    for (bool enabled : {false, true}) {
        MockBackend backend; TransientTexturePool pool;
        backend.queryMemoryStats = [&] {
            GpuMemoryStats stats;
            stats.usedBytes = stats.reservedBytes = live_mock_texture_bytes(backend) + (24ull << 20);
            return stats;
        };
        pool.set_allocation_limit(enabled ? budget : budget / 2);
        if (enabled) pool.set_tracked_resource_budget(budget, known_cache_usage, &known);
        pool.begin_frame(backend, 1);
        TextureHandle inputs[4];
        for (auto& handle : inputs) { handle = pool.acquire(effect); AUREA_CHECK(handle.valid()); }
        const auto fullShadow = pool.acquire(shadow);
        AUREA_CHECK_EQ(fullShadow.valid(), enabled);
        if (enabled) {
            const auto desc = backend.texture_desc(fullShadow);
            AUREA_CHECK_EQ(desc.width, 4096u); AUREA_CHECK_EQ(desc.height, 4096u);
            AUREA_CHECK(desc.format == SurfaceFormat::Depth32F);
            AUREA_CHECK_EQ(desc.estimated_bytes(), 64ull << 20);
            AUREA_CHECK(backend.memory_stats().usedBytes + known.total_used() <= budget);
            // A second full shadow actually exceeds tracked headroom; no new
            // allocation is permitted and no lower-resolution substitute appears.
            const auto created = backend.texturesCreated;
            AUREA_CHECK(!pool.acquire(shadow).valid());
            AUREA_CHECK_EQ(backend.texturesCreated, created);
            pool.release(fullShadow);
        } else AUREA_CHECK_EQ(backend.texturesCreated, 4u);
        for (auto handle : inputs) pool.release(handle);
        pool.end_frame(); pool.clear();
    }
}

AUREA_TEST(TrackedResourceAdmission, CacheGrowthAndReducedEnvelopeAreCheckedOnEveryAcquisition) {
    MockBackend backend; TransientTexturePool pool; MemoryManager known;
    const auto image = rt(256, 256); const u64 bytes = image.estimated_bytes();
    backend.queryMemoryStats = [&] {
        GpuMemoryStats stats; stats.usedBytes = stats.reservedBytes = live_mock_texture_bytes(backend);
        return stats;
    };
    pool.set_allocation_limit(bytes * 8);
    pool.set_tracked_resource_budget(bytes * 3, known_cache_usage, &known);
    pool.begin_frame(backend, 1);
    const auto first = pool.acquire(image); AUREA_CHECK(first.valid());
    known.commit(MemoryClass::DecodedFrames, static_cast<usize>(bytes * 2));
    const auto created = backend.texturesCreated;
    AUREA_CHECK(!pool.acquire(image).valid()); AUREA_CHECK_EQ(backend.texturesCreated, created);
    pool.release(first);
    // Reuse allocates nothing, and exactly fits the existing envelope.
    AUREA_CHECK_EQ(pool.acquire(image), first); AUREA_CHECK_EQ(backend.texturesCreated, created);
    pool.release(first);
    pool.set_tracked_resource_budget(bytes * 2, known_cache_usage, &known);
    AUREA_CHECK(!pool.acquire(image).valid()); AUREA_CHECK_EQ(backend.texturesCreated, created);
    known.free(MemoryClass::DecodedFrames, static_cast<usize>(bytes));
    AUREA_CHECK_EQ(pool.acquire(image), first);
    pool.release(first); pool.end_frame(); pool.clear();
}

AUREA_TEST(TrackedResourceAdmission, ExistingReservedBlockCanHoldFullResolutionShadows) {
    constexpr u64 budget = 322174976;
    MockBackend backend; TransientTexturePool pool; MemoryManager known;
    known.commit(MemoryClass::DecodedFrames, 40ull << 20);
    backend.queryMemoryStats = [&] {
        GpuMemoryStats stats; stats.usedBytes = live_mock_texture_bytes(backend);
        // Vulkan may already own these blocks while individual images occupy
        // only a part. Their free space must not be charged a second time.
        stats.reservedBytes = 256ull << 20;
        return stats;
    };
    pool.set_allocation_limit(budget);
    pool.set_tracked_resource_budget(budget, known_cache_usage, &known);
    pool.begin_frame(backend, 1);
    TextureDesc shadow = rt(4096, 4096); shadow.format = SurfaceFormat::Depth32F;
    TextureHandle handles[4];
    for (auto& handle : handles) { handle = pool.acquire(shadow); AUREA_CHECK(handle.valid()); }
    AUREA_CHECK_EQ(pool.stats().bytes, 256ull << 20);
    AUREA_CHECK(backend.memory_stats().reservedBytes + known.total_used() <= budget);
    const auto created = backend.texturesCreated;
    AUREA_CHECK(!pool.acquire(shadow).valid()); AUREA_CHECK_EQ(backend.texturesCreated, created);
    for (auto handle : handles) pool.release(handle);
    pool.end_frame(); pool.clear();
}

AUREA_TEST(TrackedResourceAdmission, DriverGranuleIsRecheckedBeforePublishingTheNewTexture) {
    MockBackend backend; TransientTexturePool pool;
    backend.queryMemoryStats = [&] {
        GpuMemoryStats stats;
        stats.usedBytes = live_mock_texture_bytes(backend);
        // Models a fresh driver/Vulkan allocation block larger than the image.
        stats.reservedBytes = backend.texturesCreated ? 128ull << 20 : 0;
        return stats;
    };
    pool.set_allocation_limit(64ull << 20); pool.set_tracked_resource_budget(64ull << 20);
    pool.begin_frame(backend, 1);
    AUREA_CHECK(!pool.acquire(rt(256, 256)).valid());
    AUREA_CHECK_EQ(backend.texturesCreated, 1u); AUREA_CHECK_EQ(backend.texturesDestroyed, 1u);
    AUREA_CHECK_EQ(pool.stats().alive, 0u); AUREA_CHECK_EQ(pool.stats().bytes, 0u);
    // Retirement may leave a physical allocation reserved. Do not spend it
    // again merely because the rejected logical handle was removed.
    AUREA_CHECK(!pool.acquire(rt(256, 256)).valid());
    AUREA_CHECK_EQ(backend.texturesCreated, 1u);
    pool.end_frame(); pool.clear();
}

AUREA_TEST(TrackedResourceAdmission, PendingGpuRetirementDoesNotCreateFalseHeadroom) {
    MockBackend backend; TransientTexturePool pool;
    struct Pending { void (*done)(void*); void* context; } pending{};
    backend.beforeDeferUntilGpuDone = [&](void (*done)(void*), void* context) { pending = {done, context}; };
    backend.queryMemoryStats = [&] {
        GpuMemoryStats stats; stats.usedBytes = stats.reservedBytes = live_mock_texture_bytes(backend);
        return stats;
    };
    const auto image = rt(256, 256); const u64 bytes = image.estimated_bytes();
    pool.set_allocation_limit(bytes); pool.set_tracked_resource_budget(bytes);
    pool.begin_frame(backend, 1); const auto inFlight = pool.acquire(image); AUREA_CHECK(inFlight.valid());
    pool.retire(inFlight, 7);
    AUREA_CHECK_EQ(pool.stats().bytes, 0u); AUREA_CHECK_EQ(backend.texturesDestroyed, 0u);
    AUREA_CHECK(pending.done != nullptr);
    AUREA_CHECK(!pool.acquire(image).valid()); AUREA_CHECK_EQ(backend.texturesCreated, 1u);
    if (pending.done) pending.done(pending.context);
    pending = {};
    const auto next = pool.acquire(image); AUREA_CHECK(next.valid()); AUREA_CHECK(next != inFlight);
    AUREA_CHECK_EQ(backend.texturesCreated, 2u);
    pool.release(next); pool.end_frame(); pool.clear();
}

AUREA_TEST(TrackedResourceAdmission, UnknownBackendCounterUsesPoolFloorAndOverflowCannotSpendHeadroom) {
    MockBackend backend; TransientTexturePool pool;
    auto image = rt(4, 1); image.format = SurfaceFormat::R8;
    pool.set_allocation_limit(~u64{0}); pool.set_tracked_resource_budget(4);
    pool.begin_frame(backend, 1);
    const auto first = pool.acquire(image); AUREA_CHECK(first.valid());
    AUREA_CHECK(!pool.acquire(image).valid()); AUREA_CHECK_EQ(backend.texturesCreated, 1u);
    pool.release(first); pool.end_frame(); pool.clear();
    backend.queryMemoryStats = [] {
        GpuMemoryStats stats; stats.usedBytes = ~u64{0} - 3; return stats;
    };
    pool.set_tracked_resource_budget(~u64{0}); pool.begin_frame(backend, 2);
    AUREA_CHECK(!pool.acquire(image).valid()); AUREA_CHECK_EQ(backend.texturesCreated, 1u);
    // Opting out preserves the legacy policy even when optional counters are
    // unavailable/unusable; the feature does not alter other platform defaults.
    pool.set_tracked_resource_budget(0);
    const auto legacy = pool.acquire(image); AUREA_CHECK(legacy.valid());
    pool.release(legacy); pool.end_frame(); pool.clear();
}
