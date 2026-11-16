#pragma once

#include "aurea/effects/EffectRegistry.hpp"
#include "aurea/vector/Vector.hpp"

namespace aurea::extrude_reference {
inline constexpr const char* kKey = "aurea.generate.extrude_reference";
enum ParamIndex : u32 {
    kMaskPath, kAllPaths, kMode, kInner, kDirection, kBackLength, kFrontLength,
    kVanishing, kBackAmount, kFrontAmount, kStrokeWidth, kStrokeColor, kStrokeOpacity,
    kInheritColor, kFrontColor, kSideMode, kColorCount, kSideColor,
    kColor2, kAngle2, kColor3, kAngle3, kColor4, kAngle4, kColor5, kAngle5,
    kGlobalOffset, kInterpolate, kFillOpacity, kParent, kAnchor, kInputOpacity,
    kResolution, kCornerThreshold, kFlatness, kFrontResolution,
    kDebugView, kDebugX, kDebugY, kLeft, kTop, kRight, kBottom, kCount
};

struct SourcePaths {
    std::vector<vector::Contour> rings;
    Vec4 color{1, 1, 1, 1};
    f32 opacity = 1;
};

// Live source capture and triangulation are shared CPU rules for both mobile UIs.
[[nodiscard]] Status source_paths(const EffectEval& eval, std::vector<SourcePaths>& out);
[[nodiscard]] Status make_geometry(const EffectEval& eval, const std::vector<SourcePaths>& source,
                                    std::vector<Vec4>& vertices, Rect& bounds);
void register_effect(EffectRegistry& registry);

} // namespace aurea::extrude_reference
