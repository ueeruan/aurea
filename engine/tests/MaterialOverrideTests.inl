#include "aurea/scene3d/MaterialOverrideKey.hpp"
#include "MaterialOverrideFixture.hpp"

AUREA_TEST(MaterialOverrides, ExactKeyKeepsOriginFlagsAndEveryFloatBit) {
    using namespace aurea::scene3d;
    Material first, second; auto values = MaterialOverrideValues::from(first);
    MaterialOverrideKey reference, same, changed;
    AUREA_CHECK(material_override_key(&first, values, reference));
    AUREA_CHECK(material_override_key(&first, values, same));
    AUREA_CHECK(reference == same);
    AUREA_CHECK_EQ(MaterialOverrideKeyHash{}(reference), MaterialOverrideKeyHash{}(same));
    AUREA_CHECK(material_override_key(&second, values, changed)); AUREA_CHECK(!(reference == changed));
    for (usize i = 0; i < values.factors.size(); ++i) {
        auto next = values; next.factors[i] = std::nextafter(next.factors[i], 2.f);
        AUREA_CHECK(material_override_key(&first, next, changed)); AUREA_CHECK(!(reference == changed));
    }
    auto next = values; next.alphaMode = AlphaMode::Mask;
    AUREA_CHECK(material_override_key(&first, next, changed)); AUREA_CHECK(!(reference == changed));
    next = values; next.doubleSided = !values.doubleSided;
    AUREA_CHECK(material_override_key(&first, next, changed)); AUREA_CHECK(!(reference == changed));
    next = values; next.unlit = !values.unlit;
    AUREA_CHECK(material_override_key(&first, next, changed)); AUREA_CHECK(!(reference == changed));
    next = values; next.factors[0] = 0;
    AUREA_CHECK(material_override_key(&first, next, reference)); next.factors[0] = -0.f;
    AUREA_CHECK(material_override_key(&first, next, changed)); AUREA_CHECK(!(reference == changed));
}

AUREA_TEST(MaterialOverrides, NonFiniteValuesAreNeverCacheKeysAndApplyPreservesOtherFields) {
    using namespace aurea::scene3d;
    Material material; material.name = "origin"; material.baseColorTex.image = 3;
    material.alphaCutoff = .25f; material.clearcoat = .7f; material.emissiveStrength = 4;
    auto values = MaterialOverrideValues::from(material); MaterialOverrideKey key;
    for (usize i = 0; i < values.factors.size(); ++i) for (const f32 invalid : {
        std::numeric_limits<f32>::quiet_NaN(), std::numeric_limits<f32>::infinity(), -std::numeric_limits<f32>::infinity()}) {
        auto next = values; next.factors[i] = invalid;
        AUREA_CHECK(!material_override_key(&material, next, key));
    }
    AUREA_CHECK(!material_override_key(nullptr, values, key));
    values.factors = {.2f, .3f, .4f, .5f, .6f, .7f}; values.alphaMode = AlphaMode::Blend;
    values.doubleSided = true; values.unlit = true; values.apply(material);
    AUREA_CHECK_EQ(material.baseColor.w, .5f); AUREA_CHECK_EQ(material.metallic, .6f);
    AUREA_CHECK_EQ(material.roughness, .7f); AUREA_CHECK(material.alphaMode == AlphaMode::Blend);
    AUREA_CHECK(material.doubleSided && material.unlit); AUREA_CHECK_EQ(material.name, std::string{"origin"});
    AUREA_CHECK_EQ(material.baseColorTex.image, 3); AUREA_CHECK_EQ(material.alphaCutoff, .25f);
    AUREA_CHECK_EQ(material.clearcoat, .7f); AUREA_CHECK_EQ(material.emissiveStrength, 4.f);
}

AUREA_TEST(MaterialOverrides, IdenticalRigidOverridesShareBatchesButVariedAndTransparentValuesStaySeparate) {
    RenderFixture fixture; fixture.backend.mapBuffers = true;
    auto& renderer = fixture.renderer.scene_renderer();
    renderer.set_environment_quality({16, 0, 1}, {16, 0, 1});
    renderer.set_quality(256, 0, 1, false); renderer.set_post_quality(1, false, 2, 0);
    const std::shared_ptr<const scene3d::SceneAsset> source = test_fixtures::material_override::asset();
    TransientTexturePool pool; u64 number = 1;
    // The last equal state follows animated split states: no prior-frame key
    // may retain an old override or leak a material pointer into the next build.
    for (const u32 mode : {0u, 1u, 2u, 3u, 4u, 5u, 6u, 2u}) {
        auto frame = test_fixtures::material_override::frame(source, mode);
        FrameGraph graph; Arena arena; FrameBegin begin;
        AUREA_CHECK(fixture.backend.begin_offscreen_frame(begin).ok());
        FGTexture output; AUREA_CHECK(renderer.build(graph, arena, frame, 320, 180, number, output));
        graph.set_output(output, ResourceState::ShaderRead); pool.begin_frame(fixture.backend, number++);
        AUREA_CHECK(graph.compile(pool).ok()); graph.execute(*begin.commands, false);
        AUREA_CHECK_EQ(renderer.stats().visiblePrimitives, 12u);
        const u32 expected = mode == 3 ? 3 : mode == 4 ? 2 : mode == 5 ? 12 : 1;
        AUREA_CHECK_EQ(renderer.stats().drawCalls, expected);
        AUREA_CHECK_EQ(renderer.stats().instancedDraws, mode == 5 ? 0u : expected);
        if (mode == 6) AUREA_CHECK_EQ(renderer.stats().shadowDrawCalls, 1u);
        graph.release(pool); pool.end_frame(); AUREA_CHECK(fixture.backend.end_frame().ok());
    }
    auto frame = test_fixtures::material_override::frame(source, 2, true);
    FrameGraph graph; Arena arena; FGTexture output;
    AUREA_CHECK(renderer.build(graph, arena, frame, 320, 180, number, output));
    AUREA_CHECK_EQ(renderer.stats().drawCalls, 12u); AUREA_CHECK_EQ(renderer.stats().instancedDraws, 0u);
    pool.clear();
}
