#if defined(AUREA_TEST_VULKAN)
#include "MorphSharingFixture.hpp"
#include "SceneTargetFixture.hpp"

namespace {
struct WarmupPipelineTimes {
    u32 calls = 0; u64 total = 0, maximum = 0;
    static void record(void* context, const PipelineKey& key, u64 ns, bool ok) noexcept {
        if (key.fragment != ShaderId::scene3d_pbr_pbr_frag) return;
        auto& result = *static_cast<WarmupPipelineTimes*>(context);
        ++result.calls; result.total += ns; result.maximum = std::max(result.maximum, ns);
        std::printf("\n    PBR driver create: samples=%u mrt=%u skin=%u blend=%u cull=%u ccw=%u a2c=%u ok=%u %.3f ms",
            key.sampleCount, key.hasColor1, key.mesh == MeshLayout::Skinned, key.blendEnabled,
            static_cast<u32>(key.cull), key.frontFaceCCW, key.alphaToCoverage, ok, ns / 1.e6);
    }
    void attach(ShaderLibrary& library) { library.set_pipeline_creation_probe(&record, this); }
};

FrameSnapshot warmup_scene(const std::shared_ptr<const scene3d::SceneAsset>& source, u32 mode, u32 quality) {
    FrameSnapshot snapshot; snapshot.compWidth = 320; snapshot.compHeight = 180;
    snapshot.background = Vec4{};
    auto scene = test_fixtures::morph_sharing::frame(source, mode);
    scene.post.quality = quality; scene.shadow.mapResolution = 256;
    for (auto& instance : scene.instances)
        instance.assetKey = static_cast<u64>(reinterpret_cast<std::uintptr_t>(instance.asset.get()));
    snapshot.scenes.push_back(std::move(scene));
    RenderLayer layer; layer.source.kind = LayerSource::Kind::Scene3D;
    layer.source.width = 320; layer.source.height = 180;
    snapshot.layers.push_back(std::move(layer)); snapshot.plans.emplace_back();
    return snapshot;
}

bool warmup_render(Gpu& fresh, FrameSnapshot& snapshot, const RenderSettings& settings, u32& pipelines) {
    OffscreenTarget target{fresh.target(320, 180), 320, 180};
    FrameStats stats; RenderTimings timings;
    const auto started = std::chrono::steady_clock::now();
    const Status status = fresh.renderer.render(snapshot, settings, &target, stats, timings);
    AUREA_CHECK(status.ok()); AUREA_CHECK(!fresh.renderer.take_incomplete());
    fresh.backend.wait_idle();
    std::vector<u16> half(320u * 180u * 4u);
    AUREA_CHECK(fresh.backend.read_texture(target.texture, half.data(), 320u * 8u).ok());
    f64 energy = 0; bool finite = true;
    for (usize i = 0; i < half.size(); ++i) {
        const f32 value = half_to_float(half[i]); finite &= std::isfinite(value);
        if (i % 4 != 3) energy += value;
    }
    pipelines = fresh.renderer.shaders().pipeline_count();
    const f64 elapsed = std::chrono::duration<f64, std::milli>(std::chrono::steady_clock::now() - started).count();
    std::printf("\n    3D warmup: %zu scene(s), cache=%u, hot=%u, frame=%.3f ms, energy=%.3f",
        snapshot.scenes.size(), pipelines, fresh.renderer.shaders().compiles_since_mark(), elapsed, energy);
    AUREA_CHECK(finite); AUREA_CHECK(energy > 10);
    return status.ok() && finite;
}
}

AUREA_TEST(SceneWarmupGpu, ExactCurrentSampleProfileWarmsEveryModelVariantBeforeRecording) {
    AUREA_REQUIRE_GPU();
    // Every case starts with a fresh pipeline cache. A previous High-quality
    // capture must not conceal the original Medium/Auto 2x warmup mismatch.
    for (const u32 requested : {1u, 2u, 4u, 8u}) {
        Gpu fresh; AUREA_CHECK(fresh.ok); if (!fresh.ok) return;
        RenderSettings settings; settings.dither = false; settings.gpuTimers = false;
        settings.heavyExplicit = true; settings.heavy = HeavyQuality::full();
        settings.heavy.msaaSamples = requested;
        const u32 quality = static_cast<u32>(requested == 1 ? Scene3DQuality::Low
            : requested == 2 ? Scene3DQuality::Medium : requested == 4 ? Scene3DQuality::High : Scene3DQuality::Ultra);
        settings.finalQuality = requested == 8;
        const u32 actual = fresh.backend.capabilities().msaa_samples(requested);
        auto source = test_fixtures::morph_sharing::asset(0);
        std::vector<std::shared_ptr<const scene3d::SceneAsset>> keepSources{source};
        auto snapshot = warmup_scene(source, 0, quality);
        auto& library = fresh.renderer.shaders(); WarmupPipelineTimes creation; creation.attach(library);
        library.mark_steady_state();
        u32 warmCount = 0;
        AUREA_CHECK(warmup_render(fresh, snapshot, settings, warmCount));
        AUREA_CHECK_EQ(library.compiles_since_mark(), 0u);
        std::printf(" requested=%u actual=%u PBR calls=%u driverTotal=%.3f ms max=%.3f ms",
            requested, actual, creation.calls, creation.total / 1.e6, creation.maximum / 1.e6);
        // Do not warm unrelated sample counts just to make this test pass.
        const auto key = [&](u32 samples) {
            auto result = PipelineKey::graphics(ShaderId::scene3d_pbr_mesh_vert,
                ShaderId::scene3d_pbr_pbr_frag, SurfaceFormat::RGBA16F);
            result.mesh = MeshLayout::Static; result.hasDepth = true;
            result.depthTest = result.depthWrite = true; result.cull = CullMode::Back;
            result.sampleCount = static_cast<u8>(samples); result.hasColor1 = true;
            return result;
        };
        AUREA_CHECK(library.has_pipeline(key(actual)));
        for (u32 other : {2u, 4u, 8u})
            if (other != actual) AUREA_CHECK(!library.has_pipeline(key(other)));
        // Geometry deformation, skin, opacity and lens animation share the
        // already-warmed structural keys. Count the cache too: prewarm itself
        // deliberately does not increment compiles_since_mark().
        for (const u32 mode : {0u, 3u, 6u, 9u}) {
            auto nextSource = test_fixtures::morph_sharing::asset(mode);
            if (mode == 9) for (auto& material : nextSource->materials)
                material.alphaMode = scene3d::AlphaMode::Mask;
            keepSources.push_back(nextSource); // Asset keys cannot be reused for unrelated GPU geometry.
            auto next = warmup_scene(nextSource, mode, quality);
            if (mode == 6) {
                next.scenes[0].floor.mode = 1;
                next.scenes[0].floor.reflectivity = .5f;
                next.scenes[0].floor.contactShadow = .5f;
                next.scenes[0].camera.dof = true;
                next.scenes[0].camera.focusDistance = 200;
                next.scenes[0].camera.fStop = 1.4f;
                next.scenes[0].camera.pixelsPerMeter = 90;
            }
            u32 after = 0;
            AUREA_CHECK(warmup_render(fresh, next, settings, after));
            AUREA_CHECK_EQ(after, warmCount);
            AUREA_CHECK_EQ(library.compiles_since_mark(), 0u);
        }
        library.set_pipeline_creation_probe(nullptr, nullptr);
    }
}

AUREA_TEST(SceneWarmupGpu, NestedQualityAndDepthFallbackAndDirectSkyStayWarm) {
    AUREA_REQUIRE_GPU();
    Gpu fresh; AUREA_CHECK(fresh.ok); if (!fresh.ok) return;
    auto& caps = const_cast<GPUCapabilities&>(fresh.backend.capabilities());
    const bool oldResolve = caps.depthResolveSampleZero;
    struct Restore { GPUCapabilities& caps; bool original; ~Restore() { caps.depthResolveSampleZero = original; } } restore{caps, oldResolve};
    // Force only a LESS capable path. Never advertise unsupported MSAA or
    // execute a fake hardware depth resolve on the Vulkan driver.
    caps.depthResolveSampleZero = false;
    auto snapshot = test_fixtures::scene_target::pixels();
    snapshot.scenes[0].post.quality = static_cast<u32>(Scene3DQuality::High);
    for (usize i = 0; i < snapshot.nested.size(); ++i) {
        auto& scene = snapshot.nested[i]->scenes[0]; scene.shadow.mapResolution = 256;
        scene.post.quality = static_cast<u32>(i ? Scene3DQuality::Low : Scene3DQuality::Medium);
    }
    RenderSettings settings; settings.dither = false; settings.gpuTimers = false;
    auto& library = fresh.renderer.shaders(); WarmupPipelineTimes creation; creation.attach(library);
    library.mark_steady_state();
    u32 count = 0;
    AUREA_CHECK(warmup_render(fresh, snapshot, settings, count));
    AUREA_CHECK_EQ(library.compiles_since_mark(), 0u);
    snapshot.scenes[0].camera.dof = true; snapshot.scenes[0].camera.focusDistance = 200;
    snapshot.scenes[0].camera.pixelsPerMeter = 90; snapshot.scenes[0].camera.fStop = 1.4f;
    u32 after = 0;
    AUREA_CHECK(warmup_render(fresh, snapshot, settings, after));
    AUREA_CHECK_EQ(after, count); AUREA_CHECK_EQ(library.compiles_since_mark(), 0u);
    auto sky = warmup_scene(test_fixtures::morph_sharing::asset(0), 0,
                            static_cast<u32>(Scene3DQuality::High));
    sky.scenes[0].instances.clear(); sky.scenes[0].environment.showBackground = true;
    AUREA_CHECK(warmup_render(fresh, sky, settings, after));
    AUREA_CHECK_EQ(after, count); AUREA_CHECK_EQ(library.compiles_since_mark(), 0u);
    library.set_pipeline_creation_probe(nullptr, nullptr);
}

AUREA_TEST(SceneWarmupGpu, FutureModelIsWarmBeforeItsFirstPlaybackFrame) {
    AUREA_REQUIRE_GPU();
    Scene3DRig rig(320, 180);
    ModelImport source; source.path = write_triangle_gltf(true, 1, .3f, .1f);
    const auto id = rig.e.import_model(source); AUREA_CHECK(id.ok()); if (!id.ok()) return;
    auto* composition = rig.e.project()->timeline().composition(rig.e.project()->timeline().current());
    composition->post_process().quality3d = static_cast<u32>(Scene3DQuality::Medium);
    composition->post_process().bloom = false;
    composition->layer(LayerId::unpack(*id))->start = FrameIndex{30};
    TextureDesc description; description.width = 320; description.height = 180;
    description.format = SurfaceFormat::RGBA16F;
    description.renderTarget = description.sampled = description.transferSrc = true;
    auto texture = rig.e.gpu()->create_texture(description); AUREA_CHECK(texture.ok()); if (!texture.ok()) return;
    OffscreenTarget output{*texture, 320, 180}; FrameStats stats; RenderTimings timings;
    RenderSettings settings; settings.heavyScale = .5f; settings.dither = false; settings.gpuTimers = false;
    FrameSnapshot snapshot;
    rig.e.renderer().prepare(*composition, *rig.e.project(), FrameIndex{0}, nullptr, nullptr, nullptr,
                            settings, 1, 0, DecodeMode::Still, 1, snapshot);
    AUREA_CHECK(snapshot.scenes.empty());
    AUREA_CHECK(rig.e.renderer().render(snapshot, settings, &output, stats, timings).ok());
    auto& library = rig.e.renderer().shaders();
    const u32 before = library.pipeline_count(); library.mark_steady_state();
    rig.e.renderer().prepare(*composition, *rig.e.project(), FrameIndex{31}, nullptr, nullptr, nullptr,
                            settings, 2, 0, DecodeMode::Playback, 1, snapshot);
    AUREA_CHECK_EQ(snapshot.scenes.size(), 1u);
    AUREA_CHECK(rig.e.renderer().render(snapshot, settings, &output, stats, timings).ok());
    AUREA_CHECK(!rig.e.renderer().take_incomplete()); rig.e.gpu()->wait_idle();
    AUREA_CHECK_EQ(library.pipeline_count(), before);
    AUREA_CHECK_EQ(library.compiles_since_mark(), 0u);
    std::vector<u16> pixels(320u * 180u * 4u);
    AUREA_CHECK(rig.e.gpu()->read_texture(*texture, pixels.data(), 320u * 8u).ok());
    f64 energy = 0; for (usize i = 0; i < pixels.size(); ++i) if (i % 4 != 3) energy += half_to_float(pixels[i]);
    std::printf("\n    future 3D model: cache=%u/%u hot=%u energy=%.3f", before,
        library.pipeline_count(), library.compiles_since_mark(), energy);
    AUREA_CHECK(energy > 10);
    rig.e.gpu()->destroy_texture(*texture);
}

AUREA_TEST(SceneWarmupGpu, AutoWarmsOnlyItsTwoAdaptiveProfilesBeforePlayback) {
    AUREA_REQUIRE_GPU();
    Gpu fresh; AUREA_CHECK(fresh.ok); if (!fresh.ok) return;
    auto source = test_fixtures::morph_sharing::asset(0);
    auto snapshot = warmup_scene(source, 0, static_cast<u32>(Scene3DQuality::Auto));
    RenderSettings settings; settings.heavyScale = .5f; settings.dither = false; settings.gpuTimers = false;
    auto& library = fresh.renderer.shaders(); WarmupPipelineTimes creation; creation.attach(library);
    library.mark_steady_state();
    u32 count = 0;
    AUREA_CHECK(warmup_render(fresh, snapshot, settings, count));
    const u32 calls = creation.calls;
    std::printf(" Auto warmup: PBR calls=%u driverTotal=%.3f ms max=%.3f ms", calls,
        creation.total / 1.e6, creation.maximum / 1.e6);
    AUREA_CHECK(calls > 0); AUREA_CHECK_EQ(library.compiles_since_mark(), 0u);
    for (const f32 scale : {1.f, .4f, 1.f, .5f}) {
        settings.heavyScale = scale;
        u32 after = 0; AUREA_CHECK(warmup_render(fresh, snapshot, settings, after));
        AUREA_CHECK_EQ(after, count); AUREA_CHECK_EQ(creation.calls, calls);
        AUREA_CHECK_EQ(library.compiles_since_mark(), 0u);
    }
    auto unrelated = PipelineKey::graphics(ShaderId::scene3d_pbr_mesh_vert,
        ShaderId::scene3d_pbr_pbr_frag, SurfaceFormat::RGBA16F);
    unrelated.mesh = MeshLayout::Static; unrelated.hasDepth = true;
    unrelated.depthTest = unrelated.depthWrite = true; unrelated.cull = CullMode::Back;
    unrelated.sampleCount = 8; unrelated.hasColor1 = true;
    AUREA_CHECK(!library.has_pipeline(unrelated));
    library.set_pipeline_creation_probe(nullptr, nullptr);
}
#endif
