// =============================================================================
//  Aurea / scene3d / Animation.hpp
//
//  Avaliação de animação glTF no RELÓGIO DA TIMELINE: dado um clipe e um
//  instante (segundos, já no tempo local da layer), devolve a pose — matriz de
//  mundo de cada nó, matrizes de junta de cada skin e pesos de morph.
//
//  Sem estado: o mesmo instante dá a mesma pose (seek, scrub, export e preview
//  batem sempre). Interpolações do glTF: LINEAR (slerp nas rotações), STEP e
//  CUBICSPLINE (Hermite com as tangentes do arquivo).
// =============================================================================
#pragma once

#include "aurea/scene3d/SceneAsset.hpp"

#include <vector>

namespace aurea::scene3d {

struct Pose {
    std::vector<Mat4> nodeWorld;        ///< por nó, espaço da cena do modelo
    std::vector<Mat4> jointMatrices;    ///< todas as skins, achatadas (junta × inversa de bind)
    std::vector<u32>  skinJointOffset;  ///< início de cada skin em jointMatrices
    std::vector<std::vector<f32>> morphWeights;   ///< por nó (vazio = da malha/nó)
};

/// Tempo local da layer (s) → tempo do clipe, em laço. Clipe sem duração → 0.
[[nodiscard]] f32 clip_time(const Animation& clip, f64 layerSeconds) noexcept;

/// Pose do clipe `clip` (−1 = pose de repouso) no instante `t` (s, já no
/// intervalo do clipe).
void evaluate_pose(const SceneAsset& asset, i32 clip, f32 t, Pose& out);

} // namespace aurea::scene3d
