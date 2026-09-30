#pragma once
#include "aurea/timeline/Composition.hpp"
namespace aurea::grid {
inline constexpr const char* kController="aurea.layout.grid_builder";
inline constexpr const char* kItem="aurea.layout.grid_item";
void declare_parameters(ParameterRegistry&);
struct Placement { Mat4 matrix; f32 opacity=1; };
[[nodiscard]] Placement evaluate(const Composition&,const Layer&,f64 frame);
}
