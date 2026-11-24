#pragma once

#include "aurea/effects/Parameter.hpp"

namespace aurea { struct Layer; }

namespace aurea::repeat_layout {

inline constexpr char kModelKey[] = "aurea.3d.replicator";
inline constexpr char kPlaneKey[] = "aurea.repeat.arrange";
inline constexpr char kToonKey[] = "aurea.3d.toon";
inline constexpr u32 kMaxCopies = 64;

enum Param : u32 {
    Count, Layout, Spacing, Columns, Radius, EndLayout, Morph,
    Rotation, EndScale, Scatter, RandomRotation, Seed,
    Focus, FocusRadius, FocusScale, Progress, Stagger,
    ColorStart, ColorEnd, ColorMix, UseData, ScaleData, ParameterCount,
};

struct Copy {
    Vec3 position{};
    Vec3 rotation{};
    f32 scale = 1;
    f32 opacity = 1;
    Vec4 tint{};
};

void declare_parameters(ParameterRegistry&, bool spatial);
[[nodiscard]] const ParameterRegistry& model_parameters();
[[nodiscard]] u32 count(const ParamValue*) noexcept;
[[nodiscard]] Copy sample(const ParamValue*, const EffectInstance*, u32 index) noexcept;
[[nodiscard]] u32 model_count(const Layer&, const EffectInstance&, f64 time) noexcept;
[[nodiscard]] const EffectInstance* active(const Layer&, const char* key) noexcept;
[[nodiscard]] Copy sample_model(const Layer&, const EffectInstance&, f64 time, u32 index) noexcept;
[[nodiscard]] u32 toon_bands(const Layer&, f64 time) noexcept;

} // namespace aurea::repeat_layout
