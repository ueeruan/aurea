// =============================================================================
//  Aurea / scene3d / RigSkeleton.hpp
//
//  Contas do painel ESQUELETO e dos controles nas articulações (0.4.5, pedido
//  "rig 3D mais fácil": selecionar e girar ossos com anéis na junta, espelhar
//  L/R). Funções puras — o motor (EngineRigSkeleton.cpp) as usa sobre a pose
//  da camada (scene3d/RigPose.hpp) e grava nas trilhas BonePose de sempre:
//  nada novo no projeto salvo.
// =============================================================================
#pragma once

#include "aurea/scene3d/Animation.hpp"

#include <string>
#include <vector>

namespace aurea::scene3d {

/// Nome do osso do OUTRO lado: troca Left/Right (qualquer caixa), esquerdo/
/// direito, e um "L"/"R" isolado por separador (" L ", "_L", ".l", "L_"...).
/// Vazio = o nome não tem lado (osso do meio).
[[nodiscard]] std::string mirror_bone_name(const std::string& name);

/// Par do osso `node` entre `bones` (mesmo nome espelhado); −1 = sem par.
[[nodiscard]] i32 mirror_partner(const SceneAsset& asset, const std::vector<i32>& bones, i32 node);

/// Eixo lateral do modelo (0 X, 1 Y, 2 Z): o que mais separa os pares L/R na
/// pose `rest`. Sem par nenhum, X (glTF/FBX de frente para +Z).
[[nodiscard]] u32 mirror_axis(const SceneAsset& asset, const std::vector<i32>& bones, const Pose& rest);

/// Rotação refletida no plano perpendicular ao eixo `axis` do modelo
/// (S·R·S: o componente do eixo fica, os outros dois trocam de sinal).
[[nodiscard]] Quat mirror_rotation(Quat q, u32 axis) noexcept;

/// Anel de rotação na tela: ponto(θ) = U·cosθ + V·sinθ (relativo à junta).
/// O dedo vai de p0 a p1 (relativos à junta); devolve o giro em GRAUS que
/// leva o ponto do anel mais perto de p0 junto com o dedo (tangente no
/// ponto; anel de lado tem velocidade limitada por `radius`).
[[nodiscard]] f32 ring_drag_degrees(Vec2 u, Vec2 v, Vec2 p0, Vec2 p1, f32 radius) noexcept;

/// Giro (graus, sentido da tela com y para baixo) de p0 a p1 em volta de c.
[[nodiscard]] f32 swept_degrees(Vec2 c, Vec2 p0, Vec2 p1) noexcept;

} // namespace aurea::scene3d
