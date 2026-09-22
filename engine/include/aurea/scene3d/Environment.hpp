// =============================================================================
//  Aurea / scene3d / Environment.hpp
//
//  Iluminação baseada em imagem (IBL): ambiente → irradiância difusa +
//  especular pré-filtrado (GGX, um mip por rugosidade) + LUT da BRDF.
//
//  Gerado na CPU, de forma determinística: os mesmos bits no Android e no
//  iOS (sem depender de driver para convolução), numa thread de fundo, uma
//  vez por ambiente. Custo medido em Scene3D.EnvironmentBuildIsFast.
//
//  Convenção das faces: a do Vulkan/Metal (+X, −X, +Y, −Y, +Z, −Z), ambiente
//  com Y PARA CIMA (como todo HDRI). O shader converte a direção do mundo do
//  Aurea (Y para baixo) antes de amostrar.
// =============================================================================
#pragma once

#include "aurea/core/Math.hpp"
#include "aurea/core/Types.hpp"

#include <vector>

namespace aurea::scene3d {

/// Cubemap RGBA16F: `levels[m]` = 6 faces de (size>>m)² texels, contíguas.
struct CubeData {
    u32 size = 0;
    u32 mips = 0;
    std::vector<std::vector<u16>> levels;
};

struct EnvironmentMaps {
    CubeData irradiance;      ///< 32², 1 mip
    CubeData prefiltered;     ///< base², mips até 4² (mip m ↔ rugosidade m/(mips−1))
    CubeData background;      ///< o próprio ambiente (fundo visível), base², 1 mip
    u32 lutSize = 0;
    std::vector<u16> brdfLut; ///< RG16F (+BA vazios, RGBA16F), lutSize²
};

/// Ambiente neutro de estúdio: céu claro, horizonte suave, chão escuro, uma
/// caixa de luz principal (frente-esquerda, alto) e uma de recorte (trás-
/// direita). É o ambiente padrão: IMPORTOU → APARECE bonito, com reflexo.
[[nodiscard]] EnvironmentMaps build_studio_environment(u32 baseSize = 128) noexcept;

/// A partir de um HDRI equiretangular (RGB float linear, linhas de cima
/// para baixo).
[[nodiscard]] EnvironmentMaps build_environment_from_equirect(const f32* rgb, u32 width, u32 height,
                                                              u32 baseSize = 128) noexcept;

/// Direção (Y para cima) do texel (x, y) da face `face` de um cubo `size`².
[[nodiscard]] Vec3 cube_direction(u32 face, u32 x, u32 y, u32 size) noexcept;

[[nodiscard]] u16 float_to_half(f32 v) noexcept;
[[nodiscard]] f32 half_to_float(u16 h) noexcept;

} // namespace aurea::scene3d
