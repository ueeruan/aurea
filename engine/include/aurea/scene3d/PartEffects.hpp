// =============================================================================
//  Aurea / scene3d / PartEffects.hpp
//
//  Os efeitos de PARTE dos objetos 3D (pedido de beta de 12/10/2026: trocar a
//  imagem, tirar uma parte e animar outra viram EFEITOS, como o Text 3D
//  Layout): "Animar parte 3D", "Esconder partes 3D" e "Trocar textura 3D".
//
//  Parte N = o N-ésimo nó com malha da cena, na ordem dos nós — a mesma
//  unidade do Text/Shape 3D Layout (cada letra do texto 3D separado, cada
//  parte da forma 3D, cada objeto do modelo importado). Valem em qualquer
//  camada 3D e entram no MESMO lugar do layout (place_model): preview,
//  sombras, desfoque de movimento e export pelo mesmo caminho.
// =============================================================================
#pragma once

#include "aurea/core/Math.hpp"
#include "aurea/core/Types.hpp"

#include <string>
#include <vector>

namespace aurea {
struct Layer;
}

namespace aurea::scene3d {

struct SceneAsset;

/// Efeito de parte (Animar parte 3D, Esconder partes 3D, Trocar textura 3D):
/// só vale em camada 3D.
[[nodiscard]] bool is_part3d_effect(u32 effectType) noexcept;

/// Teto do índice de parte (o parâmetro "Parte" dos efeitos).
inline constexpr u32 kPartEffectMaxParts = 512;

/// Os nós que contam como parte, na ordem (nó com malha dentro da cena).
void part_nodes(const SceneAsset& asset, std::vector<u32>& out);

/// A camada usa "Animar parte 3D" ou "Esconder partes 3D" ligados (a pose
/// deixa de ser compartilhável e o desfoque de movimento reavalia a pose).
[[nodiscard]] bool has_part_effects(const Layer& layer) noexcept;
/// A camada usa "Trocar textura 3D" ligado.
[[nodiscard]] bool has_part_textures(const Layer& layer) noexcept;

/// "Animar parte 3D" (posição em % do tamanho do objeto, rotação XYZ, escala;
/// pivô = centro da parte) e "Esconder partes 3D" (multiplica `nodeOpacity`).
/// Chamado DEPOIS dos animadores do texto 3D (eles refazem `nodeOpacity`).
void apply_part_effects(const SceneAsset& asset, const Layer& layer, f64 localTime,
                        std::vector<Mat4>& nodeWorld, std::vector<f32>& nodeOpacity);

/// "Trocar textura 3D" no instante: a imagem (asset de imagem empacotado) que
/// cobre cada nó e quantas vezes ela se repete. O último efeito da pilha vence.
/// v76 (parâmetros 4..8 do efeito, acrescentados no fim): deslocamento X/Y
/// (% de uma repetição), giro (°) e ROLAR X/Y (% por segundo — o deslocamento
/// anda com o tempo da camada). `offset` já soma o rolar (preso em 0..1: a
/// imagem repete) e `rotation` sai em radianos. Efeito antigo (4 parâmetros):
/// zeros, a textura de sempre.
struct PartTexture {
    u32 node = 0;
    u64 image = 0;
    f32 repeat = 1.0f;
    Vec2 offset{0.0f, 0.0f};
    f32 rotation = 0.0f;
};
/// Índices dos parâmetros do "Trocar textura 3D" (contrato de arquivo).
inline constexpr u32 kPartTextureOffsetX = 4, kPartTextureOffsetY = 5, kPartTextureRotation = 6,
                     kPartTextureScrollX = 7, kPartTextureScrollY = 8;
/// O deslocamento (em repetições, 0..1) de um eixo no instante: o valor
/// parado + rolar × segundos. Puro (testes e renderer).
[[nodiscard]] f32 part_texture_offset(f32 offsetPct, f32 scrollPctPerSecond, f64 localFrames, f64 fps) noexcept;
void collect_part_textures(const SceneAsset& asset, const Layer& layer, f64 localTime, std::vector<PartTexture>& out,
                           f64 fps = 30.0);

/// Nome de cada parte (o nome do nó; vazio quando o arquivo não deu nome),
/// na ordem de `part_nodes`.
void part_names(const SceneAsset& asset, std::vector<std::string>& out);

} // namespace aurea::scene3d
