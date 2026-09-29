// =============================================================================
//  Aurea / timeline / Rig.hpp
//
//  RIG 2D de personagem desenhado (camada de imagem): esqueleto de juntas,
//  pose por rotação de osso (keyframes), malha com pele automática e IK.
//
//  O modelo é o do fantoche: cada junta com pai define um OSSO (pai → junta).
//  A rotação do osso gira em volta da junta pai e leva a subárvore inteira.
//  Todas as transformações são rígidas (rotação + translação), então girar um
//  osso "no mundo" é somar o ângulo dele — é o que torna FK e IK simples.
//
//  A imagem é deformada por uma GRADE triangulada sobre o retângulo dela; cada
//  vértice segue até 4 ossos, com peso pela distância ao segmento do osso
//  relativa à do osso mais perto (normalizados): mistura lisa perto das
//  juntas, rígido no meio do membro. Na montagem (todos os ângulos
//  0) cada osso é a identidade, então a malha devolve a imagem intacta.
//
//  Tudo aqui é CPU pura e determinística: o preview e o export chamam as
//  mesmas funções com o mesmo instante e saem com os mesmos vértices.
// =============================================================================
#pragma once

#include "aurea/core/Types.hpp"
#include "aurea/core/Math.hpp"

#include <vector>

namespace aurea {

struct Layer;
struct RigData;

namespace rig {

inline constexpr u32 kMaxJoints = 64;
inline constexpr u32 kMaxInfluences = 4;

/// Afim 2D rígida: x' = a·x + c·y + tx, y' = b·x + d·y + ty.
struct Affine {
    f32 a = 1.0f, b = 0.0f, c = 0.0f, d = 1.0f, tx = 0.0f, ty = 0.0f;
    [[nodiscard]] Vec2 apply(Vec2 p) const noexcept { return Vec2{a * p.x + c * p.y + tx, b * p.x + d * p.y + ty}; }
};

/// Índice da junta `id` em `rig.joints` (−1 = não existe).
[[nodiscard]] i32 index_of(const RigData& rig, u32 id) noexcept;

/// Ângulo de cada junta (graus, na ordem de `rig.joints`) no instante LOCAL
/// da camada. Sem trilha, 0 (a montagem).
void sample_angles(const Layer& l, FrameIndex local, std::vector<f32>& deg);

/// A transformação de cada osso (índice da junta da ponta; a raiz = identidade).
void pose(const RigData& rig, const std::vector<f32>& deg, std::vector<Affine>& out);

/// Posição de cada junta na pose (px da imagem).
void posed_joints(const RigData& rig, const std::vector<Affine>& bones, std::vector<Vec2>& out);

/// Malha com pele: grade triangulada sobre (0,0)–(w,h), pesos por vértice.
struct SkinMesh {
    std::vector<Vec2> rest;                 ///< vértice na montagem (px da imagem)
    std::vector<Vec2> uv;                   ///< 0..1 na imagem
    std::vector<u32>  tris;                 ///< 3 índices por triângulo
    std::vector<u32>  bone;                 ///< kMaxInfluences por vértice (índice da junta)
    std::vector<f32>  weight;               ///< kMaxInfluences por vértice (soma 1)
};

/// Monta a grade (`cells` ao longo do lado maior) e os pesos automáticos.
/// Sem osso nenhum, a malha sai vazia.
void build_skin(const RigData& rig, f32 width, f32 height, u32 cells, SkinMesh& out);

/// Vértices deformados pela pose (mesma ordem de `m.rest`).
void deform(const SkinMesh& m, const std::vector<Affine>& bones, std::vector<Vec2>& out);

/// FK: o ângulo a somar no osso (pai → junta `index`) para a junta apontar
/// para `target` (px da imagem). 0 para a raiz.
[[nodiscard]] f32 fk_delta(const RigData& rig, const std::vector<Affine>& bones, u32 index, Vec2 target) noexcept;

/// IK de 2 ossos (avô → pai → junta `index`): os deltas (graus) do osso do
/// pai e do osso da junta para a ponta chegar em `target` (ou o mais perto
/// possível), mantendo o lado para o qual o "cotovelo" já dobrava.
/// false = a junta não tem avô.
bool ik_two_bone(const RigData& rig, const std::vector<Affine>& bones, u32 index, Vec2 target,
                 f32& deltaParent, f32& deltaJoint) noexcept;

} // namespace rig
} // namespace aurea
