#pragma once
// =============================================================================
//  Aurea / effects / builtin / CardReference.hpp
//
//  AE reference batch B: the camera and lighting groups shared by Card Wipe,
//  Card Dance and Shatter (the native "CAMLIGHT" controls), declared in the
//  native order. Behaviour: build/reference/effects-20261115/batch-b/cards.md.
// =============================================================================
#include "BuiltinEffects.hpp"

namespace aurea::card_reference {

inline constexpr const char* kCardWipeKey = "aurea.transition.card_wipe_reference";
inline constexpr const char* kCardDanceKey = "aurea.3d.card_dance_reference";
inline constexpr const char* kShatterKey = "aurea.3d.shatter_reference";

/// Camera System, Camera Position (7) and Corner Pins (6): 14 parameters from `base`.
struct CameraParams {
    u32 system, rotationX, rotationY, rotationZ, position, zPosition, focal, order;
    u32 upperLeft, upperRight, lowerLeft, lowerRight, autoFocal, cornerFocal;
};
[[nodiscard]] constexpr CameraParams camera_params(u32 base) noexcept {
    return {base, base + 1, base + 2, base + 3, base + 4, base + 5, base + 6, base + 7,
            base + 8, base + 9, base + 10, base + 11, base + 12, base + 13};
}
inline constexpr u32 kCameraParamCount = 14;

/// Lighting (6) and Material (3): 9 parameters from `base`.
struct LightParams {
    u32 type, intensity, color, position, depth, ambient, diffuse, specular, sharpness;
};
[[nodiscard]] constexpr LightParams light_params(u32 base) noexcept {
    return {base, base + 1, base + 2, base + 3, base + 4, base + 5, base + 6, base + 7, base + 8};
}
inline constexpr u32 kLightParamCount = 9;

/// Declares the 14 camera controls; returns the first index.
u32 declare_camera(ParameterRegistry& p);
/// Declares the 9 lighting and material controls; returns the first index.
u32 declare_light(ParameterRegistry& p);

/// The effect camera (card space: layer px, origin at the layer centre, y up,
/// z toward the camera) as four rows of a 4x4 that yields (x, y, z, w); screen
/// = centre + (x / w, -y / w). False = no valid camera (crossed corner pins).
[[nodiscard]] bool fill_camera(const EffectEval& e, const CameraParams& c, Vec2 layerSize, Vec4 rows[4],
                               Vec4& eye) noexcept;
/// Light (position or direction, w = 1 point), ambient (w = highlight on),
/// diffuse colour (w = highlight strength) and normalised highlight colour
/// (w = exponent applied to cos^2 of the reflection angle).
void fill_light(const EffectEval& e, const LightParams& l, Vec2 layerSize, Vec4& light, Vec4& ambient,
                Vec4& diffuse, Vec4& specular) noexcept;

void register_effects(EffectRegistry& registry);

} // namespace aurea::card_reference
