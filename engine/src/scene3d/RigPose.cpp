// Pose do rig 3D pelo usuário (ver RigPose.hpp).
#include "aurea/scene3d/RigPose.hpp"

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
        if (t.property != TrackProperty::BonePose || t.effectParamIndex > 2 || t.effectIndex >= (1u << 24)) continue;
        const i32 node = static_cast<i32>(t.effectIndex);
        auto it = std::find_if(out.begin(), out.end(), [&](const BoneOffset& b) { return b.node == node; });
        if (it == out.end()) {
            out.push_back(BoneOffset{node, Quat::identity(), Vec3{0, 0, 0}});
            it = out.end() - 1;
        }
        const f32 v = t.value_or_f(local, t.staticValue);
        const f32 safe = std::isfinite(v) ? v : 0.0f;
        if (t.effectParamIndex == 0) it->euler.x = safe;
        else if (t.effectParamIndex == 1) it->euler.y = safe;
        else it->euler.z = safe;
    }
    for (BoneOffset& b : out) b.rotation = bone_quat(b.euler);
}

} // namespace aurea::scene3d
