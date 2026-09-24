// =============================================================================
//  Aurea / scene3d / Text3D.hpp
//
//  Texto 3D: os contornos vetoriais da fonte (os mesmos do texto 2D) viram
//  MALHA — frente e fundo triangulados (com os furos das letras: "o", "a",
//  "B"...) e as laterais extrudadas com normais suaves nas curvas e vincadas
//  nos cantos. Com o chanfro ligado, a silhueta é recuada em anéis
//  interpolados por um perfil (reto = chanfro, redondo = filete): a malha
//  ganha frente/fundo menores, uma parede lateral entre os dois e o anel de
//  chanfro em cada ponta. É geometria de verdade — nada de brilho fingindo.
//
//  O texto entra no MESMO PBR dos modelos importados: metal, rugosidade,
//  especular, oclusão, emissão, HDRI/IBL, luzes, sombras. Nada de renderer
//  especial de texto. Frente, lateral e chanfro podem ter materiais
//  diferentes (o caso clássico: frente branca, chanfro cromado, lateral preta).
//
//  O asset guarda só a RECEITA (texto, geometria, materiais, alinhamento) no
//  caminho de origem; ao reabrir o projeto a malha é gerada de novo. A
//  geometria (contornos + triangulação + chanfro) tem cache próprio por
//  receita geométrica, então mexer só na cor não refaz o trabalho caro.
// =============================================================================
#pragma once

#include "aurea/scene3d/Importer.hpp"

#include <string>
#include <vector>

namespace aurea::text { class Font; }

namespace aurea::scene3d {

/// Material de uma região do texto. Só os campos que o PBR do Aurea lê.
struct Text3DMaterial {
    Vec4 color{1.0f, 1.0f, 1.0f, 1.0f};   ///< sRGB
    f32  metallic = 0.0f;
    f32  roughness = 0.35f;
    f32  specular = 1.0f;                 ///< KHR_materials_specular
    f32  occlusion = 1.0f;
    Vec3 emissive{0.0f, 0.0f, 0.0f};      ///< linear
    f32  emissiveStrength = 1.0f;
};

struct Text3DSpec {
    std::string content = "Texto";
    std::string fontPath;
    u32 animation = 0;                  ///< 0 parado, 1 onda, 2 giro X, 3 giro Y, 4 giro Z
    f32 animationDuration = 2.0f;
    f32 animationStagger = 0.12f;
    f32 animationAmount = 0.3f;          ///< onda: alturas de letra; giro: voltas
    f32  depth = 0.25f;                   ///< profundidade, em "alturas de letra" (1 = o tamanho da fonte)
    u32  alignment = 1;                   ///< 0 esquerda, 1 centro, 2 direita

    // --- material da FRENTE (e das outras regiões, se `regionMaterials` for falso)
    Vec4 color{1.0f, 1.0f, 1.0f, 1.0f};   ///< sRGB
    f32  metallic = 0.0f;
    f32  roughness = 0.35f;
    f32  specular = 1.0f;
    f32  occlusion = 1.0f;
    Vec3 emissive{0.0f, 0.0f, 0.0f};      ///< sRGB
    f32  emissiveStrength = 1.0f;

    // --- geometria: chanfro -------------------------------------------------
    // O chanfro RECUA a frente e o fundo em `bevelWidth` e gasta `bevelDepth`
    // do comprimento para chegar lá, num perfil entre reto (0) e filete
    // redondo (1) amostrado em `bevelSegments` anéis. `bevelDepth` é limitado
    // a 45 % de `depth` para as duas pontas não se cruzarem; se a silhueta não
    // comportar `bevelWidth` (letra fina, traço estreito) o recuo é reduzido
    // até caber — a malha sai sempre fechada.
    bool bevel = false;
    f32  bevelWidth = 0.02f;
    f32  bevelDepth = 0.02f;
    u32  bevelSegments = 3;               ///< 1..8
    f32  bevelRoundness = 1.0f;           ///< 0 = chanfro reto, 1 = filete redondo

    // --- materiais por região -----------------------------------------------
    bool regionMaterials = false;         ///< false = o material acima vale nas três regiões
    Text3DMaterial side;
    Text3DMaterial bevelMat;              ///< `bevelMat` porque `bevel` já é o interruptor

    [[nodiscard]] Text3DMaterial front_material() const noexcept {
        Text3DMaterial m;
        m.color = color;
        m.metallic = metallic;
        m.roughness = roughness;
        m.specular = specular;
        m.occlusion = occlusion;
        m.emissive = emissive;
        m.emissiveStrength = emissiveStrength;
        return m;
    }
};

/// Prefixo do caminho de origem de um asset de texto 3D.
inline constexpr const char* kText3DScheme = "aurea-text3d:";

[[nodiscard]] std::string encode_text3d(const Text3DSpec& spec);
[[nodiscard]] bool decode_text3d(const std::string& source, Text3DSpec& out);
[[nodiscard]] std::shared_ptr<const text::Font> text3d_font(const Text3DSpec& spec);

/// Chave do cache de geometria (fonte + parâmetros + conteúdo). Exposta para o
/// teste que prende a propriedade que interessa: ela NÃO pode mudar quando o
/// objeto `Font` muda de endereço, nem ser a mesma para fontes diferentes.
[[nodiscard]] std::string text3d_geometry_key(const text::Font& font, const Text3DSpec& spec);

/// Triangula um polígono com furos (anel 0 = borda, demais = furos; qualquer
/// orientação). Índices sobre os pontos concatenados na ordem dos anéis.
/// Falso se sobrar área sem triângulo (polígono degenerado).
bool triangulate_polygon(const std::vector<std::vector<Vec2>>& rings, std::vector<u32>& out);

/// Maior recuo que um contorno fechado comporta sem que nenhuma aresta vire do
/// avesso (o traço é mais estreito que 2 × recuo). É o teto do chanfro: um
/// contorno com um traço fino tem o dele, e o do vizinho não o piora.
[[nodiscard]] f32 contour_offset_limit(const std::vector<Vec2>& contour);

/// Recua um contorno fechado em `amount` na direção do MATERIAL (o lado
/// esquerdo da aresta — a orientação é normalizada: borda anti-horária, furo
/// horário). Junta os vértices pela bissetriz com limite de mitra. O número de
/// vértices não muda, então os anéis do chanfro casam entre si. Falso se o
/// recuo não é seguro (traço mais estreito que 2 × `amount`).
[[nodiscard]] bool offset_contour(const std::vector<Vec2>& contour, f32 amount, std::vector<Vec2>& out);

/// Gera a malha do texto. Unidades: 1 = a altura da fonte; Y para cima,
/// frente olhando para +Z (convenção glTF).
[[nodiscard]] ImportResult build_text3d(const text::Font& font, const Text3DSpec& spec);

} // namespace aurea::scene3d
