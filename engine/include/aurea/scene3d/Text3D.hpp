// =============================================================================
//  Aurea / scene3d / Text3D.hpp
//
//  Texto 3D: os contornos vetoriais da fonte (os mesmos do texto 2D) viram
//  malha — frente e fundo triangulados (com os furos das letras: "o", "a",
//  "B"...) e as laterais extrudadas com normais suaves nas curvas e vincadas
//  nos cantos. Sai um SceneAsset comum: desenha, ilumina, projeta sombra e
//  usa o HDRI como qualquer modelo importado.
//
//  O asset guarda só a RECEITA (texto, profundidade, cor, alinhamento) no
//  caminho de origem; ao reabrir o projeto a malha é gerada de novo.
// =============================================================================
#pragma once

#include "aurea/scene3d/Importer.hpp"

#include <string>
#include <vector>

namespace aurea::text { class Font; }

namespace aurea::scene3d {

struct Text3DSpec {
    std::string content = "Texto";
    f32  depth = 0.25f;                   ///< profundidade, em "alturas de letra" (1 = o tamanho da fonte)
    Vec4 color{1.0f, 1.0f, 1.0f, 1.0f};   ///< sRGB
    u32  alignment = 1;                   ///< 0 esquerda, 1 centro, 2 direita
};

/// Prefixo do caminho de origem de um asset de texto 3D.
inline constexpr const char* kText3DScheme = "aurea-text3d:";

[[nodiscard]] std::string encode_text3d(const Text3DSpec& spec);
[[nodiscard]] bool decode_text3d(const std::string& source, Text3DSpec& out);

/// Triangula um polígono com furos (anel 0 = borda, demais = furos; qualquer
/// orientação). Índices sobre os pontos concatenados na ordem dos anéis.
/// Falso se sobrar área sem triângulo (polígono degenerado).
bool triangulate_polygon(const std::vector<std::vector<Vec2>>& rings, std::vector<u32>& out);

/// Gera a malha do texto. Unidades: 1 = a altura da fonte; Y para cima,
/// frente olhando para +Z (convenção glTF).
[[nodiscard]] ImportResult build_text3d(const text::Font& font, const Text3DSpec& spec);

} // namespace aurea::scene3d
