#pragma once

#include "aurea/scene3d/SceneAsset.hpp"

#include <array>
#include <bit>
#include <cmath>
#include <functional>

namespace aurea::scene3d {

// Only these fields are editable per instance. The original GpuMaterial's
// identity fixes all other factors, texture transforms, textures and samplers.
struct MaterialOverrideValues {
    std::array<f32, 6> factors{}; // RGBA, metallic, roughness
    AlphaMode alphaMode = AlphaMode::Opaque;
    bool doubleSided = false;
    bool unlit = false;

    static MaterialOverrideValues from(const Material& material) noexcept {
        return {{material.baseColor.x, material.baseColor.y, material.baseColor.z,
                 material.baseColor.w, material.metallic, material.roughness},
                material.alphaMode, material.doubleSided, material.unlit};
    }
    void apply(Material& material) const noexcept {
        material.baseColor = Vec4{factors[0], factors[1], factors[2], factors[3]};
        material.metallic = factors[4];
        material.roughness = factors[5];
        material.alphaMode = alphaMode;
        material.doubleSided = doubleSided;
        material.unlit = unlit;
    }
};

struct MaterialOverrideKey {
    const void* source = nullptr;
    std::array<u32, 6> factors{};
    AlphaMode alphaMode = AlphaMode::Opaque;
    bool doubleSided = false;
    bool unlit = false;

    bool operator==(const MaterialOverrideKey&) const noexcept = default;
};

// Compare float bits, never a padded struct or an approximate/quantized value.
// Invalid authored values keep separate copies; caching a NaN could otherwise
// conceal a changing invalid state or combine unrelated draws.
[[nodiscard]] inline bool material_override_key(const void* source,
    const MaterialOverrideValues& values, MaterialOverrideKey& out) noexcept {
    if (!source) return false;
    MaterialOverrideKey key;
    key.source = source;
    for (usize i = 0; i < values.factors.size(); ++i) {
        if (!std::isfinite(values.factors[i])) return false;
        key.factors[i] = std::bit_cast<u32>(values.factors[i]);
    }
    key.alphaMode = values.alphaMode;
    key.doubleSided = values.doubleSided;
    key.unlit = values.unlit;
    out = key;
    return true;
}

struct MaterialOverrideKeyHash {
    usize operator()(const MaterialOverrideKey& key) const noexcept {
        usize hash = std::hash<const void*>{}(key.source);
        const auto mix = [&](usize value) {
            hash ^= value + usize{0x9e3779b9u} + (hash << 6) + (hash >> 2);
        };
        for (const u32 factor : key.factors) mix(factor);
        mix(static_cast<u8>(key.alphaMode));
        mix(key.doubleSided);
        mix(key.unlit);
        return hash;
    }
};

} // namespace aurea::scene3d
