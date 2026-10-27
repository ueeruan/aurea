// =============================================================================
//  Aurea / rig / Humanoid.hpp
//
//  Esqueleto humanoide canônico (AutoRig 3D + Motion Capture, 0.3.6 BETA).
//  Contrato comum dos agentes rig e mocap (build/reference/.../BRIEF-MOCAP.md).
//
//  - 17 ossos canônicos; `HumanoidMap` diz qual nó do SceneAsset é cada um.
//  - Rig do AutoRig: nós com nome "aurea:<bone_name>", mapeados direto.
//  - Rig importado (glTF/FBX com skin): mapeado por nomes comuns (Mixamo,
//    Bip01/3ds Max, Blender/Rigify, Unreal…) = "rig compatível".
//  - A pose de repouso (T-pose) fica em espaço do MODELO (a cena do
//    SceneAsset: metros, Y para cima, frente +Z), para o retargeting.
//
//  ANIMAÇÃO DOS OSSOS — propriedades animáveis comuns (sem sistema paralelo):
//  trilhas TrackProperty::BonePose da camada (scene3d/RigPose.hpp), mesmos
//  comandos de key, desfazer, editor de curvas, prévia = export.
//    effectIndex = nó da junta (< kBoneIkBase)
//      param 0..2  rotação extra X/Y/Z em graus (R = Rz·Ry·Rx), no espaço LOCAL
//                  da junta, por cima do repouso/clipe: local = local·R
//      param 3..5  translação extra X/Y/Z (unidades do nó pai) — o quadril
//    effectIndex = kBoneIkBase + nó da mão/pé (controlador IK de dois ossos:
//                  mão→antebraço→braço, pé→canela→coxa, pelos pais do nó)
//      param 0..2  alvo (espaço do modelo)  · 3..5 polo (espaço do modelo)
//      param 6     peso 0..1 (0 = desligado)
//  O mocap grava tudo isso numa ação de desfazer com Engine::set_bone_keys.
// =============================================================================
#pragma once

#include "aurea/core/Math.hpp"
#include "aurea/core/Types.hpp"

#include <array>
#include <string>
#include <vector>

namespace aurea::scene3d {
struct SceneAsset;
}

namespace aurea::rig {

enum class HumanoidBone : u8 {
    Hips = 0, Spine, Chest, Neck, Head,
    LeftUpperArm, LeftLowerArm, LeftHand,
    RightUpperArm, RightLowerArm, RightHand,
    LeftUpperLeg, LeftLowerLeg, LeftFoot,
    RightUpperLeg, RightLowerLeg, RightFoot,
    Count
};

inline constexpr usize kHumanoidBoneCount = static_cast<usize>(HumanoidBone::Count);

/// Pai canônico (Hips → Count = raiz).
[[nodiscard]] constexpr HumanoidBone parent_of(HumanoidBone b) noexcept {
    switch (b) {
        case HumanoidBone::Hips:          return HumanoidBone::Count;
        case HumanoidBone::Spine:         return HumanoidBone::Hips;
        case HumanoidBone::Chest:         return HumanoidBone::Spine;
        case HumanoidBone::Neck:          return HumanoidBone::Chest;
        case HumanoidBone::Head:          return HumanoidBone::Neck;
        case HumanoidBone::LeftUpperArm:  return HumanoidBone::Chest;
        case HumanoidBone::LeftLowerArm:  return HumanoidBone::LeftUpperArm;
        case HumanoidBone::LeftHand:      return HumanoidBone::LeftLowerArm;
        case HumanoidBone::RightUpperArm: return HumanoidBone::Chest;
        case HumanoidBone::RightLowerArm: return HumanoidBone::RightUpperArm;
        case HumanoidBone::RightHand:     return HumanoidBone::RightLowerArm;
        case HumanoidBone::LeftUpperLeg:  return HumanoidBone::Hips;
        case HumanoidBone::LeftLowerLeg:  return HumanoidBone::LeftUpperLeg;
        case HumanoidBone::LeftFoot:      return HumanoidBone::LeftLowerLeg;
        case HumanoidBone::RightUpperLeg: return HumanoidBone::Hips;
        case HumanoidBone::RightLowerLeg: return HumanoidBone::RightUpperLeg;
        case HumanoidBone::RightFoot:     return HumanoidBone::RightLowerLeg;
        case HumanoidBone::Count:         break;
    }
    return HumanoidBone::Count;
}

/// Nome estável (serialização, nós "aurea:<nome>", chave de texto da UI).
[[nodiscard]] constexpr const char* bone_name(HumanoidBone b) noexcept {
    switch (b) {
        case HumanoidBone::Hips:          return "hips";
        case HumanoidBone::Spine:         return "spine";
        case HumanoidBone::Chest:         return "chest";
        case HumanoidBone::Neck:          return "neck";
        case HumanoidBone::Head:          return "head";
        case HumanoidBone::LeftUpperArm:  return "left_upper_arm";
        case HumanoidBone::LeftLowerArm:  return "left_lower_arm";
        case HumanoidBone::LeftHand:      return "left_hand";
        case HumanoidBone::RightUpperArm: return "right_upper_arm";
        case HumanoidBone::RightLowerArm: return "right_lower_arm";
        case HumanoidBone::RightHand:     return "right_hand";
        case HumanoidBone::LeftUpperLeg:  return "left_upper_leg";
        case HumanoidBone::LeftLowerLeg:  return "left_lower_leg";
        case HumanoidBone::LeftFoot:      return "left_foot";
        case HumanoidBone::RightUpperLeg: return "right_upper_leg";
        case HumanoidBone::RightLowerLeg: return "right_lower_leg";
        case HumanoidBone::RightFoot:     return "right_foot";
        case HumanoidBone::Count:         break;
    }
    return "";
}

/// Prefixo dos nós criados pelo AutoRig ("aurea:hips"…).
inline constexpr const char* kAutoRigNodePrefix = "aurea:";

/// Primeiro effectIndex dos controladores IK nas trilhas BonePose.
inline constexpr u32 kBoneIkBase = 1u << 24;
/// Parâmetros das trilhas BonePose (ver o cabeçalho).
inline constexpr u32 kBoneParamRotX = 0, kBoneParamPosX = 3;
inline constexpr u32 kIkParamTargetX = 0, kIkParamPoleX = 3, kIkParamWeight = 6;

/// Índice do nó do modelo para cada osso canônico (−1 = ausente).
struct HumanoidMap {
    std::array<i32, kHumanoidBoneCount> joint{};
    HumanoidMap() noexcept { joint.fill(-1); }
    [[nodiscard]] i32 operator[](HumanoidBone b) const noexcept {
        return b < HumanoidBone::Count ? joint[static_cast<usize>(b)] : -1;
    }
    [[nodiscard]] u32 mapped() const noexcept {
        u32 n = 0;
        for (const i32 j : joint) n += j >= 0 ? 1u : 0u;
        return n;
    }
    /// Dá para animar o corpo: quadril, cabeça, e as cadeias braço/perna
    /// completas (pescoço, coluna e peito podem faltar).
    [[nodiscard]] bool usable() const noexcept;
};

/// Pose de repouso dos ossos mapeados, espaço do modelo.
struct HumanoidRest {
    std::array<Vec3, kHumanoidBoneCount> position{};     ///< junta no mundo do modelo
    std::array<Quat, kHumanoidBoneCount> rotation{};     ///< rotação de mundo (sem escala)
    std::array<Quat, kHumanoidBoneCount> localRotation{};///< rotação local do nó em repouso
    std::array<Quat, kHumanoidBoneCount> parentRotation{};///< mundo do nó PAI real (sem escala)
    std::array<u8, kHumanoidBoneCount> present{};
    /// Inversa da parte linear do mundo do PAI do quadril (modelo → local do pai):
    /// converte deslocamento do quadril em espaço do modelo para os params 3..5.
    Mat4 hipsParentInverse = Mat4::identity();
    f32 height = 0.0f;   ///< altura do corpo (pés → topo da cabeça estimado), m
};

/// Rig humanoide de um modelo: mapa + repouso.
struct HumanoidRig {
    HumanoidMap map;
    HumanoidRest rest;
    bool autoRig = false;     ///< criado pelo AutoRig (pesos do Aurea)
    bool compatible = false;  ///< map.usable()
};

/// Osso canônico pelo nome estável ("left_hand" → LeftHand). false = nenhum.
[[nodiscard]] bool bone_from_name(const std::string& name, HumanoidBone& out) noexcept;

/// Mapeamento por nome das juntas das skins do modelo (e dos nós "aurea:*").
[[nodiscard]] HumanoidMap map_by_names(const scene3d::SceneAsset& asset);

/// Repouso dos ossos mapeados (matrizes de repouso do asset).
[[nodiscard]] HumanoidRest rest_pose(const scene3d::SceneAsset& asset, const HumanoidMap& map);

/// Rotação local (param 0..2 em quaternion) que leva o osso `b` a girar
/// `deltaBone` em MUNDO desde o repouso, quando o pai canônico já girou
/// `deltaParent` em mundo (os nós intermediários ficam em repouso):
///   offset = R_b⁻¹ · deltaParent⁻¹ · deltaBone · R_b
[[nodiscard]] Quat offset_from_world_delta(const HumanoidRest& rest, HumanoidBone b, Quat deltaParent,
                                           Quat deltaBone) noexcept;

/// Deslocamento do quadril em espaço do modelo → params 3..5 (local do pai).
[[nodiscard]] Vec3 hips_local_translation(const HumanoidRest& rest, Vec3 modelDelta) noexcept;

/// Euler da trilha (graus, R = Rz·Ry·Rx) a partir de um quaternion, o mais
/// perto de `near` (continuidade entre quadros).
[[nodiscard]] Vec3 euler_deg_from_quat(Quat q, Vec3 nearDeg) noexcept;

/// Uma key de osso a gravar (Engine::set_bone_keys).
struct BoneKey {
    i32  node = -1;           ///< nó do modelo (HumanoidMap)
    i64  frame = 0;           ///< quadro LOCAL da camada
    Vec3 eulerDeg{0, 0, 0};   ///< params 0..2
    bool hasTranslation = false;
    Vec3 translation{0, 0, 0};///< params 3..5 (local do pai; ver hips_local_translation)
};

} // namespace aurea::rig
