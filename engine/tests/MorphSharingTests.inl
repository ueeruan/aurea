#include "MorphSharingFixture.hpp"

AUREA_TEST(MorphSharing, RepeatedPosesShareWorkWhileDistinctWeightsMaterialsAndSkinsStayIndependent) {
    RenderFixture fixture; fixture.backend.mapBuffers = true;
    auto& renderer = fixture.renderer.scene_renderer();
    renderer.set_environment_quality({16, 0, 1}, {16, 0, 1});
    renderer.set_quality(256, 0, 1, false); renderer.set_post_quality(1, false, 2, 0);
    TransientTexturePool pool; u64 number = 1;
    for (const u32 mode : {0u, 1u, 2u, 3u, 4u, 5u, 6u, 0u}) {
        const std::shared_ptr<const scene3d::SceneAsset> source = test_fixtures::morph_sharing::asset(mode);
        auto frame = test_fixtures::morph_sharing::frame(source, mode);
        for (auto& instance : frame.instances) instance.assetKey += number << 20;
        FrameGraph graph; Arena arena; FrameBegin begin;
        AUREA_CHECK(fixture.backend.begin_offscreen_frame(begin).ok());
        FGTexture output; AUREA_CHECK(renderer.build(graph, arena, frame, 320, 180, number, output));
        graph.set_output(output, ResourceState::ShaderRead); pool.begin_frame(fixture.backend, number++);
        AUREA_CHECK(graph.compile(pool).ok()); graph.execute(*begin.commands, false);
        const auto actual = renderer.stats();
        const u32 uploads = 2 * test_fixtures::morph_sharing::unique_weights(mode);
        AUREA_CHECK_EQ(actual.morphUploads, uploads);
        AUREA_CHECK_EQ(actual.morphVertices, u64{uploads} * 4);
        AUREA_CHECK_EQ(actual.morphUploadBytes, u64{uploads} * 256);
        AUREA_CHECK_EQ(actual.visiblePrimitives, mode == 5 ? 48u : 24u);
        AUREA_CHECK_EQ(actual.drawCalls, test_fixtures::morph_sharing::color_draws(mode));
        AUREA_CHECK_EQ(actual.shadowDrawCalls, test_fixtures::morph_sharing::shadow_draws(mode));
        graph.release(pool); pool.end_frame(); AUREA_CHECK(fixture.backend.end_frame().ok());
    }
    pool.clear();
}
