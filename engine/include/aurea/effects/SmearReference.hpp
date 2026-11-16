#pragma once
// Smear reference (AE Distort › Smear, ADBE SCHMEAR; REA batch-c/rea/Smear_New).
// The native plugin builds two closed curves from the layer masks (Source,
// Boundary), places the source copy with a matrix (rotate and scale about the
// mean of the source-mask vertices, then move that point to Mask Offset) and
// hands both to AE's elastic distortion engine. Aurea's own solver below
// produces a backward displacement grid: inside the moved source the pixel
// comes from the inverse matrix, outside the boundary nothing moves, and the
// band in between relaxes harmonically (elasticity = how far it relaxes).

#include "aurea/effects/EffectRegistry.hpp"

#include <vector>

namespace aurea::smear_reference {
inline constexpr const char* kKey = "aurea.distort.smear_mask_reference";
enum ParamIndex : u32 {
    kSourceMask, kBoundaryMask, kMaskOffset, kMaskRotation, kMaskScale, kPercent, kElasticity, kInterpolation, kCount
};
inline constexpr u32 kElasticityCount = 9;   ///< Stiff … Super Fluid

struct Inputs {
    std::vector<Vec2> source;     ///< flattened closed source curve (layer px)
    std::vector<Vec2> sourceVertices; ///< the mask's own vertices (pivot = their mean)
    std::vector<Vec2> boundary;   ///< flattened closed boundary curve (layer px)
    Vec2 offset{};                ///< Mask Offset (layer px)
    f32 rotationDegrees = 0;
    f32 scale = 1;
    f32 percent = 0;              ///< 0..1
    u32 elasticity = 0;           ///< 0..8
};

struct Grid {
    Rect area;                    ///< layer px covered by the grid
    u32 width = 0, height = 0;    ///< nodes
    f32 cell = 1;                 ///< px between nodes
    std::vector<Vec2> displacement; ///< source = position + displacement
    [[nodiscard]] bool empty() const noexcept { return displacement.empty(); }
};

/// Shared CPU rule (both mobile UIs): the displacement grid, empty = identity.
[[nodiscard]] Grid solve(const Inputs& in, u32 maxNodes = 96);
void register_effect(EffectRegistry& registry);

} // namespace aurea::smear_reference
