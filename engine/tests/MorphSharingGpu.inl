#if defined(AUREA_TEST_VULKAN)
#include "MorphSharingFixture.hpp"

AUREA_TEST(MorphSharingGpu, IdenticalDeformationMatchesIndependentOriginsIncludingFloorAndSkin) {
    AUREA_REQUIRE_GPU();
    Scene3DRig rig(320, 180);
    auto& backend = *rig.e.gpu(); auto& renderer = rig.e.renderer().scene_renderer();
    renderer.set_environment_quality({16, 0, 1}, {16, 0, 1});
    renderer.set_environment_wait(true); renderer.set_quality(256, 0, 1, false);
    renderer.set_post_quality(1, false, 2, 0);
    TransientTexturePool pool; u64 number = 1;
    auto render = [&](scene3d::SceneFrame frame, bool instancing) {
        // Each fixture is an independent source model; do not give a previous
        // model's immutable GPU cache entry to a new source with a reused ID.
        for (auto& instance : frame.instances) instance.assetKey += number << 20;
        renderer.set_instancing(instancing); renderer.reset_incomplete();
        renderer.finish_environment(frame.environment);
        FrameGraph graph; Arena arena; FrameBegin begin; FloatImage image;
        const Status begun = backend.begin_offscreen_frame(begin); AUREA_CHECK(begun.ok());
        if (!begun.ok()) return image;
        FGTexture output; const bool built = renderer.build(graph, arena, frame, 320, 180, number, output);
        AUREA_CHECK(built); if (!built) { (void)backend.end_frame(); return image; }
        graph.set_output(output, ResourceState::ShaderRead); pool.begin_frame(backend, number++);
        const Status compiled = graph.compile(pool); AUREA_CHECK(compiled.ok());
        if (!compiled.ok()) { (void)backend.end_frame(); return image; }
        graph.execute(*begin.commands, false);
        AUREA_CHECK(backend.end_frame().ok()); backend.wait_idle();
        AUREA_CHECK(!renderer.incomplete());
        std::vector<u16> half(320u * 180u * 4u);
        const Status read = backend.read_texture(graph.physical(output), half.data(), 320u * 8u);
        AUREA_CHECK(read.ok());
        if (read.ok()) {
            image.width = 320; image.height = 180; image.px.resize(half.size());
            for (usize i = 0; i < half.size(); ++i) image.px[i] = half_to_float(half[i]);
        }
        graph.release(pool); pool.end_frame();
        return image;
    };
    // Equal weights return after distinct animated weights, proving the cache
    // belongs to this build and cannot reuse a previous frame's deformation.
    for (const bool floor : {false, true}) for (const u32 mode : {0u, 1u, 2u, 3u, 4u, 5u, 6u, 0u}) {
        const std::shared_ptr<const scene3d::SceneAsset> source = test_fixtures::morph_sharing::asset(mode);
        const auto reference = render(test_fixtures::morph_sharing::frame(source, mode, true, floor), false);
        const auto baseline = renderer.stats();
        const auto shared = render(test_fixtures::morph_sharing::frame(source, mode, false, floor), true);
        const auto actual = renderer.stats();
        AUREA_CHECK_EQ(reference.px.size(), usize{320u * 180u * 4u});
        AUREA_CHECK_EQ(reference.px.size(), shared.px.size());
        if (reference.px.size() != shared.px.size() || shared.px.empty()) continue;
        f32 maximum = 0; f64 energy = 0; bool finite = true;
        for (usize i = 0; i < shared.px.size(); ++i) {
            finite &= std::isfinite(reference.px[i]) && std::isfinite(shared.px[i]);
            maximum = std::max(maximum, std::fabs(reference.px[i] - shared.px[i]));
            if (i % 4 != 3) energy += shared.px[i];
        }
        std::printf("\n    morph mode=%u floor=%u: error=%g energy=%.3f uploads=%u/%u bytes=%llu/%llu color=%u shadow=%u",
            mode, floor, maximum, energy, actual.morphUploads, baseline.morphUploads,
            static_cast<unsigned long long>(actual.morphUploadBytes), static_cast<unsigned long long>(baseline.morphUploadBytes),
            actual.drawCalls, actual.shadowDrawCalls);
        AUREA_CHECK(finite); AUREA_CHECK(energy > 100); AUREA_CHECK(maximum <= .001f);
        AUREA_CHECK_EQ(baseline.morphUploads, 24u);
        const u32 uploads = 2 * test_fixtures::morph_sharing::unique_weights(mode);
        AUREA_CHECK_EQ(actual.morphUploads, uploads);
        AUREA_CHECK_EQ(actual.morphVertices, u64{uploads} * 4);
        AUREA_CHECK_EQ(actual.morphUploadBytes * 24, baseline.morphUploadBytes * uploads);
        AUREA_CHECK_EQ(actual.visiblePrimitives, mode == 5 ? 48u : 24u);
        AUREA_CHECK_EQ(actual.drawCalls, test_fixtures::morph_sharing::color_draws(mode));
        AUREA_CHECK_EQ(actual.shadowDrawCalls, test_fixtures::morph_sharing::shadow_draws(mode));
    }
    renderer.set_instancing(true); pool.clear();
}
#endif
