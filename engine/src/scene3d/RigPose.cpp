// Pose do rig 3D pelo usuário (ver RigPose.hpp).
#include "aurea/scene3d/RigPose.hpp"

#include "aurea/rig/Humanoid.hpp"
#include "aurea/rig/Ik.hpp"
#include "aurea/rig/Mocap.hpp"
#include "aurea/scene3d/SceneAsset.hpp"
#include "aurea/timeline/Layer.hpp"

#include <algorithm>
#include <cmath>

namespace aurea::scene3d {

std::vector<i32> pose_bones(const SceneAsset& asset) {
    std::vector<i32> bones;
    std::vector<u8> seen(asset.nodes.size(), 0);
    for (const Skin& skin : asset.skins) {
        for (const i32 joint : skin.joints) {
            if (joint < 0 || static_cast<usize>(joint) >= asset.nodes.size() || seen[static_cast<usize>(joint)]) continue;
            seen[static_cast<usize>(joint)] = 1;
            bones.push_back(joint);
            if (bones.size() >= kMaxPoseBones) return bones;
        }
    }
    return bones;
}

bool has_bone_pose(const Layer& layer) noexcept {
    if (rig::active_preview(layer.model.mocapPreview)) return true;   // prévia do Motion Capture
    for (u32 i = 0; i < layer.tracks.size(); ++i)
        if (layer.tracks.at(i).property == TrackProperty::BonePose) return true;
    return false;
}

Quat bone_quat(Vec3 e) noexcept {
    constexpr f32 k = 3.14159265358979323846f / 180.0f;
    return Quat::from_euler_zyx(e.x * k, e.y * k, e.z * k).normalized();
}

void bone_offsets(const Layer& layer, f64 local, std::vector<BoneOffset>& out) {
    out.clear();
    for (u32 i = 0; i < layer.tracks.size(); ++i) {
        const Track& t = layer.tracks.at(i);
        // 0..2 rotação, 3..5 translação (0.3.6); controladores IK à parte.
        if (t.property != TrackProperty::BonePose || t.effectParamIndex > 5 || t.effectIndex >= rig::kBoneIkBase) continue;
        const i32 node = static_cast<i32>(t.effectIndex);
        auto it = std::find_if(out.begin(), out.end(), [&](const BoneOffset& b) { return b.node == node; });
        if (it == out.end()) {
            out.push_back(BoneOffset{node, Quat::identity(), Vec3{0, 0, 0}, Vec3{0, 0, 0}});
            it = out.end() - 1;
        }
        const f32 v = t.value_or_f(local, t.staticValue);
        const f32 safe = std::isfinite(v) ? v : 0.0f;
        switch (t.effectParamIndex) {
            case 0: it->euler.x = safe; break;
            case 1: it->euler.y = safe; break;
            case 2: it->euler.z = safe; break;
            case 3: it->translation.x = safe; break;
            case 4: it->translation.y = safe; break;
            default: it->translation.z = safe; break;
        }
    }
    for (BoneOffset& b : out) b.rotation = bone_quat(b.euler);
    // Motion Capture (prévia transitória): os ossos mapeados tocam a captura
    // no lugar das trilhas deles, sem gravar nada (rig/Mocap.hpp).
    if (const rig::MocapPreview* pv = rig::active_preview(layer.model.mocapPreview)) {
        for (usize c = 0; c < pv->nodes.size(); ++c) {
            const i32 node = pv->nodes[c];
            auto it = std::find_if(out.begin(), out.end(), [&](const BoneOffset& b) { return b.node == node; });
            if (it == out.end()) {
                out.push_back(BoneOffset{node, Quat::identity(), Vec3{0, 0, 0}, Vec3{0, 0, 0}});
                it = out.end() - 1;
            }
            pv->sample(local, c, it->rotation);
            it->translation = node == pv->hipsNode ? pv->sample_hips(local) : Vec3{0, 0, 0};
        }
    }
}

void ik_controllers(const Layer& layer, f64 local, std::vector<IkController>& out) {
    out.clear();
    if (rig::active_preview(layer.model.mocapPreview)) return;   // a captura já traz as mãos e os pés
    for (u32 i = 0; i < layer.tracks.size(); ++i) {
        const Track& t = layer.tracks.at(i);
        if (t.property != TrackProperty::BonePose || t.effectIndex < rig::kBoneIkBase || t.effectParamIndex > 6) continue;
        const i32 end = static_cast<i32>(t.effectIndex - rig::kBoneIkBase);
        auto it = std::find_if(out.begin(), out.end(), [&](const IkController& c) { return c.end == end; });
        if (it == out.end()) {
            out.push_back(IkController{});
            it = out.end() - 1;
            it->end = end;
        }
        const f32 v = t.value_or_f(local, t.staticValue);
        const f32 safe = std::isfinite(v) ? v : 0.0f;
        const u32 p = t.effectParamIndex;
        if (p < 3) (p == 0 ? it->target.x : p == 1 ? it->target.y : it->target.z) = safe;
        else if (p < 6) (p == 3 ? it->pole.x : p == 4 ? it->pole.y : it->pole.z) = safe;
        else it->weight = std::clamp(safe, 0.0f, 1.0f);
    }
}

bool ik_chain(const SceneAsset& asset, i32 end, i32& mid, i32& root) noexcept {
    const i32 n = static_cast<i32>(asset.nodes.size());
    if (end < 0 || end >= n) return false;
    mid = asset.nodes[static_cast<usize>(end)].parent;
    if (mid < 0 || mid >= n) return false;
    root = asset.nodes[static_cast<usize>(mid)].parent;
    return root >= 0 && root < n;
}

void evaluate_rig_pose(const SceneAsset& asset, const Layer& layer, i32 clip, f32 t, f64 local, Pose& out,
                       PoseWorkspace* workspace) {
    thread_local std::vector<BoneOffset> offsets;
    thread_local std::vector<IkController> iks;
    bone_offsets(layer, local, offsets);
    ik_controllers(layer, local, iks);
    evaluate_pose(asset, clip, t, out, workspace, offsets.data(), offsets.size());
    bool any = false;
    for (const IkController& c : iks) {
        i32 mid = -1, root = -1;
        if (!(c.weight > 0.0f) || !ik_chain(asset, c.end, mid, root)) continue;
        if (static_cast<usize>(c.end) >= out.nodeWorld.size()) continue;
        const Mat4& wr = out.nodeWorld[static_cast<usize>(root)];
        const Mat4& wm = out.nodeWorld[static_cast<usize>(mid)];
        const Mat4& we = out.nodeWorld[static_cast<usize>(c.end)];
        const rig::TwoBoneIk s = rig::solve_two_bone(rig::matrix_position(wr), rig::matrix_position(wm),
                                                     rig::matrix_position(we), c.target, c.pole, c.weight);
        // Mundo → local: o = W⁻¹·Δ·W (W = rotação de mundo do osso agora).
        const Quat rw = rig::matrix_rotation(wr);
        const Quat mw = (s.rootDelta * rig::matrix_rotation(wm)).normalized();
        offsets.push_back(BoneOffset{root, (rig::conj(rw) * s.rootDelta * rw).normalized(), Vec3{0, 0, 0}, Vec3{0, 0, 0}});
        offsets.push_back(BoneOffset{mid, (rig::conj(mw) * s.midDelta * mw).normalized(), Vec3{0, 0, 0}, Vec3{0, 0, 0}});
        any = true;
    }
    if (any) evaluate_pose(asset, clip, t, out, workspace, offsets.data(), offsets.size());
}

} // namespace aurea::scene3d
