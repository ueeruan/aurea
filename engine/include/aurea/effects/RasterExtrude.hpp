#pragma once

#include "aurea/effects/Effect.hpp"
#include "aurea/core/Types.hpp"
#include <array>

namespace aurea::raster_extrude {

enum ParamIndex : u32 { kDepth = 0, kSideColor, kShading, kQuality, kReflection, kMetal, kRoughness };
inline constexpr u32 kStrokeDepth = 5;
inline constexpr f32 kMaxDepth = 4096.0f;
/// Pirâmide de máximos de alfa nos slots 0..8; os três últimos levam o
/// ambiente da cena (irradiância, especular pré-filtrado, LUT da BRDF), como
/// no PBR dos modelos 3D.
inline constexpr u32 kTextureSlots = 9;
inline constexpr u32 kIrradianceSlot = 9, kPrefilterSlot = 10, kBrdfSlot = 11;
static_assert(kBrdfSlot < binding::kTextureSlots);
struct Options {
    f32 depth = 0;
    Vec4 sideColor{.45f,.45f,.45f,1};
    f32 shading = .7f;
    u32 quality = 1;
    /// Reflexo do ambiente (HDRI/estúdio da cena) — 0 = como antes.
    f32 reflection = 0;
    f32 metal = 0;
    f32 roughness = .3f;
};
struct Volume {
    std::array<FGTexture,kTextureSlots> textures{};
    Mat4 layerFromClip = Mat4::identity();
    Mat4 layerFromWorld = Mat4::identity();
    Vec4 box{}, params{}, sideColor{}, sizes{};
    Vec4 material{};   ///< x = reflexo do ambiente, y = metal, z = rugosidade
    u32 count = 0;
};
[[nodiscard]] bool accepts(LayerKind kind) noexcept;
[[nodiscard]] bool depth_parameter(EffectTypeId type, u32 index) noexcept;
[[nodiscard]] Options options_from(const EffectEval& eval) noexcept;
[[nodiscard]] bool update_projection(Volume& volume, const Mat4& clipFromLayer, const Mat4& clipFromWorld, Mat4* worldFromLayer = nullptr) noexcept;
/// Alpha maxima occupy at most one extra source area plus14 pixels. Traversal
/// is capped at64/192/512 cells; no CPU readback or unbounded GPU loop.
[[nodiscard]] bool build_volume(EffectBuildContext& ctx, const LayerImage& input,
    const Options& options, const Mat4& clipFromLayer, const Mat4& worldFromLayer, Volume& out) noexcept;

} // namespace aurea::raster_extrude
