#if defined(AUREA_TEST_VULKAN)
#include "MaterialOverrideFixture.hpp"

AUREA_TEST(MaterialOverridesGpu, SharedExactVariedAnimatedOpacityAndShadowMatchIndependentOrigins) {
    AUREA_REQUIRE_GPU();
    Scene3DRig rig(320, 180);
    auto& backend = *rig.e.gpu(); auto& renderer = rig.e.renderer().scene_renderer();
    renderer.set_environment_quality({16, 0, 1}, {16, 0, 1});
    renderer.set_environment_wait(true); renderer.set_quality(256, 0, 1, false);
    renderer.set_post_quality(1, false, 2, 0);
    TransientTexturePool pool; u64 number = 1;
    const std::shared_ptr<const scene3d::SceneAsset> source = test_fixtures::material_override::asset();
    auto render = [&](scene3d::SceneFrame frame, bool instancing) {
        renderer.set_instancing(instancing); renderer.reset_incomplete();
        // Direct SceneRenderer fixtures must prepare the group environment as
        // Renderer does for exact captures. set_environment_wait controls only
        // per-object overrides; build requests a cold group asynchronously.
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
    FloatImage prior;
    for (const u32 mode : {0u, 1u, 2u, 3u, 4u, 5u, 6u, 2u}) {
        // Independent origins forbid the new cache from sharing any copies.
        // They form a reference even if an incorrect key merged shared draws.
        const auto reference = render(test_fixtures::material_override::frame(source, mode, true), false);
        const auto shared = render(test_fixtures::material_override::frame(source, mode), true);
        AUREA_CHECK_EQ(reference.px.size(), usize{320u * 180u * 4u});
        AUREA_CHECK_EQ(reference.px.size(), shared.px.size());
        if (reference.px.size() != shared.px.size() || shared.px.empty()) continue;
        f32 maximum = 0; f64 squared = 0, energy = 0, changed = 0;
        for (usize i = 0; i < shared.px.size(); ++i) {
            AUREA_CHECK(std::isfinite(reference.px[i])); AUREA_CHECK(std::isfinite(shared.px[i]));
            const f32 difference = std::fabs(reference.px[i] - shared.px[i]);
            maximum = std::max(maximum, difference); squared += difference * difference;
            if (i % 4 != 3) energy += shared.px[i];
            if (prior.px.size() == shared.px.size()) changed += std::fabs(prior.px[i] - shared.px[i]);
        }
        std::printf("\n    material mode=%u: max=%g rms=%g energy=%.3f changed=%.3f colorDraws=%u shadowDraws=%u",
            mode, maximum, std::sqrt(squared / shared.px.size()), energy, changed,
            renderer.stats().drawCalls, renderer.stats().shadowDrawCalls);
        AUREA_CHECK(energy > 100); AUREA_CHECK(maximum <= .001f);
        if (mode > 1) AUREA_CHECK(changed > 10);
        const u32 expected = mode == 3 ? 3 : mode == 4 ? 2 : mode == 5 ? 12 : 1;
        AUREA_CHECK_EQ(renderer.stats().drawCalls, expected);
        if (mode == 6) AUREA_CHECK_EQ(renderer.stats().shadowDrawCalls, 1u);
        prior = shared;
    }
    // Different textures on different source materials must remain different
    // even when the six edited values are bit-identical.
    const std::shared_ptr<const scene3d::SceneAsset> blue = test_fixtures::material_override::asset(true);
    auto sharedFrame = test_fixtures::material_override::frame(source, 2);
    auto referenceFrame = test_fixtures::material_override::frame(source, 2, true);
    for (u32 i = 6; i < 12; ++i) {
        sharedFrame.instances[i].asset = blue; sharedFrame.instances[i].assetKey = 2;
        referenceFrame.instances[i].asset = std::make_shared<scene3d::SceneAsset>(*blue);
        referenceFrame.instances[i].assetKey = 200 + i;
    }
    const auto reference = render(std::move(referenceFrame), false);
    const auto shared = render(std::move(sharedFrame), true);
    AUREA_CHECK_EQ(reference.px.size(), usize{320u * 180u * 4u});
    AUREA_CHECK_EQ(reference.px.size(), shared.px.size());
    f32 maximum = 0;
    if (reference.px.size() == shared.px.size()) for (usize i = 0; i < shared.px.size(); ++i) {
        AUREA_CHECK(std::isfinite(reference.px[i]) && std::isfinite(shared.px[i]));
        maximum = std::max(maximum, std::fabs(reference.px[i] - shared.px[i]));
    }
    AUREA_CHECK(maximum <= .001f); AUREA_CHECK_EQ(renderer.stats().drawCalls, 2u);
    renderer.set_instancing(true); pool.clear();
}
#endif
