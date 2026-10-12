// =============================================================================
//  Aurea / scene3d / RigPose.hpp
//
//  Posar o rig de um modelo 3D na cena (0.1.8, pedido do dono: "como no
//  Blender" — a pessoa gira os ossos e marca os keyframes). A pose do usuário
//  fica nas trilhas TrackProperty::BonePose da camada (effectIndex = nó da
//  junta, paramIndex 0..2 = rotação X/Y/Z em graus, Euler da camada) e entra
//  por CIMA da animação do arquivo, no espaço local de cada junta, ANTES das
//  matrizes de junta — a malha deforma junto, na prévia e no export.
// =============================================================================
#pragma once

#include "aurea/scene3d/Animation.hpp"

#include <vector>

namespace aurea {
struct Layer;
}

namespace aurea::scene3d {

/// Teto de ossos posáveis por modelo (a lista que a UI recebe).
inline constexpr u32 kMaxPoseBones = 160;

/// Juntas das skins do modelo, sem repetir, na ordem do arquivo.
[[nodiscard]] std::vector<i32> pose_bones(const SceneAsset& asset);

/// A camada tem alguma trilha de pose de osso?
[[nodiscard]] bool has_bone_pose(const Layer& layer) noexcept;

/// Rotação de pose de cada osso no tempo LOCAL da camada (fracionário).
void bone_offsets(const Layer& layer, f64 local, std::vector<BoneOffset>& out);

/// Euler da camada (graus, R = Rz·Ry·Rx) ↔ quaternion.
[[nodiscard]] Quat bone_quat(Vec3 eulerDeg) noexcept;

} // namespace aurea::scene3d
