#pragma once
// Paletas do efeito Palettize: as prontas, a importação (PNG/JPG, .gpl do GIMP,
// .hex do Lospec) e a tabela que vai para a GPU. A tabela reaproveita o
// CubeLut (dimensions = 0): `size` cores sRGB seguidas das MESMAS cores em
// Oklab, para o shader comparar em Oklab e devolver a cor sRGB.
#include "aurea/effects/CubeLut.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace aurea {
inline constexpr u32 kMaxPaletteColors = 256;

/// sRGB codificado (0..1) → Oklab (L, a, b).
[[nodiscard]] Vec3 srgb_to_oklab(Vec3 srgb) noexcept;
/// Arquivo de paleta (e não .cube)? Pela extensão: .gpl, .hex, .png, .jpg, .jpeg.
[[nodiscard]] bool is_palette_path(std::string_view path) noexcept;
[[nodiscard]] Result<std::vector<Vec3>> parse_gpl_palette(std::string_view text);
[[nodiscard]] Result<std::vector<Vec3>> parse_hex_palette(std::string_view text);
/// Cores únicas de uma imagem RGBA8 (alfa < 50 % fica de fora); mais de 256
/// cores: as 256 mais frequentes.
[[nodiscard]] Result<std::vector<Vec3>> palette_from_rgba(const u8* rgba, u32 width, u32 height);
[[nodiscard]] Result<std::vector<Vec3>> read_palette_file(const std::string& path);
/// Tabela da GPU: `colors` em sRGB e depois em Oklab (dimensions = 0).
[[nodiscard]] CubeLut palette_table(const std::vector<Vec3>& colors);
/// .cube → LUT; arquivo de paleta → tabela de paleta.
[[nodiscard]] Result<CubeLut> read_color_table(const std::string& path);
}
