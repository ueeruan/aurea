#include "TestFramework.hpp"
#include "aurea/scene3d/GroundPlacement.hpp"

#include <chrono>
#include <cstdlib>

using namespace aurea;
using namespace aurea::scene3d;

namespace {

std::shared_ptr<SceneAsset> ground_asset(usize nodes = 1) {
    auto a = std::make_shared<SceneAsset>();
    a->nodes.resize(nodes); a->nodes.back().mesh = 0;
    a->meshes.resize(1);
    Primitive p;
    p.positions = {{-4, -2, -3}, {3, 6, 1}, {0, 1, 5}};
    for (const auto& v : p.positions) p.bounds.add(v);
    a->bounds = p.bounds;
    a->meshes[0].primitives.push_back(std::move(p));
    return a;
}

f32 vertex_upper_y(const Primitive& primitive, const Mat4& world) {
    f32 y = -1e30f;
    for (const auto& v : primitive.positions) y = std::max(y, world.transform_point(v).y);
    return y;
}

} // namespace

AUREA_TEST(GroundPlacement, SparseTopologyAndLegacyFallbackAgreeWithoutScanningStaticVertices) {
    const auto asset = ground_asset(32768);
    SceneFrame frame; SceneInstance instance; instance.asset = asset;
    frame.instances.push_back(instance);
    GpuModel gpuModel; gpuModel.drawableNodes = {32767};
    const GpuModel* models[] = {&gpuModel};
    const auto legacy = ground_placement(frame);
    const auto sparse = ground_placement(frame, models);
    AUREA_CHECK(legacy.valid && sparse.valid);
    AUREA_CHECK_EQ(legacy.center, sparse.center);
    AUREA_CHECK_EQ(legacy.radius, sparse.radius);
    AUREA_CHECK_EQ(legacy.nodesVisited, u64{32768});
    AUREA_CHECK_EQ(sparse.nodesVisited, u64{1});
    AUREA_CHECK_EQ(sparse.verticesVisited, u64{0});
    AUREA_CHECK_EQ(sparse.center.y, 6.f);
}

AUREA_TEST(GroundPlacement, AxisProjectionTracksAnimatedTransformsIncludingReflectionAndCollapse) {
    const auto asset = ground_asset();
    SceneFrame frame; SceneInstance instance; instance.asset = asset; instance.nodeWorld.resize(1);
    frame.instances.push_back(instance);
    GpuModel gpuModel; gpuModel.drawableNodes = {0};
    const GpuModel* models[] = {&gpuModel};
    for (u32 axis = 0; axis < 3; ++axis) {
        for (f32 scale : {-2.f, 0.f, 3.f}) {
            Mat4 nodeWorld = Mat4::identity();
            nodeWorld.col[0].y = 0; nodeWorld.col[1].y = 0; nodeWorld.col[2].y = 0;
            nodeWorld.col[axis].y = scale;
            nodeWorld.col[3].y = 7.f + axis;
            frame.instances[0].nodeWorld[0] = nodeWorld;
            frame.instances[0].world = Mat4::translation({10, 20, 30});
            const auto result = ground_placement(frame, models);
            AUREA_CHECK(result.valid);
            AUREA_CHECK_EQ(result.center.y, vertex_upper_y(asset->meshes[0].primitives[0], frame.instances[0].world * nodeWorld));
            AUREA_CHECK_EQ(result.verticesVisited, u64{0});
        }
    }
}

AUREA_TEST(GroundPlacement, ArbitraryAndTinyRotationsRetainExactVertexSupport) {
    const auto asset = ground_asset();
    SceneFrame frame; SceneInstance instance; instance.asset = asset;
    frame.instances.push_back(instance);
    GpuModel gpuModel; gpuModel.drawableNodes = {0};
    const GpuModel* models[] = {&gpuModel};
    for (f32 tilt : {.5f, .000001f}) {
        Mat4 world = Mat4::identity();
        world.col[0].y = tilt; world.col[1].y = .25f; world.col[2].y = .75f; world.col[3].y = 9.f;
        frame.instances[0].world = world;
        const auto result = ground_placement(frame, models);
        AUREA_CHECK(result.valid);
        AUREA_CHECK_EQ(result.center.y, vertex_upper_y(asset->meshes[0].primitives[0], world));
        AUREA_CHECK_EQ(result.verticesVisited, u64{3});
    }
}

AUREA_TEST(GroundPlacement, DeformedPrimitivesNeverUseStaticAxisShortcut) {
    auto asset = ground_asset();
    SceneFrame frame; SceneInstance instance; instance.asset = asset; frame.instances.push_back(instance);
    GpuModel gpuModel; gpuModel.drawableNodes = {0}; const GpuModel* models[] = {&gpuModel};
    auto& p = asset->meshes[0].primitives[0];
    p.morphTargets.resize(1);
    AUREA_CHECK_EQ(ground_placement(frame, models).verticesVisited, u64{3});
    p.morphTargets.clear(); p.joints.resize(12); p.weights.resize(3);
    AUREA_CHECK_EQ(ground_placement(frame, models).verticesVisited, u64{3});
    asset->nodes[0].skin = 0;
    const auto skinned = ground_placement(frame, models);
    AUREA_CHECK(skinned.valid);
    AUREA_CHECK_EQ(skinned.center.y, 6.f);
}

AUREA_TEST(GroundPlacement, SelectedSceneTopologyIgnoresUnrenderedNodesAndNullModelsKeepFallback) {
    auto asset = ground_asset(2);
    asset->nodes[0].mesh = 0;
    SceneFrame frame; SceneInstance instance; instance.asset = asset;
    instance.nodeWorld = {Mat4::translation({0, 10000, 0}), Mat4::identity()};
    frame.instances.push_back(instance);
    GpuModel gpuModel; gpuModel.drawableNodes = {1}; const GpuModel* models[] = {&gpuModel};
    const auto selected = ground_placement(frame, models);
    AUREA_CHECK_EQ(selected.center.y, 6.f);
    AUREA_CHECK_EQ(selected.nodesVisited, u64{1});
    const GpuModel* pending[] = {nullptr};
    AUREA_CHECK_EQ(ground_placement(frame, pending).center.y, 10006.f);
    AUREA_CHECK_EQ(ground_placement(frame).center.y, 10006.f);
}

AUREA_TEST(GroundPlacement, BenchStaticGroundSupport) {
    const char* enabled = std::getenv("AUREA_BENCH");
    if (!enabled || *enabled != '1') return;
    auto asset = ground_asset(32768);
    auto& p = asset->meshes[0].primitives[0];
    p.positions.resize(200000, Vec3{1, 2, 3});
    SceneFrame frame; SceneInstance instance; instance.asset = asset; frame.instances.push_back(instance);
    GpuModel gpuModel; gpuModel.drawableNodes = {32767}; const GpuModel* models[] = {&gpuModel};
    std::vector<f64> scanTimes, compactTimes;
    f32 checksum = 0;
    for (u32 i = 0; i < 60; ++i) {
        const auto begin = std::chrono::steady_clock::now();
        checksum += vertex_upper_y(p, frame.instances[0].world);
        const auto compactStart = std::chrono::steady_clock::now();
        const auto ground = ground_placement(frame, models); checksum += ground.center.y;
        const auto done = std::chrono::steady_clock::now();
        if (i >= 10) {
            scanTimes.push_back(std::chrono::duration<f64, std::milli>(compactStart - begin).count());
            compactTimes.push_back(std::chrono::duration<f64, std::milli>(done - compactStart).count());
        }
    }
    AUREA_CHECK_EQ(checksum, 720.f);
    std::sort(scanTimes.begin(), scanTimes.end()); std::sort(compactTimes.begin(), compactTimes.end());
    std::printf("\n    CPU host only: 200000 static vertices, vertex scan p50=%.4f ms, complete ground bounds p50=%.4f ms\n",
                scanTimes[scanTimes.size() / 2], compactTimes[compactTimes.size() / 2]);
}
