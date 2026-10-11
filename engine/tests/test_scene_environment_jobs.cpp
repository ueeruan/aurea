#include "TestFramework.hpp"
#include "MockBackend.hpp"

#include "aurea/scene3d/EnvironmentJob.hpp"
#include "aurea/scene3d/SceneRenderer.hpp"
#include "aurea/scene3d/Shape3D.hpp"

#include <chrono>
#include <future>
#include <cstring>

using namespace aurea;
using namespace aurea::scene3d;

namespace aurea::scene3d {
struct SceneEnvironmentJobTestAccess {
    static void attach(SceneRenderer& renderer, GPUBackend& gpu) { renderer.gpu_ = &gpu; }
    static void install(SceneRenderer& renderer, std::future<EnvironmentMaps> future, u64 key, u32 tier) {
        renderer.pendingObjectEnv_ = std::move(future);
        renderer.pendingObjectKey_ = key;
        renderer.pendingObjectSpecTier_ = tier;
    }
    static u64 pending_key(const SceneRenderer& renderer) { return renderer.pendingObjectKey_; }
};
}

namespace {
SceneEnvironment object_environment(u64 key) {
    SceneEnvironment environment;
    auto pixels = std::make_shared<HdriPixels>();
    pixels->width = 32; pixels->height = 16;
    pixels->rgb.assign(32 * 16 * 3, .3f);
    environment.hdri = std::move(pixels);
    environment.hdriKey = key;
    return environment;
}
}

AUREA_TEST(SceneEnvironmentJobs, PreviewNeverWaitsOrReturnsAnotherObjectsMaps) {
    test::MockBackend gpu;
    SceneRenderer scene;
    SceneEnvironmentJobTestAccess::attach(scene, gpu);
    scene.set_environment_quality({16, 0, 1}, {32, 0, 1});
    const auto a = object_environment(10), b = object_environment(20);
    const auto* cachedA = scene.environment_set(a, 1);
    AUREA_CHECK(cachedA != nullptr); if (!cachedA) return;
    const auto oldA = cachedA->irradiance;
    std::promise<EnvironmentMaps> promise;
    SceneEnvironmentJobTestAccess::install(scene, promise.get_future(), b.hdriKey, 16);
    // A controlled unfinished worker proves polling returns before the worker
    // completes. Complete it after the bounded observation, even on failure.
    auto call = std::async(std::launch::async, [&] { return scene.environment_set(b, 2, false); });
    const bool returned = call.wait_for(std::chrono::milliseconds(250)) == std::future_status::ready;
    AUREA_CHECK(returned);
    if (returned) {
        AUREA_CHECK(call.get() == nullptr);
        AUREA_CHECK(scene.incomplete());
        AUREA_CHECK(scene.environment_pending());
        AUREA_CHECK_EQ(SceneEnvironmentJobTestAccess::pending_key(scene), b.hdriKey);
        AUREA_CHECK_EQ(gpu.texturesCreated, 3u);
    }
    promise.set_value(build_studio_environment({16, 0, 1}));
    if (!returned) (void)call.get();
    scene.reset_incomplete();
    const auto* readyB = scene.environment_set(b, 2, false);
    AUREA_CHECK(readyB != nullptr);
    AUREA_CHECK(!scene.incomplete());
    AUREA_CHECK(!scene.environment_pending());
    if (readyB) AUREA_CHECK(readyB->irradiance != oldA);
    AUREA_CHECK(gpu.textureAlive[oldA.id - 1]);
    scene.release_all();
    AUREA_CHECK_EQ(scene.resident_bytes(), 0u);
}

AUREA_TEST(SceneEnvironmentJobs, QualityPromotionRetainsCapturedMapsAndFinalMapsServePreview) {
    test::MockBackend gpu;
    SceneRenderer scene;
    SceneEnvironmentJobTestAccess::attach(scene, gpu);
    scene.set_environment_quality({16, 0, 1}, {16, 0, 1});
    const auto environment = object_environment(10);
    const auto* low = scene.environment_set(environment, 1);
    AUREA_CHECK(low != nullptr); if (!low) return;
    const TextureHandle captured = low->irradiance;
    std::promise<EnvironmentMaps> promise;
    SceneEnvironmentJobTestAccess::install(scene, promise.get_future(), environment.hdriKey, 32);
    scene.set_environment_quality({32, 0, 1}, {32, 0, 1});
    const auto* fallback = scene.environment_set(environment, 1, false);
    AUREA_CHECK(fallback != nullptr);
    if (fallback) AUREA_CHECK_EQ(fallback->irradiance, captured);
    AUREA_CHECK(scene.incomplete());
    promise.set_value(build_studio_environment({32, 0, 1}));
    scene.reset_incomplete();
    const auto* high = scene.environment_set(environment, 1, false);
    AUREA_CHECK(high != nullptr); if (!high) { scene.release_all(); return; }
    const TextureHandle promoted = high->irradiance;
    AUREA_CHECK_EQ(high->specularTier, 32u);
    AUREA_CHECK(promoted != captured);
    AUREA_CHECK(gpu.textureAlive[captured.id - 1]);
    AUREA_CHECK_EQ(gpu.texturesDestroyed, 0u);
    scene.set_environment_quality({16, 0, 1}, {32, 0, 1});
    AUREA_CHECK_EQ(scene.environment_set(environment, 2, false)->irradiance, promoted);
    AUREA_CHECK(!scene.environment_pending());
    AUREA_CHECK(!scene.incomplete());
    scene.release_all();
    AUREA_CHECK(!gpu.textureAlive[captured.id - 1]);
    AUREA_CHECK(!gpu.textureAlive[promoted.id - 1]);
}

AUREA_TEST(SceneEnvironmentJobs, FailedUploadRetriesAndProjectResetDiscardsCompletedJobs) {
    test::MockBackend gpu;
    SceneRenderer scene;
    SceneEnvironmentJobTestAccess::attach(scene, gpu);
    scene.set_environment_quality({16, 0, 1}, {16, 0, 1});
    const auto environment = object_environment(10);
    std::promise<EnvironmentMaps> failed;
    SceneEnvironmentJobTestAccess::install(scene, failed.get_future(), environment.hdriKey, 16);
    failed.set_value(build_studio_environment({16, 0, 1}));
    gpu.beforeTextureUpload = [](TextureHandle, const void*, u32) { return Status{Errc::Timeout}; };
    AUREA_CHECK(scene.environment_set(environment, 1) == nullptr);
    AUREA_CHECK(scene.incomplete());
    AUREA_CHECK_EQ(scene.resident_bytes(), 0u);
    gpu.beforeTextureUpload = {};
    scene.reset_incomplete();
    AUREA_CHECK(scene.environment_set(environment, 1) != nullptr);
    AUREA_CHECK(!scene.incomplete());
    std::promise<EnvironmentMaps> oldProject;
    SceneEnvironmentJobTestAccess::install(scene, oldProject.get_future(), 99, 16);
    oldProject.set_value(build_studio_environment({16, 0, 1}));
    const u32 created = gpu.texturesCreated;
    scene.release_all();
    AUREA_CHECK(!scene.environment_pending());
    AUREA_CHECK_EQ(scene.resident_bytes(), 0u);
    AUREA_CHECK_EQ(gpu.texturesCreated, created);
}

AUREA_TEST(SceneEnvironmentJobs, WorkerRetainsPixelsAndRejectsReplacingAnUnfinishedJob) {
    const EnvironmentQuality quality{16, 0, 1};
    auto pixels = std::make_shared<HdriPixels>();
    pixels->width = 32; pixels->height = 16; pixels->rgb.assign(32 * 16 * 3, .3f);
    std::future<EnvironmentMaps> job;
    AUREA_CHECK(start_environment_job(job, pixels, quality));
    pixels.reset();
    AUREA_CHECK(!start_environment_job(job, {}, quality));
    EnvironmentMaps maps;
    AUREA_CHECK(take_environment_job(job, maps));
    AUREA_CHECK_EQ(maps.prefiltered.size, 16u);
    AUREA_CHECK(!job.valid());
    AUREA_CHECK(!take_environment_job(job, maps));
}

AUREA_TEST(SceneEnvironmentJobs, OwnLightingProfilesKeepDistinctUniformsAndBatchEqualProfiles) {
    test::MockBackend gpu;
    gpu.mapBuffers = true;
    ShaderLibrary shaders;
    AUREA_CHECK(shaders.initialize(gpu).ok());
    SceneRenderer scene;
    AUREA_CHECK(scene.initialize(gpu, shaders).ok());
    scene.set_environment_quality({16, 0, 1}, {16, 0, 1});
    scene.set_environment_wait(false);
    SceneFrame frame;
    frame.camera = default_camera(256, 256);
    frame.post.bloom = false;
    frame.environment.intensity = .2f;
    scene.finish_environment(frame.environment);
    auto built = build_shape3d(default_shape3d(Shape3DKind::Cube));
    AUREA_CHECK(built.ok()); if (!built.ok()) { scene.shutdown(); shaders.shutdown(); return; }
    SceneInstance first;
    first.asset = std::shared_ptr<const SceneAsset>(std::move(built.asset));
    first.assetKey = 1;
    first.world = Mat4::translation({100, 100, 0});
    first.ownEnvironment = true;
    first.environment.intensity = 1.25f;
    first.environment.exposure = .5f;
    first.environment.rotation = .3f;
    first.environment.sky = {.3f, .4f, .5f};
    first.environment.ground = {.1f, .2f, .3f};
    frame.instances.push_back(first);
    SceneInstance second = first;
    second.world = Mat4::translation({150, 150, 0});
    second.environment.intensity = 3.5f;
    second.environment.exposure = 2.f;
    second.environment.rotation = -.7f;
    second.environment.sky = {.6f, .7f, .8f};
    second.environment.ground = {.4f, .5f, .6f};
    frame.instances.push_back(second);
    first.world = Mat4::translation({130, 130, 0});
    frame.instances.push_back(first); // equal profile remains eligible for instancing
    std::promise<EnvironmentMaps> pending;
    SceneEnvironmentJobTestAccess::install(scene, pending.get_future(), 0, 16);
    struct UniformPrefix {
        Mat4 viewProjection;
        Vec4 cameraPosition, environment, sky, ground;
    };
    std::vector<UniformPrefix> observed;
    gpu.beforeSetUniforms = [&](const void* bytes, u32 size) {
        if (size <= sizeof(UniformPrefix)) return; // excludes shadow/post blocks
        UniformPrefix prefix;
        std::memcpy(&prefix, bytes, sizeof(prefix));
        observed.push_back(prefix);
    };
    TransientTexturePool pool;
    for (u64 number = 1; number <= 2; ++number) {
        FrameGraph graph;
        Arena arena;
        FrameBegin begin;
        AUREA_CHECK(gpu.begin_offscreen_frame(begin).ok());
        FGTexture output;
        AUREA_CHECK(scene.build(graph, arena, frame, 256, 256, number, output));
        graph.set_output(output, ResourceState::ShaderRead);
        pool.begin_frame(gpu, number);
        AUREA_CHECK(graph.compile(pool).ok());
        observed.clear();
        graph.execute(*begin.commands, false);
        bool sawFirst = false, sawSecond = false;
        for (const auto& uniform : observed) {
            if (uniform.environment.x == first.environment.intensity) {
                sawFirst = true;
                AUREA_CHECK_EQ(uniform.cameraPosition.w, first.environment.exposure);
                AUREA_CHECK_EQ(uniform.environment.w, first.environment.rotation);
                AUREA_CHECK_EQ(uniform.sky.x, first.environment.sky.x);
                AUREA_CHECK_EQ(uniform.ground.x, first.environment.ground.x);
                AUREA_CHECK_EQ(uniform.environment.y, number == 1 ? 0.f : 1.f);
            }
            if (uniform.environment.x == second.environment.intensity) {
                sawSecond = true;
                AUREA_CHECK_EQ(uniform.cameraPosition.w, second.environment.exposure);
                AUREA_CHECK_EQ(uniform.environment.w, second.environment.rotation);
                AUREA_CHECK_EQ(uniform.sky.x, second.environment.sky.x);
                AUREA_CHECK_EQ(uniform.ground.x, second.environment.ground.x);
                AUREA_CHECK_EQ(uniform.environment.y, number == 1 ? 0.f : 1.f);
            }
        }
        AUREA_CHECK(sawFirst && sawSecond);
        AUREA_CHECK(scene.stats().instancedDraws > 0);
        AUREA_CHECK_EQ(scene.incomplete(), number == 1);
        graph.release(pool);
        pool.end_frame();
        AUREA_CHECK(gpu.end_frame().ok());
        if (number == 1) {
            pending.set_value(build_studio_environment({16, 0, 1}));
            scene.reset_incomplete();
        }
    }
    pool.clear();
    scene.shutdown();
    shaders.shutdown();
}
