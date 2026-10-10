#pragma once

#include "aurea/scene3d/SceneAsset.hpp"

#include <algorithm>
#include <span>
#include <vector>

namespace aurea::scene3d {

struct ActiveMorphTarget {
    const MorphTarget* target;
    f32 weight;
};

// Facial models often contain dozens of shapes, with only one or two active.
// Select once per primitive, rather than testing all weights per vertex. Keep
// the authored order and every nonzero weight (including small/negative ones),
// so accumulation is identical to the full target loop.
inline void select_active_morph_targets(const Primitive& primitive, std::span<const f32> weights,
                                        std::vector<ActiveMorphTarget>& active) {
    active.clear();
    const usize count = std::min(primitive.morphTargets.size(), weights.size());
    for (usize i = 0; i < count; ++i) {
        if (weights[i] != 0.0f) active.push_back({&primitive.morphTargets[i], weights[i]});
    }
}

inline void deform_morph_vertex(const Primitive& primitive, usize vertex,
                                std::span<const ActiveMorphTarget> active, Vec3& position, Vec3& normal) noexcept {
    position = primitive.positions[vertex];
    normal = vertex < primitive.normals.size() ? primitive.normals[vertex] : Vec3{0, 0, 1};
    for (const auto& entry : active) {
        const MorphTarget& target = *entry.target;
        if (vertex < target.positions.size()) position = position + target.positions[vertex] * entry.weight;
        if (vertex < target.normals.size()) normal = normal + target.normals[vertex] * entry.weight;
    }
}

} // namespace aurea::scene3d
