// =============================================================================
//  Aurea / scene3d / Environment.hpp
//
//  Iluminação baseada em imagem (IBL): ambiente → irradiância difusa +
//  especular pré-filtrado (GGX, um mip por rugosidade) + LUT da BRDF + fundo
//  visível com mips.
//
//  Gerado na CPU, de forma determinística: os mesmos bits no Android e no
//  iOS (sem depender de driver para convolução), fora da thread de render,
//  uma vez por ambiente e por qualidade. As linhas dos cubos são repartidas
//  entre threads (cada texel é independente: o resultado não depende de
//  quantas). Custo medido em Scene3D.EnvironmentHdriBuildTimes.
//
//  Convenção das faces: a do Vulkan/Metal (+X, −X, +Y, −Y, +Z, −Z), ambiente
//  com Y PARA CIMA (como todo HDRI). O shader converte a direção do mundo do
//  Aurea (Y para baixo) antes de amostrar.
// =============================================================================
#pragma once

#include "aurea/core/Math.hpp"
#include "aurea/core/Types.hpp"

#include <memory>
#include <vector>

namespace aurea::scene3d {

/// Cubemap RGBA16F: `levels[m]` = 6 faces de (size>>m)² texels, contíguas.
struct CubeData {
    u32 size = 0;
    u32 mips = 0;
    std::vector<std::vector<u16>> levels;
};

/// HDRI decodificado: RGB float linear, linhas de cima para baixo.
struct HdriPixels {
    u32 width = 0, height = 0;
    std::vector<f32> rgb;
    bool ldr = false;   ///< veio de jpg/png (sRGB → linear), não de um .hdr
};

/// Decodifica um panorama da memória. Radiance (.hdr) fica linear como está;
/// jpg/png (8 ou 16 bits) passam pela curva sRGB exata → linear (não a gama
/// 2,2 do stb) e são multiplicados por `ldrGain` (1 = como está; > 1 dá ao
/// panorama LDR um pouco da energia que um HDR teria). Vazio = não lido.
[[nodiscard]] std::shared_ptr<HdriPixels> decode_hdri(const u8* bytes, usize size, f32 ldrGain = 1.0f) noexcept;

/// Por que um panorama não entrou — a UI mostra uma frase para cada caso.
enum class HdriStatus : u8 {
    Ok = 0,
    Unreadable,          ///< arquivo não abriu / leitura cortada (E/S)
    UnsupportedFormat,   ///< não é HDR/EXR/JPG/PNG (nem um .zip com um deles)
    Corrupt,             ///< o formato é conhecido, mas os dados acabam/estão errados (download incompleto)
    TooLarge,            ///< passaria do teto de memória mesmo reduzindo
};

/// Teto da SAÍDA: 4096×2048 (o ambiente não usa mais que W/4 por face). Um
/// panorama maior é reduzido por média de caixa — não recusado.
inline constexpr u32 kHdriMaxPixels = 4096u * 2048u;

struct HdriDecode {
    std::shared_ptr<HdriPixels> pixels;   ///< nulo quando `status != Ok`
    HdriStatus status = HdriStatus::UnsupportedFormat;
};

/// O mesmo que `decode_hdri`, dizendo o motivo da falha. Aceita Radiance
/// (`#?RADIANCE`/`#?RGBE`, RLE novo e antigo, qualquer orientação ±Y ±X, RGBE
/// ou XYZE), OpenEXR (sem compressão, RLE, ZIP, ZIPS, PIZ, PXR24, B44; meia
/// precisão ou float; linhas ou tiles), JPG/PNG e um .zip com um desses
/// dentro. Valores não finitos/negativos viram 0.
[[nodiscard]] HdriDecode decode_hdri_detailed(const u8* bytes, usize size, f32 ldrGain = 1.0f,
                                            u64 memoryBudget = ~u64{0}) noexcept;

struct EnvironmentMaps {
    CubeData irradiance;      ///< 32², 1 mip
    /// base², mips até 4². Mip m ↔ rugosidade perceptual pela curva de
    /// `specular_lod` (a mesma do `env_lod` do pbr.frag). Mip 0 = espelho,
    /// filtrado por área a partir da fonte (não é uma amostra só por texel).
    CubeData prefiltered;
    /// O próprio ambiente (fundo visível), com mips completos até 1² (o
    /// céu escolhe o LOD pela pegada do pixel). Vazio (size 0) quando não foi
    /// pedido — o estúdio padrão nunca aparece como fundo.
    CubeData background;
    u32 lutSize = 0;
    std::vector<u16> brdfLut; ///< RG16F (+BA vazios, RGBA16F), lutSize²
};

/// Qualidade do IBL (tamanhos são tetos: um HDRI pequeno não é ampliado além
/// da própria resolução). Memória RGBA16F com mips: especular 256² ≈ 4,2 MB,
/// 512² ≈ 16,8 MB; fundo 512² ≈ 16,8 MB, 1024² ≈ 67 MB (só com fundo ligado).
struct EnvironmentQuality {
    u32 specularSize = 256;     ///< base do cubo especular (potência de 2, 16..1024)
    u32 backgroundSize = 0;     ///< teto da face do fundo; 0 = sem fundo
    u32 threads = 0;            ///< 0 = automático (núcleos − 1, até 8). Não muda o resultado.
    /// Preview (MEDIUM/HIGH): especular 256², fundo até 512². Gerado em
    /// segundo plano com só 2 threads — não disputa núcleos com o decode e a
    /// reprodução (o céu analítico cobre até ficar pronto).
    [[nodiscard]] static constexpr EnvironmentQuality preview() noexcept { return {256u, 512u, 2u}; }
    /// Export/captura/ULTRA: especular 512², fundo até 1024² (o export espera:
    /// usa todos os núcleos livres).
    [[nodiscard]] static constexpr EnvironmentQuality final_quality() noexcept { return {512u, 1024u, 0u}; }
};

/// Ambiente neutro de estúdio: céu claro, horizonte suave, chão escuro, uma
/// caixa de luz principal (frente-esquerda, alto) e uma de recorte (trás-
/// direita). É o ambiente padrão: IMPORTOU → APARECE bonito, com reflexo.
[[nodiscard]] EnvironmentMaps build_studio_environment(const EnvironmentQuality& q) noexcept;
[[nodiscard]] EnvironmentMaps build_studio_environment(u32 baseSize = 256) noexcept;

/// A partir de um HDRI equiretangular (RGB float linear, linhas de cima
/// para baixo). A conversão para cubo é filtrada por área: pirâmide da
/// equiretangular + 2×2 subamostras por texel, cada uma no mip que casa com a
/// sua pegada (anisotrópica perto dos polos). Um sol de poucos texels num 4K
/// não some nem pisca; a energia do mapa é preservada.
[[nodiscard]] EnvironmentMaps build_environment_from_equirect(const f32* rgb, u32 width, u32 height,
                                                              const EnvironmentQuality& q) noexcept;
[[nodiscard]] EnvironmentMaps build_environment_from_equirect(const f32* rgb, u32 width, u32 height,
                                                              u32 baseSize = 256) noexcept;

/// Rugosidade perceptual → LOD do especular pré-filtrado (Frostbite/Unity):
/// lod = (mips−1)·r·(1,7 − 0,7·r). É exatamente o `env_lod` do pbr.frag; os
/// mips são gerados com a inversa (`specular_roughness_at`).
[[nodiscard]] f32 specular_lod(f32 roughness, u32 mips) noexcept;
[[nodiscard]] f32 specular_roughness_at(f32 lod, u32 mips) noexcept;

/// Como o céu (environment.frag) amostra o fundo. O LOD base vem das
/// derivadas da direção (textureGrad: a pegada real de cada pixel, sem
/// pixelar nem serrilhar); o desfoque escala essas derivadas e, forte, mistura
/// o especular pré-filtrado (GGX, suave de verdade — mip de caixa ampliado
/// fica quadriculado).
struct BackgroundSampling {
    f32 gradScale = 1.0f;   ///< multiplica dFdx/dFdy da direção (2^LOD extra)
    f32 specLod = 0.0f;     ///< LOD no especular pré-filtrado
    f32 specBlend = 0.0f;   ///< 0 = só o fundo nítido; 1 = só o especular
};
/// `blur` 0..1 (EnvironmentSettings::backgroundBlur, lido como rugosidade).
[[nodiscard]] BackgroundSampling background_sampling(f32 blur, f32 fovY, u32 viewportHeight, u32 backgroundSize,
                                                     u32 specularMips) noexcept;

/// Direção (Y para cima) do texel (x, y) da face `face` de um cubo `size`².
[[nodiscard]] Vec3 cube_direction(u32 face, u32 x, u32 y, u32 size) noexcept;

[[nodiscard]] u16 float_to_half(f32 v) noexcept;
[[nodiscard]] f32 half_to_float(u16 h) noexcept;

} // namespace aurea::scene3d
