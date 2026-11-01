#pragma once

#include "aurea/scene3d/SceneRenderer.hpp"

#include <span>

namespace aurea::scene3d {

/// Largest Y is the bottom of the model in Aurea's world coordinates.
struct GroundPlacement {
    bool valid = false;
    Vec3 center{};
    f32 radius = 1.0f;
    // CPU work counters for deterministic performance regressions.
    u64 nodesVisited = 0;
    u64 verticesVisited = 0;
};

/// 0.4.1 (Reflective Floor): uma cena só com camadas 2D no espaço 3D ([planes])
/// também ganha chão, sob o canto mais baixo delas. Com algum modelo na cena os
/// planos não contam: o chão (e a sombra que ele recebe) fica sob os modelos.
[[nodiscard]] GroundPlacement ground_placement(const SceneFrame& frame,
    std::span<const GpuModel* const> models = {}, std::span<const ScenePlane> planes = {}) noexcept;

} // namespace aurea::scene3d
