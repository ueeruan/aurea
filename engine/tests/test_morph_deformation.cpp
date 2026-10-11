#include "TestFramework.hpp"
#include "aurea/scene3d/MorphDeformation.hpp"

#include <chrono>
#include <cstdlib>

using namespace aurea;
using namespace aurea::scene3d;

namespace {

void reference_vertex(const Primitive& p, usize vertex, std::span<const f32> weights, Vec3& position, Vec3& normal) {
    position = p.positions[vertex];
    normal = vertex < p.normals.size() ? p.normals[vertex] : Vec3{0, 0, 1};
    for (usize i = 0; i < std::min(p.morphTargets.size(), weights.size()); ++i) {
        if (weights[i] == 0.0f) continue;
        const auto& target = p.morphTargets[i];
        if (vertex < target.positions.size()) position = position + target.positions[vertex] * weights[i];
        if (vertex < target.normals.size()) normal = normal + target.normals[vertex] * weights[i];
    }
}

} // namespace

AUREA_TEST(MorphDeformation, SparseTargetsPreserveSmallNegativeWeightsAndAuthoredOrder) {
    Primitive p;
    p.positions = {{1, 2, 3}, {4, 5, 6}};
    p.normals = {{0, 0, 1}};
    p.morphTargets.resize(52);
    p.morphTargets[3].positions = {{8, -4, 2}, {4, 2, -2}};
    p.morphTargets[3].normals = {{0, 4, 0}};
    p.morphTargets[40].positions = {{1000000, 0, 0}};
    std::vector<f32> weights(53, 0.f);
    weights[3] = -.25f;
    weights[40] = .000001f;
    weights[52] = 1.f; // No corresponding target: ignored.
    std::vector<ActiveMorphTarget> active;
    select_active_morph_targets(p, weights, active);
    AUREA_CHECK_EQ(active.size(), usize{2});
    if (active.size() != 2) return;
    AUREA_CHECK(active[0].target == &p.morphTargets[3]);
    AUREA_CHECK(active[1].target == &p.morphTargets[40]);
    Vec3 position, normal;
    deform_morph_vertex(p, 0, active, position, normal);
    AUREA_CHECK_EQ(position, (Vec3{0, 3, 2.5f}));
    AUREA_CHECK_EQ(normal, (Vec3{0, -1, 1}));
    deform_morph_vertex(p, 1, active, position, normal);
    AUREA_CHECK_EQ(position, (Vec3{3, 4.5f, 6.5f}));
    AUREA_CHECK_EQ(normal, (Vec3{0, 0, 1}));
    weights.assign(52, 0.f);
    select_active_morph_targets(p, weights, active);
    AUREA_CHECK(active.empty());
}

AUREA_TEST(MorphDeformation, SparseAndDenseEvaluationMatchesFullLoopWithMissingStreams) {
    Primitive p;
    constexpr usize vertices = 257, targets = 52;
    for (usize v = 0; v < vertices; ++v) {
        p.positions.push_back({static_cast<f32>(v) * .01f, -2, 3});
        if (v < 200) p.normals.push_back({.3f, .4f, .5f});
    }
    p.morphTargets.resize(targets);
    for (usize t = 0; t < targets; ++t) {
        for (usize v = 0; v < vertices - t * 2; ++v) {
            p.morphTargets[t].positions.push_back({t * .004f, v * .002f, -.01f});
            if (t % 3) p.morphTargets[t].normals.push_back({.01f, -.02f, .03f});
        }
    }
    std::vector<ActiveMorphTarget> active;
    for (usize count : {usize{0}, usize{1}, usize{51}, usize{52}, usize{57}}) {
        for (bool sparse : {true, false}) {
            std::vector<f32> weights(count, 0.f);
            for (usize t = 0; t < count; ++t) if (!sparse || t == 3 || t == 40) weights[t] = (t % 2 ? -.25f : .5f);
            select_active_morph_targets(p, weights, active);
            for (usize v = 0; v < vertices; ++v) {
                Vec3 expectedPosition, expectedNormal, position, normal;
                reference_vertex(p, v, weights, expectedPosition, expectedNormal);
                deform_morph_vertex(p, v, active, position, normal);
                AUREA_CHECK_EQ(position, expectedPosition);
                AUREA_CHECK_EQ(normal, expectedNormal);
            }
        }
    }
}

AUREA_TEST(MorphDeformation, BenchSparseFacialTargets) {
    const char* enabled = std::getenv("AUREA_BENCH");
    if (!enabled || *enabled != '1') return;
    Primitive p;
    constexpr usize vertices = 50000, targets = 52;
    p.positions.resize(vertices, Vec3{1, 2, 3});
    p.normals.resize(vertices, Vec3{0, 0, 1});
    p.morphTargets.resize(targets);
    for (auto& target : p.morphTargets) {
        target.positions.resize(vertices, Vec3{.01f, .02f, .03f});
        target.normals.resize(vertices, Vec3{.02f, .01f, .04f});
    }
    std::vector<f32> weights(targets, 0.f);
    weights[3] = .75f; weights[40] = -.25f;
    std::vector<ActiveMorphTarget> active;
    std::vector<f64> fullTimes, sparseTimes;
    std::vector<Vec3> fullPosition(vertices), fullNormal(vertices), sparsePosition(vertices), sparseNormal(vertices);
    for (u32 round = 0; round < 60; ++round) {
        const auto fullStart = std::chrono::steady_clock::now();
        for (usize v = 0; v < vertices; ++v) reference_vertex(p, v, weights, fullPosition[v], fullNormal[v]);
        const auto sparseStart = std::chrono::steady_clock::now();
        select_active_morph_targets(p, weights, active);
        for (usize v = 0; v < vertices; ++v) deform_morph_vertex(p, v, active, sparsePosition[v], sparseNormal[v]);
        const auto done = std::chrono::steady_clock::now();
        if (round >= 10) {
            fullTimes.push_back(std::chrono::duration<f64, std::milli>(sparseStart - fullStart).count());
            sparseTimes.push_back(std::chrono::duration<f64, std::milli>(done - sparseStart).count());
        }
    }
    AUREA_CHECK(fullPosition == sparsePosition);
    AUREA_CHECK(fullNormal == sparseNormal);
    std::sort(fullTimes.begin(), fullTimes.end());
    std::sort(sparseTimes.begin(), sparseTimes.end());
    std::printf("\n    CPU host only: %zu vertices, %zu targets (2 active), full p50=%.4f ms, compact p50=%.4f ms\n",
                vertices, targets, fullTimes[fullTimes.size() / 2], sparseTimes[sparseTimes.size() / 2]);
}
