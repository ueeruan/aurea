// =============================================================================
//  AutoRig 3D (0.3.6 BETA): humanoide procedural em T-pose → juntas no lugar,
//  pesos somando 1 (≤ 4 influências), mapeamento Mixamo/Bip01, IK de dois
//  ossos alcança o alvo, keys de osso (rotação/translação/IK) na pose que o
//  renderer usa, receita no projeto e o comando assíncrono do motor.
// =============================================================================
#include "TestFramework.hpp"

#include "aurea/Engine.hpp"
#include "aurea/project/Project.hpp"
#include "aurea/project/Serialization.hpp"
#include "aurea/rig/AutoRig.hpp"
#include "aurea/rig/Ik.hpp"
#include "aurea/scene3d/Animation.hpp"
#include "aurea/scene3d/RigPose.hpp"
#include "aurea/scene3d/SceneAsset.hpp"
#include "aurea/timeline/Layer.hpp"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <string>
#include <thread>
#include <vector>

using namespace aurea;
using rig::HumanoidBone;

namespace {

/// Caixa com as 6 faces em grade k×k (vértices bastantes para pesos variarem).
void add_box(scene3d::Primitive& p, Vec3 lo, Vec3 hi, u32 k = 6) {
    for (int axis = 0; axis < 3; ++axis)
        for (int side = 0; side < 2; ++side) {
            const u32 base = static_cast<u32>(p.positions.size());
            const int u = (axis + 1) % 3, v = (axis + 2) % 3;
            for (u32 i = 0; i <= k; ++i)
                for (u32 j = 0; j <= k; ++j) {
                    f32 c[3];
                    const f32* l = &lo.x;
                    const f32* h = &hi.x;
                    c[axis] = side ? h[axis] : l[axis];
                    c[u] = l[u] + (h[u] - l[u]) * static_cast<f32>(i) / static_cast<f32>(k);
                    c[v] = l[v] + (h[v] - l[v]) * static_cast<f32>(j) / static_cast<f32>(k);
                    p.positions.push_back(Vec3{c[0], c[1], c[2]});
                }
            for (u32 i = 0; i < k; ++i)
                for (u32 j = 0; j < k; ++j) {
                    const u32 a = base + i * (k + 1) + j, b = a + 1, c = a + (k + 1), d = c + 1;
                    p.indices.insert(p.indices.end(), {a, c, b, b, c, d});
                }
        }
}

/// Boneco de 1,8 m em T-pose, de frente para +Z (esquerda do boneco = +X).
scene3d::Primitive humanoid_prim(bool tpose = true) {
    scene3d::Primitive p;
    add_box(p, {0.04f, 0.0f, -0.07f}, {0.16f, 0.84f, 0.07f});      // perna esq.
    add_box(p, {-0.16f, 0.0f, -0.07f}, {-0.04f, 0.84f, 0.07f});    // perna dir.
    add_box(p, {0.04f, 0.0f, -0.07f}, {0.16f, 0.08f, 0.20f});      // pé esq. (dedos para +Z)
    add_box(p, {-0.16f, 0.0f, -0.07f}, {-0.04f, 0.08f, 0.20f});    // pé dir.
    add_box(p, {-0.18f, 0.84f, -0.11f}, {0.18f, 1.45f, 0.11f});    // tronco
    add_box(p, {-0.05f, 1.45f, -0.05f}, {0.05f, 1.55f, 0.05f});    // pescoço
    add_box(p, {-0.10f, 1.55f, -0.11f}, {0.10f, 1.80f, 0.11f});    // cabeça
    if (tpose) {
        add_box(p, {0.18f, 1.33f, -0.05f}, {0.88f, 1.43f, 0.05f}, 10);    // braço esq.
        add_box(p, {-0.88f, 1.33f, -0.05f}, {-0.18f, 1.43f, 0.05f}, 10);  // braço dir.
    } else {
        add_box(p, {0.18f, 0.75f, -0.05f}, {0.28f, 1.43f, 0.05f}, 10);    // braços caídos
        add_box(p, {-0.28f, 0.75f, -0.05f}, {-0.18f, 1.43f, 0.05f}, 10);
    }
    return p;
}

scene3d::SceneAsset asset_of(scene3d::Primitive p) {
    scene3d::SceneAsset a;
    scene3d::Mesh m;
    for (const Vec3& v : p.positions) m.bounds.add(v);
    p.bounds = m.bounds;
    m.primitives.push_back(std::move(p));
    a.meshes.push_back(std::move(m));
    scene3d::Node n;
    n.name = "corpo";
    n.mesh = 0;
    a.nodes.push_back(n);
    a.roots.push_back(0);
    a.bounds = a.meshes[0].bounds;
    return a;
}

Vec3 J(const rig::AutoRigJoints& j, HumanoidBone b) { return j.position[static_cast<usize>(b)]; }

bool near3(Vec3 a, Vec3 b, f32 tol) { return (a - b).length() <= tol; }

/// Junta de maior peso de um vértice.
u16 dominant(const scene3d::Primitive& p, usize v) {
    const Vec4 w = p.weights[v];
    const f32 ws[4]{w.x, w.y, w.z, w.w};
    usize best = 0;
    for (usize k = 1; k < 4; ++k) if (ws[k] > ws[best]) best = k;
    return p.joints[v * 4 + best];
}

usize closest_vertex(const scene3d::Primitive& p, Vec3 q) {
    usize best = 0;
    for (usize v = 1; v < p.positions.size(); ++v)
        if ((p.positions[v] - q).length_sq() < (p.positions[best] - q).length_sq()) best = v;
    return best;
}

scene3d::SceneAsset rigged_humanoid() {
    scene3d::SceneAsset a = asset_of(humanoid_prim());
    rig::AutoRigJoints j;
    if (rig::detect_tpose(a, j) != rig::AutoRigError::None) return a;
    (void)rig::build_rigged_asset(a, j);
    return a;
}

i32 node_of(const scene3d::SceneAsset& a, HumanoidBone b) { return rig::map_by_names(a)[b]; }

Vec3 world_pos(const scene3d::Pose& pose, i32 node) { return rig::matrix_position(pose.nodeWorld[static_cast<usize>(node)]); }

} // namespace

AUREA_TEST(AutoRig, DetectsTheTPoseHumanoidJoints) {
    const scene3d::SceneAsset a = asset_of(humanoid_prim());
    rig::AutoRigJoints j;
    AUREA_CHECK_EQ(static_cast<i32>(rig::detect_tpose(a, j)), static_cast<i32>(rig::AutoRigError::None));
    constexpr f32 tol = 0.08f * 1.8f;
    std::printf("    quadril (%.2f %.2f %.2f) mao esq. (%.2f %.2f %.2f) pe dir. (%.2f %.2f %.2f) cabeca y %.2f\n",
                J(j, HumanoidBone::Hips).x, J(j, HumanoidBone::Hips).y, J(j, HumanoidBone::Hips).z,
                J(j, HumanoidBone::LeftHand).x, J(j, HumanoidBone::LeftHand).y, J(j, HumanoidBone::LeftHand).z,
                J(j, HumanoidBone::RightFoot).x, J(j, HumanoidBone::RightFoot).y, J(j, HumanoidBone::RightFoot).z,
                J(j, HumanoidBone::Head).y);
    AUREA_CHECK(near3(J(j, HumanoidBone::Hips), {0.0f, 0.95f, 0.0f}, tol));
    AUREA_CHECK(near3(J(j, HumanoidBone::Neck), {0.0f, 1.50f, 0.0f}, tol));
    AUREA_CHECK(near3(J(j, HumanoidBone::Head), {0.0f, 1.60f, 0.0f}, tol));
    AUREA_CHECK(near3(J(j, HumanoidBone::LeftUpperArm), {0.15f, 1.38f, 0.0f}, tol));
    AUREA_CHECK(near3(J(j, HumanoidBone::LeftLowerArm), {0.45f, 1.38f, 0.0f}, tol));
    AUREA_CHECK(near3(J(j, HumanoidBone::LeftHand), {0.70f, 1.38f, 0.0f}, tol));
    AUREA_CHECK(near3(J(j, HumanoidBone::RightHand), {-0.70f, 1.38f, 0.0f}, tol));
    AUREA_CHECK(near3(J(j, HumanoidBone::LeftUpperLeg), {0.10f, 0.90f, 0.0f}, tol));
    AUREA_CHECK(near3(J(j, HumanoidBone::LeftLowerLeg), {0.10f, 0.48f, 0.0f}, tol));
    AUREA_CHECK(near3(J(j, HumanoidBone::LeftFoot), {0.10f, 0.08f, 0.0f}, tol));
    AUREA_CHECK(near3(J(j, HumanoidBone::RightFoot), {-0.10f, 0.08f, 0.0f}, tol));
    // Dedos à frente (+Z): a ponta do pé vai para a frente do tornozelo.
    AUREA_CHECK(j.leftToe.z > J(j, HumanoidBone::LeftFoot).z + 0.05f);
}

AUREA_TEST(AutoRig, RefusesWhatIsNotATPoseHumanoid) {
    // Braços caídos: não é T-pose.
    rig::AutoRigJoints j;
    const rig::AutoRigError down = rig::detect_tpose(asset_of(humanoid_prim(false)), j);
    AUREA_CHECK(down == rig::AutoRigError::NotTPose || down == rig::AutoRigError::NotHumanoid);
    // Um cubo: nada de humanoide.
    scene3d::Primitive cube;
    add_box(cube, {-1, 0, -1}, {1, 2, 1});
    const rig::AutoRigError box = rig::detect_tpose(asset_of(cube), j);
    AUREA_CHECK(box == rig::AutoRigError::NotTPose || box == rig::AutoRigError::NotHumanoid);
    // Uma placa larga e baixa (mesa): também não.
    scene3d::Primitive slab;
    add_box(slab, {-1, 0, -0.1f}, {1, 0.8f, 0.1f});
    AUREA_CHECK(rig::detect_tpose(asset_of(slab), j) != rig::AutoRigError::None);
    // Modelo que já tem skin: não substitui.
    const scene3d::SceneAsset rigged = rigged_humanoid();
    AUREA_CHECK(rig::detect_tpose(rigged, j) == rig::AutoRigError::AlreadyRigged);
    // Cancelado antes de começar.
    std::atomic<bool> cancel{true};
    rig::AutoRigControl ctl;
    ctl.cancel = &cancel;
    AUREA_CHECK(rig::detect_tpose(asset_of(humanoid_prim()), j, ctl) == rig::AutoRigError::Cancelled);
}

AUREA_TEST(AutoRig, WeightsAreNormalizedFourInfluencesAndFollowTheBones) {
    const scene3d::SceneAsset a = rigged_humanoid();
    AUREA_CHECK_EQ(a.skins.size(), usize{1});
    AUREA_CHECK_EQ(a.nodes[0].skin, 0);
    const scene3d::Primitive& p = a.meshes[0].primitives[0];
    AUREA_CHECK(p.skinned());
    AUREA_CHECK_EQ(p.weights.size(), p.positions.size());
    AUREA_CHECK_EQ(p.joints.size(), p.positions.size() * 4);
    bool sums = true, indices = true;
    for (usize v = 0; v < p.positions.size(); ++v) {
        const Vec4 w = p.weights[v];
        sums = sums && std::fabs(w.x + w.y + w.z + w.w - 1.0f) < 1e-3f && w.x >= 0 && w.y >= 0 && w.z >= 0 && w.w >= 0;
        for (usize k = 0; k < 4; ++k) indices = indices && p.joints[v * 4 + k] < rig::kHumanoidBoneCount;
    }
    AUREA_CHECK(sums);
    AUREA_CHECK(indices);
    auto dom = [&](Vec3 q) { return static_cast<HumanoidBone>(dominant(p, closest_vertex(p, q))); };
    AUREA_CHECK(dom({0.88f, 1.38f, 0.0f}) == HumanoidBone::LeftHand);
    AUREA_CHECK(dom({-0.88f, 1.38f, 0.0f}) == HumanoidBone::RightHand);
    AUREA_CHECK(dom({0.0f, 1.80f, 0.0f}) == HumanoidBone::Head);
    AUREA_CHECK(dom({0.10f, 0.0f, 0.20f}) == HumanoidBone::LeftFoot);
    AUREA_CHECK(dom({-0.16f, 0.45f, 0.07f}) == HumanoidBone::RightLowerLeg || dom({-0.16f, 0.45f, 0.07f}) == HumanoidBone::RightUpperLeg);
    // O lado nunca troca: vértice do braço esquerdo sem peso de osso direito.
    const usize lv = closest_vertex(p, {0.6f, 1.43f, 0.05f});
    for (usize k = 0; k < 4; ++k) {
        const auto b = static_cast<HumanoidBone>(p.joints[lv * 4 + k]);
        const f32 w = (&p.weights[lv].x)[k];
        AUREA_CHECK(!(w > 0.0f && (b == HumanoidBone::RightUpperArm || b == HumanoidBone::RightLowerArm || b == HumanoidBone::RightHand)));
    }
    // Rig do AutoRig: os 17 ossos mapeados pelos nomes "aurea:*".
    const rig::HumanoidMap map = rig::map_by_names(a);
    AUREA_CHECK_EQ(map.mapped(), static_cast<u32>(rig::kHumanoidBoneCount));
    AUREA_CHECK(map.usable());
    // Repouso: a pose de descanso deixa a malha onde estava.
    scene3d::Pose pose;
    scene3d::evaluate_pose(a, -1, 0.0f, pose);
    const usize v = closest_vertex(p, {0.88f, 1.38f, 0.0f});
    Vec3 skinned{0, 0, 0};
    for (usize k = 0; k < 4; ++k)
        skinned += pose.jointMatrices[p.joints[v * 4 + k]].transform_point(p.positions[v]) * (&p.weights[v].x)[k];
    AUREA_CHECK(near3(skinned, p.positions[v], 1e-4f));
}

AUREA_TEST(AutoRig, RecipeRoundTripRebuildsTheSameWeights) {
    scene3d::SceneAsset a = asset_of(humanoid_prim());
    rig::AutoRigJoints j;
    AUREA_CHECK(rig::detect_tpose(a, j) == rig::AutoRigError::None);
    const std::string spec = rig::encode_autorig_spec(j);
    scene3d::SceneAsset b = a;
    AUREA_CHECK(rig::build_rigged_asset(a, j) == rig::AutoRigError::None);
    AUREA_CHECK(rig::apply_rig_spec(b, spec) == rig::AutoRigError::None);
    const auto& pa = a.meshes[0].primitives[0];
    const auto& pb = b.meshes[0].primitives[0];
    AUREA_CHECK(pa.joints == pb.joints);
    f32 diff = 0.0f;
    for (usize v = 0; v < pa.weights.size(); ++v)
        diff = std::max(diff, std::fabs(pa.weights[v].x - pb.weights[v].x) + std::fabs(pa.weights[v].y - pb.weights[v].y));
    AUREA_CHECK(diff < 1e-4f);
    // Mapa corrigido à mão (rig importado).
    rig::HumanoidMap m;
    m.joint[0] = 7;
    m.joint[4] = 9;
    bool isAuto = true;
    rig::HumanoidMap back;
    AUREA_CHECK(rig::decode_rig_spec(rig::encode_map_spec(m), isAuto, j, back));
    AUREA_CHECK(!isAuto);
    AUREA_CHECK(back.joint == m.joint);
    AUREA_CHECK(!rig::decode_rig_spec("arig1 a 1 2", isAuto, j, back));
}

AUREA_TEST(AutoRig, MapsMixamoAndBipedRigsByName) {
    auto build = [](const std::vector<std::pair<std::string, i32>>& nodes) {
        scene3d::SceneAsset a;
        scene3d::Skin skin;
        for (usize i = 0; i < nodes.size(); ++i) {
            scene3d::Node n;
            n.name = nodes[i].first;
            n.parent = nodes[i].second;
            n.translation = Vec3{0, 0.1f, 0};
            a.nodes.push_back(n);
            if (n.parent >= 0) a.nodes[static_cast<usize>(n.parent)].children.push_back(static_cast<i32>(i));
            else a.roots.push_back(static_cast<i32>(i));
            skin.joints.push_back(static_cast<i32>(i));
            skin.inverseBind.push_back(Mat4::identity());
        }
        a.skins.push_back(skin);
        return a;
    };
    const scene3d::SceneAsset mixamo = build({
        {"mixamorig:Hips", -1}, {"mixamorig:Spine", 0}, {"mixamorig:Spine1", 1}, {"mixamorig:Spine2", 2},
        {"mixamorig:Neck", 3}, {"mixamorig:Head", 4}, {"mixamorig:HeadTop_End", 5},
        {"mixamorig:LeftShoulder", 3}, {"mixamorig:LeftArm", 7}, {"mixamorig:LeftForeArm", 8}, {"mixamorig:LeftHand", 9},
        {"mixamorig:LeftHandIndex1", 10},
        {"mixamorig:RightShoulder", 3}, {"mixamorig:RightArm", 12}, {"mixamorig:RightForeArm", 13}, {"mixamorig:RightHand", 14},
        {"mixamorig:LeftUpLeg", 0}, {"mixamorig:LeftLeg", 16}, {"mixamorig:LeftFoot", 17}, {"mixamorig:LeftToeBase", 18},
        {"mixamorig:RightUpLeg", 0}, {"mixamorig:RightLeg", 20}, {"mixamorig:RightFoot", 21}, {"mixamorig:RightToeBase", 22},
    });
    const rig::HumanoidMap m = rig::map_by_names(mixamo);
    AUREA_CHECK(m.usable());
    AUREA_CHECK_EQ(m[HumanoidBone::Hips], 0);
    AUREA_CHECK_EQ(m[HumanoidBone::Spine], 1);
    AUREA_CHECK_EQ(m[HumanoidBone::Chest], 3);
    AUREA_CHECK_EQ(m[HumanoidBone::Neck], 4);
    AUREA_CHECK_EQ(m[HumanoidBone::Head], 5);
    AUREA_CHECK_EQ(m[HumanoidBone::LeftUpperArm], 8);
    AUREA_CHECK_EQ(m[HumanoidBone::LeftLowerArm], 9);
    AUREA_CHECK_EQ(m[HumanoidBone::LeftHand], 10);
    AUREA_CHECK_EQ(m[HumanoidBone::RightHand], 15);
    AUREA_CHECK_EQ(m[HumanoidBone::LeftUpperLeg], 16);
    AUREA_CHECK_EQ(m[HumanoidBone::LeftLowerLeg], 17);
    AUREA_CHECK_EQ(m[HumanoidBone::LeftFoot], 18);
    AUREA_CHECK_EQ(m[HumanoidBone::RightFoot], 22);

    const scene3d::SceneAsset biped = build({
        {"Bip01", -1}, {"Bip01 Pelvis", 0}, {"Bip01 Spine", 1}, {"Bip01 Spine1", 2}, {"Bip01 Neck", 3}, {"Bip01 Head", 4},
        {"Bip01 L Clavicle", 3}, {"Bip01 L UpperArm", 6}, {"Bip01 L Forearm", 7}, {"Bip01 L Hand", 8},
        {"Bip01 R Clavicle", 3}, {"Bip01 R UpperArm", 10}, {"Bip01 R Forearm", 11}, {"Bip01 R Hand", 12},
        {"Bip01 L Thigh", 1}, {"Bip01 L Calf", 14}, {"Bip01 L Foot", 15},
        {"Bip01 R Thigh", 1}, {"Bip01 R Calf", 17}, {"Bip01 R Foot", 18},
    });
    const rig::HumanoidMap b = rig::map_by_names(biped);
    AUREA_CHECK(b.usable());
    AUREA_CHECK_EQ(b[HumanoidBone::Hips], 1);
    AUREA_CHECK_EQ(b[HumanoidBone::Chest], 3);
    AUREA_CHECK_EQ(b[HumanoidBone::LeftUpperArm], 7);
    AUREA_CHECK_EQ(b[HumanoidBone::RightLowerArm], 11);
    AUREA_CHECK_EQ(b[HumanoidBone::LeftLowerLeg], 15);

    // Nomes comuns (Unreal / Blender): upperarm_l, thigh.R…
    const scene3d::SceneAsset ue = build({
        {"pelvis", -1}, {"spine_01", 0}, {"spine_03", 1}, {"neck_01", 2}, {"head", 3},
        {"upperarm_l", 2}, {"lowerarm_l", 5}, {"hand_l", 6}, {"upperarm_r", 2}, {"lowerarm_r", 8}, {"hand_r", 9},
        {"thigh_l", 0}, {"calf_l", 11}, {"foot_l", 12}, {"thigh_r", 0}, {"calf_r", 14}, {"foot_r", 15},
    });
    AUREA_CHECK(rig::map_by_names(ue).usable());
    // Esqueleto que não é humanoide: não é "compatível".
    const scene3d::SceneAsset tail = build({{"root", -1}, {"tail1", 0}, {"tail2", 1}});
    AUREA_CHECK(!rig::map_by_names(tail).usable());
}

AUREA_TEST(AutoRig, TwoBoneIkReachesTheTargetAndBendsTowardThePole) {
    const Vec3 a{0, 0, 0}, b{1, 0, 0}, c{2, 0, 0};
    const Vec3 target{1.2f, 0.8f, 0.3f}, pole{0.5f, 2.0f, 0.0f};
    const rig::TwoBoneIk s = rig::solve_two_bone(a, b, c, target, pole, 1.0f);
    AUREA_CHECK(s.reached);
    AUREA_CHECK(near3(s.end, target, 1e-3f));
    const Vec3 elbow = a + rig::rotate(s.rootDelta, b - a);
    AUREA_CHECK_NEAR((elbow - a).length(), 1.0f, 1e-4f);
    AUREA_CHECK(elbow.y > 0.5f);   // dobra para o lado do polo
    // Fora de alcance: estica na direção do alvo.
    const rig::TwoBoneIk far = rig::solve_two_bone(a, b, c, {0, 5, 0}, pole, 1.0f);
    AUREA_CHECK(!far.reached);
    AUREA_CHECK(near3(far.end, {0, 2, 0}, 1e-2f));
    // Peso 0: nada muda.
    const rig::TwoBoneIk off = rig::solve_two_bone(a, b, c, target, pole, 0.0f);
    AUREA_CHECK(near3(off.end, c, 1e-6f));
}

AUREA_TEST(AutoRig, BoneKeysAndIkDriveThePoseTheRendererUses) {
    const scene3d::SceneAsset a = rigged_humanoid();
    const i32 lArm = node_of(a, HumanoidBone::LeftUpperArm), lHand = node_of(a, HumanoidBone::LeftHand);
    const i32 hips = node_of(a, HumanoidBone::Hips), rFoot = node_of(a, HumanoidBone::RightFoot);
    AUREA_CHECK(lArm >= 0 && lHand >= 0 && hips >= 0 && rFoot >= 0);
    if (lArm < 0 || lHand < 0 || hips < 0 || rFoot < 0) return;
    Layer l;
    l.kind = LayerKind::Model3D;
    // Braço esquerdo: 0° no quadro 0 → −90° em Z no quadro 10 (o braço desce).
    Track& z = l.tracks.get_or_create(TrackProperty::BonePose, static_cast<u32>(lArm), 2);
    (void)z.set(FrameIndex{0}, 0.0f);
    (void)z.set(FrameIndex{10}, -90.0f);
    // Quadril: sobe 0,2 m no quadro 10 (params 3..5).
    Track& up = l.tracks.get_or_create(TrackProperty::BonePose, static_cast<u32>(hips), 4);
    (void)up.set(FrameIndex{0}, 0.0f);
    (void)up.set(FrameIndex{10}, 0.2f);
    scene3d::Pose rest, mid, end;
    scene3d::evaluate_rig_pose(a, l, -1, 0.0f, 0.0, rest);
    scene3d::evaluate_rig_pose(a, l, -1, 0.0f, 5.0, mid);
    scene3d::evaluate_rig_pose(a, l, -1, 0.0f, 10.0, end);
    const Vec3 s0 = world_pos(rest, lArm), h0 = world_pos(rest, lHand), h10 = world_pos(end, lHand), s10 = world_pos(end, lArm);
    const f32 armLen = (h0 - s0).length();
    AUREA_CHECK_NEAR(s10.y - s0.y, 0.2f, 1e-3f);
    AUREA_CHECK(near3(h10, s10 + Vec3{0, -armLen, 0}, 0.02f));   // pendurado
    AUREA_CHECK(world_pos(mid, lHand).y < h0.y && world_pos(mid, lHand).y > h10.y);
    // A malha vai junto: o vértice da mão segue as matrizes de junta.
    const scene3d::Primitive& p = a.meshes[0].primitives[0];
    const usize v = closest_vertex(p, {0.88f, 1.38f, 0.0f});
    Vec3 skinned{0, 0, 0};
    for (usize k = 0; k < 4; ++k)
        skinned += end.jointMatrices[p.joints[v * 4 + k]].transform_point(p.positions[v]) * (&p.weights[v].x)[k];
    AUREA_CHECK(skinned.y < s10.y - armLen * 0.8f);

    // IK do pé direito: alvo 0,3 m à frente e acima; polo à frente do joelho.
    const Vec3 foot0 = world_pos(rest, rFoot);
    const Vec3 target = foot0 + Vec3{0.0f, 0.3f, 0.25f};
    Layer k;
    k.kind = LayerKind::Model3D;
    const u32 effect = rig::kBoneIkBase + static_cast<u32>(rFoot);
    const f32 vals[7]{target.x, target.y, target.z, foot0.x, 0.6f, 1.0f, 1.0f};
    for (u32 c = 0; c < 7; ++c) k.tracks.get_or_create(TrackProperty::BonePose, effect, c).staticValue = vals[c];
    scene3d::Pose ik;
    scene3d::evaluate_rig_pose(a, k, -1, 0.0f, 0.0, ik);
    std::printf("    IK pe: alvo (%.3f %.3f %.3f) -> (%.3f %.3f %.3f)\n", target.x, target.y, target.z,
                world_pos(ik, rFoot).x, world_pos(ik, rFoot).y, world_pos(ik, rFoot).z);
    AUREA_CHECK(near3(world_pos(ik, rFoot), target, 2e-3f));
    AUREA_CHECK(world_pos(ik, node_of(a, HumanoidBone::RightLowerLeg)).z > foot0.z + 0.02f);   // joelho para a frente
    AUREA_CHECK(scene3d::has_bone_pose(k));
}

AUREA_TEST(AutoRig, ProjectKeepsTheRigRecipe) {
    auto p = Project::create_new(320, 180, 30.0, "autorig");
    AUREA_CHECK(p.ok());
    Project project = std::move(*p);
    Asset a;
    a.kind = AssetKind::Model3D;
    a.sourcePath = "docs:boneco.glb";
    a.model.rig = "arig1 m 0 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16";
    const AssetId id = project.add_asset(a);
    std::vector<u8> bytes;
    AUREA_CHECK(ProjectSerializer::encode(project, SaveOptions{}, bytes).ok());
    Project back;
    AUREA_CHECK(ProjectSerializer::load_bytes(back, bytes.data(), bytes.size(), LoadOptions{}, nullptr).ok());
    bool found = false;
    back.for_each_asset([&](AssetId, const Asset& b) { found = found || b.model.rig == a.model.rig; });
    AUREA_CHECK(found);
    (void)id;
}

AUREA_TEST(AutoRig, EngineRunsAutoRigAsyncWithUndoAndBoneKeys) {
    // OBJ do mesmo boneco.
    const scene3d::Primitive prim = humanoid_prim();
    const std::string path = "aurea_teste_autorig.obj";
    {
        std::FILE* f = std::fopen(path.c_str(), "wb");
        AUREA_CHECK(f != nullptr);
        if (!f) return;
        for (const Vec3& v : prim.positions) std::fprintf(f, "v %.5f %.5f %.5f\n", v.x, v.y, v.z);
        for (usize t = 0; t + 2 < prim.indices.size(); t += 3)
            std::fprintf(f, "f %u %u %u\n", prim.indices[t] + 1, prim.indices[t + 1] + 1, prim.indices[t + 2] + 1);
        std::fclose(f);
    }
    Engine e;
    EngineConfig ec;
    ec.workerCount = 1;
    ec.disableAutosave = true;
    AUREA_CHECK(e.initialize(ec).ok());
    AUREA_CHECK(e.new_project(640, 640, 30.0, "autorig").ok());
    ModelImport req;
    req.path = path;
    auto imported = e.import_model(req);
    AUREA_CHECK(imported.ok());
    if (!imported.ok()) { e.shutdown(); std::remove(path.c_str()); return; }
    const u64 id = *imported;
    AUREA_CHECK_EQ(e.humanoid_status(id), 0);
    AUREA_CHECK(e.autorig_start(id));
    Engine::AutoRigProgress pr;
    for (int i = 0; i < 600; ++i) {
        pr = e.autorig_progress();
        if (pr.state != 1) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    std::printf("    autorig: estado %d erro %d (%s)\n", pr.state, pr.error, rig::to_string(static_cast<rig::AutoRigError>(pr.error)));
    AUREA_CHECK_EQ(pr.state, 2);
    AUREA_CHECK_EQ(e.humanoid_status(id), 3);
    rig::HumanoidRig hr;
    AUREA_CHECK(e.query_humanoid(id, hr));
    AUREA_CHECK(hr.autoRig);
    // Juntas na tela (Corrigir esqueleto) e ossos posáveis.
    std::vector<f32> joints(e.query_humanoid_joints(id, nullptr, 0));
    AUREA_CHECK_EQ(joints.size(), rig::kHumanoidBoneCount * Engine::kHumanoidJointFloats);
    (void)e.query_humanoid_joints(id, joints.data(), static_cast<u32>(joints.size()));
    const usize hand = static_cast<usize>(HumanoidBone::LeftHand) * Engine::kHumanoidJointFloats;
    const f32 hx = joints[hand + 1], hy = joints[hand + 2];
    // Keys em lote (mocap): o braço desce no quadro 0; um passo de desfazer.
    rig::BoneKey key;
    key.node = hr.map[HumanoidBone::LeftUpperArm];
    key.frame = 0;
    key.eulerDeg = Vec3{0, 0, -90};
    AUREA_CHECK(e.set_bone_keys(id, &key, 1, "capturar movimento").ok());
    (void)e.query_humanoid_joints(id, joints.data(), static_cast<u32>(joints.size()));
    AUREA_CHECK(std::hypot(joints[hand + 1] - hx, joints[hand + 2] - hy) > 20.0f);
    // IK do braço: liga, aparece na tela, desliga.
    AUREA_CHECK(e.humanoid_set_ik(id, 0, true));
    std::vector<f32> ik(e.query_humanoid_ik(id, nullptr, 0));
    AUREA_CHECK_EQ(ik.size(), usize{4} * Engine::kHumanoidIkFloats);
    (void)e.query_humanoid_ik(id, ik.data(), static_cast<u32>(ik.size()));
    AUREA_CHECK_EQ(ik[0], 1.0f);
    AUREA_CHECK_EQ(e.toggle_humanoid_ik_key(id, 0), 1);
    AUREA_CHECK(e.humanoid_set_ik(id, 0, false));
    // Desfazer tudo até antes do AutoRig: o modelo volta sem esqueleto.
    Command undo;
    undo.type = CommandType::Undo;
    // AutoRig, keys, ligar IK, key do IK, desligar IK = 5 passos.
    for (int i = 0; i < 5; ++i) (void)e.apply_command(undo);
    AUREA_CHECK_EQ(e.humanoid_status(id), 0);
    e.shutdown();
    std::remove(path.c_str());
}
