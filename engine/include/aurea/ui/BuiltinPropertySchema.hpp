#pragma once

#include "aurea/effects/Parameter.hpp"

#include <array>
#include <span>
#include <string_view>

namespace aurea::ui {

inline constexpr u32 kBuiltinPropertySchemaVersion = 1;
enum class PropertyDomain : u8 { Light, Material };

// Conditions inspect the existing native value array, not a second UI model.
struct PropertyCondition {
    i32 bindingParam = -1;
    std::array<f32, 2> equals{};
    u32 count = 0;
};

struct BuiltinPropertySpec {
    const char* id = "";
    const char* label = "";
    ParamType type = ParamType::Float;
    u32 components = 1;
    std::array<i32, 4> bindingParams{-1, -1, -1, -1};
    std::array<i32, 4> trackProperties{-1, -1, -1, -1};
    Vec4 defaultValue{};
    f32 sliderMin = 0, sliderMax = 1;
    f32 typedMin = 0, typedMax = 1;
    const char* unit = "";
    u32 precision = 2;
    const char* group = "";
    const char* colorSpace = "none";
    // Imported materials keep their source defaults; canonical values here
    // are metadata only and must not masquerade as a per-asset Reset action.
    const char* defaultSource = "layer";
    std::array<PropertyCondition, 2> conditions{};
    u32 conditionCount = 0;
};

[[nodiscard]] std::span<const BuiltinPropertySpec> builtin_properties(PropertyDomain domain) noexcept;
// Versioned, NUL-terminated JSON with process lifetime, built once without heap
// allocation. Current values remain in query_light/query_materials; all writes
// use the established native setters and TrackProperty contracts.
[[nodiscard]] std::string_view builtin_property_schema_json() noexcept;

} // namespace aurea::ui
