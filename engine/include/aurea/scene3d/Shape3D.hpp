// =============================================================================
//  Aurea / scene3d / Shape3D.hpp
//
//  FORMAS 3D prontas (cubo, esfera, cilindro, cone, pirâmide, toro, estrela,
//  coração, cápsula e diamante), geradas pelo motor — nenhum arquivo.
//
//  Cada forma é feita de PARTES com nome (as faces do cubo, as pontas da
//  estrela, as metades do coração...): uma parte = um nó + uma malha + um
//  material na SceneAsset. Assim cada parte tem cor e imagem próprias (o
//  mapa de cor base do mesmo PBR dos modelos importados) e se move sozinha.
//
//  Mover, girar e escalar uma parte são TRILHAS da camada
//  (TrackProperty::ShapePart, effectIndex = parte, effectParamIndex = canal
//  0..8: posição XYZ, rotação XYZ em graus, escala XYZ), avaliadas pelo motor
//  de keyframes normal (easing, gráfico) e aplicadas depois da pose — no
//  mesmo lugar do layout do texto 3D, então preview, sombras e export veem o
//  mesmo quadro. Sem keyframe, o valor parado da trilha (`staticValue`) vale.
//
//  Como o texto 3D, o asset guarda só a RECEITA (forma, cor e imagem por
//  parte) no caminho de origem; ao reabrir o projeto a malha é gerada de novo.
//
//  Unidades: a forma cabe numa caixa de ~1 × 1 × 1 (Y para cima, frente +Z,
//  convenção glTF); a posição de uma parte anda nessas unidades.
// =============================================================================
#pragma once

#include "aurea/scene3d/Importer.hpp"

#include <functional>
#include <string>
#include <vector>

namespace aurea { struct Layer; }

namespace aurea::scene3d {

enum class Shape3DKind : u32 {
    Cube = 0, Sphere, Cylinder, Cone, Pyramid, Torus, Star, Heart, Capsule, Diamond,
};
inline constexpr u32 kShape3DKindCount = 10;
/// Teto de partes numa forma (o diamante tem 8). A receita recusa mais.
inline constexpr u32 kShape3DMaxParts = 8;
/// Canais de transform por parte: 0..2 posição, 3..5 rotação (°), 6..8 escala.
inline constexpr u32 kShape3DChannels = 9;

/// Prefixo do caminho de origem de um asset de forma 3D.
inline constexpr const char* kShape3DScheme = "aurea-shape3d:";

struct Shape3DPart {
    Vec4 color{1.0f, 1.0f, 1.0f, 1.0f};   ///< sRGB
    /// Imagem no mapa de cor (caminho como o motor guarda: "docs:..." ou
    /// absoluto). Vazio = só a cor. Com imagem, a cor tinge (branco = original).
    std::string image;
};

struct Shape3DSpec {
    Shape3DKind kind = Shape3DKind::Cube;
    f32 metallic = 0.0f;
    f32 roughness = 0.45f;
    std::vector<Shape3DPart> parts;   ///< sempre shape3d_part_count(kind) depois de decode/normalize
    /// FATIA do cubo (só Cube): o pedaço [boxMin, boxMax] do cubo inteiro
    /// (−0,5..0,5 em cada eixo). As faces ficam na caixa da fatia e a UV de
    /// cada face é a do cubo inteiro projetada no plano dela — a imagem segue
    /// contínua de uma fatia para a outra, e a face nova do corte mostra a
    /// imagem da face de fora com a mesma direção. Padrão = o cubo inteiro
    /// (projetos antigos não têm a chave "b" e abrem iguais).
    Vec3 boxMin{-0.5f, -0.5f, -0.5f};
    Vec3 boxMax{0.5f, 0.5f, 0.5f};
};

/// Menor espessura de uma fatia (unidades do cubo): abaixo disso a receita
/// recusa o corte (faces degeneradas).
inline constexpr f32 kShape3DMinSlice = 1.0f / 256.0f;
/// Faixa de partes do "dividir em partes".
inline constexpr u32 kShape3DSplitMin = 2;
inline constexpr u32 kShape3DSplitMax = 16;

/// A receita é o cubo inteiro (sem fatia)?
[[nodiscard]] bool shape3d_full_box(const Shape3DSpec& spec) noexcept;

/// Divide a receita (cubo ou fatia de cubo) em `count` fatias iguais ao longo
/// do eixo `axis` (0 X, 1 Y, 2 Z), na ordem do eixo (do menor ao maior).
/// Cada fatia leva a mesma cor/imagem por face; juntas, as caixas cobrem a
/// caixa original sem buraco. Vazio = não dá (não é cubo, eixo/contagem fora
/// da faixa ou fatia fina demais).
[[nodiscard]] std::vector<Shape3DSpec> split_shape3d_spec(const Shape3DSpec& spec, u32 axis, u32 count);

[[nodiscard]] u32 shape3d_part_count(Shape3DKind kind) noexcept;
/// Nome estável (inglês, minúsculo) da forma e da parte: chave da UI e nome
/// do nó. Ex.: "cube"/"front", "star"/"tip3". Nulo = fora da faixa.
[[nodiscard]] const char* shape3d_kind_key(Shape3DKind kind) noexcept;
[[nodiscard]] const char* shape3d_part_key(Shape3DKind kind, u32 part) noexcept;

/// Receita padrão da forma: cada parte com uma cor da paleta (dá para ver as
/// partes logo de cara) e sem imagem.
[[nodiscard]] Shape3DSpec default_shape3d(Shape3DKind kind);
/// Ajusta `parts` ao número de partes da forma (corta ou completa em branco).
void normalize_shape3d(Shape3DSpec& spec);

[[nodiscard]] std::string encode_shape3d(const Shape3DSpec& spec);
[[nodiscard]] bool decode_shape3d(const std::string& source, Shape3DSpec& out);
[[nodiscard]] inline bool is_shape3d_source(const std::string& source) noexcept {
    return source.rfind(kShape3DScheme, 0) == 0;
}

/// Caminho guardado → arquivo legível (o sandbox do projeto). Nulo = o mesmo.
using Shape3DPathResolver = std::function<std::string(const std::string&)>;

/// Gera a malha: um nó/malha/material por parte, na ordem das partes.
/// Imagens ilegíveis não impedem a forma: a parte fica só com a cor e o nome
/// do arquivo entra em `missingTextures`.
[[nodiscard]] ImportResult build_shape3d(const Shape3DSpec& spec, const Shape3DPathResolver& resolve = {},
                                         u32 maxTextureSize = 2048, u64 memoryBudget = 256ull << 20);

/// Lê PNG/JPEG/BMP/TGA do disco para RGBA8 (reduzida até `maxSize`).
[[nodiscard]] bool load_shape3d_image(const std::string& path, u32 maxSize, Image& out, u64 memoryBudget = 64ull << 20);

/// Valor do canal da parte no instante local (fracionário): keyframes, ou o
/// valor parado da trilha, ou o neutro (0 posição/rotação, 1 escala).
[[nodiscard]] f32 shape3d_part_value(const Layer& layer, u32 part, u32 channel, f64 localTime) noexcept;
/// Neutro do canal: 0 para posição e rotação, 1 para escala.
[[nodiscard]] constexpr f32 shape3d_channel_neutral(u32 channel) noexcept { return channel >= 6 ? 1.0f : 0.0f; }

/// Aplica as trilhas das partes em `nodeWorld` (pivô = centro da parte).
/// Chamado no mesmo lugar do layout do texto 3D: preview, sombras e export.
void apply_shape3d_parts(const SceneAsset& asset, const Layer& layer, f64 localTime, std::vector<Mat4>& nodeWorld);

/// Efeito "Shape 3D Layout" (effect_keys::kShape3DLayout): o layout por letra
/// do texto 3D com cada PARTE no lugar de uma letra — rotação X/Y/Z por
/// parte, atraso entre partes, variação, aleatório, primeira/última parte,
/// curvar, torcer e espalhar (afasta as partes do centro). Mesma conta de
/// apply_text3d_layout (definida em Text3D.cpp).
///
/// Ordem no renderer (preview = export): pose → layout (este) → trilhas das
/// partes (apply_shape3d_parts, pivô = onde o layout pôs a parte, então mover
/// uma parte à mão continua relativo ao lugar que ela ocupa) → animação por
/// unidade (apply_text3d_animators: a forma expõe textUnits = parte, parte, 0,
/// textWords = partes, textLines = 1).
void apply_shape3d_layout(const SceneAsset& asset, const Layer& layer, f64 localTime, std::vector<Mat4>& nodeWorld);

/// Matriz de uma parte a partir dos 9 canais (espaço do modelo, pivô `pivot`).
[[nodiscard]] Mat4 shape3d_part_matrix(const f32* channels9, Vec3 pivot) noexcept;

} // namespace aurea::scene3d
