#include "SceneTargetFixture.hpp"

AUREA_TEST(SceneTargetsGpu, ExactPhasesPreserveRawHdrAlphaAndDistinctNestedSceneZero) {
    AUREA_REQUIRE_GPU();
    auto& g = gpu();
    struct Release { Renderer& renderer; ~Release() {
        renderer.set_transient_allocation_limit(~u64{0}); renderer.release_project_resources();
    } } release{g.renderer};
    auto render = [&](bool staged, bool recoverPressure = false) {
        g.renderer.release_project_resources();
        auto snapshot = test_fixtures::scene_target::pixels();
        // Both sides resolve cold environments before parity is measured.
        // The immutable scene, dimensions and post processing stay identical.
        auto ready = [&](auto&& self, const FrameSnapshot& frame) -> void {
            for (const auto& child : frame.nested) if (child) self(self, *child);
            for (const auto& scene : frame.scenes) g.renderer.scene_renderer().finish_environment(scene.environment);
        };
        ready(ready, snapshot);
        RenderSettings settings; settings.finalQuality = true; settings.dither = false; settings.stageUnblurredScenes = staged;
        OffscreenTarget target{g.target(214, 120), 214, 120}; FrameStats stats; RenderTimings timings;
        if (recoverPressure) {
            // Shadow targets can alias across independent scenes. A single
            // 4096-square shadow already occupies 64 MiB; its live HDR work
            // makes this fixed 64 MiB envelope fail after partial allocation.
            // The source scene, target, shaders and all quality settings stay
            // identical; only the fault is removed before the normal retry.
            g.renderer.set_transient_allocation_limit(64ull << 20);
            const Status failed = g.renderer.render(snapshot, settings, &target, stats, timings);
            const auto faultPool = g.renderer.pool_stats();
            const auto faultScene = g.renderer.scene_stats();
            std::printf("\n    scene pressure fault: limit=%llu status=%u pool=%llu inUse=%u draws=%u shadowDraws=%u shadowMap=%u\n",
                static_cast<unsigned long long>(64ull << 20), failed.raw(),
                static_cast<unsigned long long>(faultPool.bytes), faultPool.inUse,
                faultScene.drawCalls, faultScene.shadowDrawCalls, faultScene.shadowMapSize);
            AUREA_CHECK_EQ(failed.code(), Errc::OutOfDeviceMemory);
            AUREA_CHECK(g.renderer.pool_stats().bytes > 0);
            AUREA_CHECK(g.renderer.reclaim_failed_transients(2'000'000'000).ok());
            std::printf("\n    scene pressure completed reclaim: pool=%llu\n",
                static_cast<unsigned long long>(g.renderer.pool_stats().bytes));
            AUREA_CHECK_EQ(g.renderer.pool_stats().bytes, 0ull);
            g.renderer.set_transient_allocation_limit(~u64{0});
            stats = {}; timings = {};
        }
        const Status status = g.renderer.render(snapshot, settings, &target, stats, timings);
        if (!status.ok()) std::printf("\n    scene targets staged=%u status=%u detail=%.*s\n", staged,
            status.raw(), static_cast<int>(status.detail().size()), status.detail().data());
        AUREA_CHECK(status.ok()); AUREA_CHECK(!g.renderer.take_incomplete());
        AUREA_CHECK_EQ(timings.sceneExposureBatches, staged ? 3u : 0u);
        g.backend.wait_idle();
        std::vector<u16> pixels(214u * 120u * 4u);
        if (status.ok()) AUREA_CHECK(g.backend.read_texture(target.texture, pixels.data(), 214u * 8u).ok());
        else pixels.clear();
        return pixels;
    };
    const auto old = render(false); const auto staged = render(true);
    AUREA_CHECK_EQ(old.size(), usize{214u * 120u * 4u}); AUREA_CHECK_EQ(old.size(), staged.size());
    if (old.empty() || old.size() != staged.size()) return;
    f32 maximum = 0; f64 squared = 0, energy = 0;
    for (usize i = 0; i < old.size(); ++i) {
        const f32 a = half_to_float(old[i]), b = half_to_float(staged[i]);
        AUREA_CHECK(std::isfinite(a)); AUREA_CHECK(std::isfinite(b));
        const f32 difference = std::fabs(a - b); maximum = std::max(maximum, difference); squared += difference * difference;
        if (i % 4 != 3) energy += b;
    }
    std::printf("\n    raw staged scene parity: max=%g rms=%g energy=%g\n", maximum, std::sqrt(squared / old.size()), energy);
    AUREA_CHECK(energy > 100); AUREA_CHECK_EQ(maximum, 0.f);
    const auto recovered = render(false, true);
    AUREA_CHECK_EQ(recovered.size(), old.size());
    if (recovered.size() == old.size()) AUREA_CHECK(recovered == old);
}
