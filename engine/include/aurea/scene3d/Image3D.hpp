// =============================================================================
//  Aurea / scene3d / Image3D.hpp
//
//  Imagem 3D: um PNG com alfa vira objeto extrudado. A silhueta sai do alfa, a
//  frente mostra a imagem e lateral, chanfro e traseira têm material próprio.
//  Como o texto 3D, o projeto guarda a RECEITA e a malha é refeita ao abrir.
// =============================================================================
#pragma once

#include "aurea/scene3d/Text3D.hpp"

namespace aurea::scene3d {

struct Image3DSpec {
    std::string imagePath;              ///< portátil (docs:...), como Text3DSpec::texturePath
    f32 depth = 0.14f;                  ///< em alturas da imagem (1 = a altura)
    f32 threshold = 0.5f;               ///< alfa da silhueta
    bool bevel = true;
    f32 bevelWidth = 0.012f;
    f32 bevelDepth = 0.012f;
    u32 bevelSegments = 3;              ///< 1..8
    f32 bevelRoundness = 1.0f;          ///< 0 reto, 1 filete
    Text3DMaterial front;               ///< multiplica a imagem
    Text3DMaterial side, bevelMat, back;
    Image3DSpec();
};

inline constexpr const char* kImage3DScheme = "aurea-image3d:";

[[nodiscard]] std::string encode_image3d(const Image3DSpec& spec);
[[nodiscard]] bool decode_image3d(const std::string& source, Image3DSpec& out);
void sanitize_image3d(Image3DSpec& spec) noexcept;
[[nodiscard]] inline bool is_image3d_source(const std::string& source) noexcept { return source.rfind(kImage3DScheme, 0) == 0; }
/// Texto 3D e Imagem 3D: a malha é refeita da receita, então pode sair da memória.
[[nodiscard]] inline bool is_rebuildable_mesh_source(const std::string& source) noexcept {
    return is_image3d_source(source) || source.rfind(kText3DScheme, 0) == 0;
}

/// A receita como números para as pontes, a mesma ordem nas duas plataformas:
/// 0 profundidade, 1 corte do alfa, 2 chanfro, 3 largura, 4 altura, 5 arredondamento,
/// 6 segmentos, 7-8 frente (metal, rugosidade), 9-13 lateral (rgb, metal, rugosidade),
/// 14-18 chanfro, 19-23 traseira.
inline constexpr u32 kImage3DFields = 24;
void image3d_to_fields(const Image3DSpec& spec, f32* out) noexcept;
void image3d_from_fields(const f32* in, Image3DSpec& spec) noexcept;
/// Material pronto do texto 3D na lateral, no chanfro e na traseira (a frente é a imagem).
[[nodiscard]] bool apply_image3d_material_preset(Image3DSpec& spec, u32 preset) noexcept;
/// Lateral e chanfro na cor média da parte visível da imagem (só ao criar).
[[nodiscard]] bool tint_image3d_sides(Image3DSpec& spec, const std::function<std::string(const std::string&)>& resolvePath,
                                      u32 maxTextureSize, u64 memoryBudget);

/// Lê a imagem, traça o alfa e gera a malha. Materiais: 0 frente, 1 lateral, 2 chanfro, 3 traseira.
[[nodiscard]] ImportResult build_image3d(const Image3DSpec& spec, const std::function<std::string(const std::string&)>& resolvePath,
                                         u32 maxTextureSize, u64 memoryBudget);
/// O mesmo a partir de pixels já decodificados (sem cache da geometria).
[[nodiscard]] ImportResult build_image3d_from(Image image, const Image3DSpec& spec);

} // namespace aurea::scene3d
