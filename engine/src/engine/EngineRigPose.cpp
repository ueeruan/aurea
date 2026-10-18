// =============================================================================
//  Aurea / engine / EngineRigPose.cpp
//
//  Posar o rig de um modelo 3D na cena (0.1.8): a lista de ossos com a posição
//  na tela, o dedo girando o osso como uma trackball, keyframe por osso (ou de
//  todos de uma vez, como o "inserir keyframe" do Blender no modo de pose) e o
//  reset. A pose fica nas trilhas TrackProperty::BonePose (scene3d/RigPose.hpp)
//  e o renderer a aplica antes das juntas — prévia e export iguais.
// =============================================================================
#include "aurea/Engine.hpp"

#include "aurea/core/Trackball.hpp"
#include "aurea/render/Renderer.hpp"
#include "aurea/scene3d/Animation.hpp"
#include "aurea/scene3d/RigPose.hpp"

#include <algorithm>
#include <cmath>

namespace aurea {
namespace {

Layer* model_layer(Composition* comp, u64 id) noexcept {
    Layer* l = comp ? comp->layer(LayerId::unpack(id)) : nullptr;
    return l && l->kind == LayerKind::Model3D ? l : nullptr;
}

/// O mesmo `layer_from_model` do renderer (Y/Z do arquivo → composição, m → px).
Mat4 layer_from_file(const Model3DData& m) noexcept {
    Mat4 flip;
    flip.col[1] = Vec4{0, -1, 0, 0};
    flip.col[2] = Vec4{0, 0, -1, 0};
    return flip * Mat4::scale(Vec3{m.unitScale, m.unitScale, m.unitScale}) * Mat4::translation(-m.pivot);
}

/// Pose do modelo no cabeçote com a pose do usuário (o que a prévia mostra).
void current_pose(const Composition& comp, const Layer& l, const scene3d::SceneAsset& asset, FrameIndex now,
                  scene3d::Pose& pose) {
    const f64 fps = comp.fps() > 0.0 ? comp.fps() : 30.0;
    const f64 local = static_cast<f64>(l.local_time(now).value);
    const i32 clip = l.model.animationClip >= 0 && l.model.animationClip < static_cast<i32>(asset.animations.size())
        ? l.model.animationClip : -1;
    const f32 t = clip >= 0 ? scene3d::clip_time(asset.animations[static_cast<usize>(clip)], local / fps * l.model.timeScale) : 0.0f;
    std::vector<scene3d::BoneOffset> offsets;
    scene3d::bone_offsets(l, local, offsets);
    scene3d::evaluate_pose(asset, clip, t, pose, nullptr, offsets.data(), offsets.size());
}

/// Rotação de pose do osso no tempo local (graus; 0 sem trilha).
Vec3 bone_euler(const Layer& l, u32 node, FrameIndex local) noexcept {
    Vec3 e{0, 0, 0};
    for (u32 c = 0; c < 3; ++c) {
        const Track* t = l.tracks.find(TrackProperty::BonePose, node, c);
        const f32 v = t ? t->value_or(local, t->staticValue) : 0.0f;
        (c == 0 ? e.x : c == 1 ? e.y : e.z) = std::isfinite(v) ? v : 0.0f;
    }
    return e;
}

Quat frame_rotation(const Mat4& m) noexcept {
    const f32 f[9]{m.col[0].x, m.col[0].y, m.col[0].z, m.col[1].x, m.col[1].y, m.col[1].z, m.col[2].x, m.col[2].y, m.col[2].z};
    const trackball::detail::DQ q = trackball::detail::frame_quat(f);
    return Quat{static_cast<f32>(q.x), static_cast<f32>(q.y), static_cast<f32>(q.z), static_cast<f32>(q.w)}.normalized();
}

Vec3 rotate(const Quat& q, Vec3 v) noexcept {
    return Mat4::from_quat(q).transform_point(v) - Mat4::from_quat(q).transform_point(Vec3{0, 0, 0});
}

} // namespace

u32 Engine::query_model_bones(u64 layerId, f32* out, u32 capacity) noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    Composition* comp = project_ ? current_composition() : nullptr;
    const Layer* l = model_layer(comp, layerId);
    if (!l) return 0;
    const auto it = models_.find(l->model.scene.pack());
    if (it == models_.end() || !it->second) return 0;
    const scene3d::SceneAsset& asset = *it->second;
    const std::vector<i32> bones = scene3d::pose_bones(asset);
    const u32 need = static_cast<u32>(bones.size()) * kBoneFloats;
    if (!out || capacity < need || bones.empty()) return need;
    const FrameIndex now = playback_.current();
    const FrameIndex local = l->local_time(now);
    scene3d::Pose pose;
    current_pose(*comp, *l, asset, now, pose);
    const Mat4 world = layer_world_3d(*comp, *l, now) * layer_from_file(l->model);
    const Mat4 vp = sceneEditor_.enabled ? scene_editor_projection(comp->width(), comp->height(), sceneEditor_)
                                         : comp_view_projection(*comp, now);
    for (usize b = 0; b < bones.size(); ++b) {
        const i32 node = bones[b];
        f32* o = out + b * kBoneFloats;
        o[0] = static_cast<f32>(node);
        // Pai na lista: sobe pela hierarquia até achar outra junta.
        i32 parentSlot = -1;
        for (i32 p = asset.nodes[static_cast<usize>(node)].parent; p >= 0 && parentSlot < 0;
             p = asset.nodes[static_cast<usize>(p)].parent) {
            const auto found = std::find(bones.begin(), bones.end(), p);
            if (found != bones.end()) parentSlot = static_cast<i32>(found - bones.begin());
        }
        o[1] = static_cast<f32>(parentSlot);
        const Vec3 e = bone_euler(*l, static_cast<u32>(node), local);
        o[2] = e.x; o[3] = e.y; o[4] = e.z;
        u32 animated = 0, here = 0;
        for (u32 c = 0; c < 3; ++c) {
            const Track* t = l->tracks.find(TrackProperty::BonePose, static_cast<u32>(node), c);
            if (!t || t->keys.empty()) continue;
            animated |= 1u << c;
            if (t->find_exact(local) != kInvalidIndex) here = 1;
        }
        o[5] = static_cast<f32>(animated);
        o[6] = static_cast<f32>(here);
        const Mat4& nw = static_cast<usize>(node) < pose.nodeWorld.size() ? pose.nodeWorld[static_cast<usize>(node)] : Mat4::identity();
        const Vec3 wp = world.transform_point(Vec3{nw.col[3].x, nw.col[3].y, nw.col[3].z});
        const Vec4 clip = vp * Vec4{wp.x, wp.y, wp.z, 1};
        const bool front = clip.w > 1e-6f;
        o[7] = front ? clip.x / clip.w : 0.0f;
        o[8] = front ? clip.y / clip.w : 0.0f;
        o[9] = front ? 1.0f : 0.0f;
    }
    return need;
}

bool Engine::drag_bone(u64 layerId, u32 node, f32 dxPx, f32 dyPx, f32 twistDeg, bool continuing) noexcept {
    if (!std::isfinite(dxPx) || !std::isfinite(dyPx) || !std::isfinite(twistDeg)) return false;
    std::lock_guard<std::mutex> lock(modelMutex_);
    Composition* comp = project_ ? current_composition() : nullptr;
    Layer* l = model_layer(comp, layerId);
    if (!l || l->locked) return false;
    const auto it = models_.find(l->model.scene.pack());
    if (it == models_.end() || !it->second || node >= it->second->nodes.size()) return false;
    const scene3d::SceneAsset& asset = *it->second;
    const std::vector<i32> bones = scene3d::pose_bones(asset);
    if (std::find(bones.begin(), bones.end(), static_cast<i32>(node)) == bones.end()) return false;
    const FrameIndex now = playback_.current();
    const FrameIndex local = l->local_time(now);
    const bool inside = l->contains_time(now);
    for (u32 c = 0; c < 3; ++c) {
        const Track* t = l->tracks.find(TrackProperty::BonePose, node, c);
        if (t && !t->keys.empty() && !inside) return false;   // animado fora da camada: sem onde gravar
    }
    // Eixos da vista no espaço do arquivo (x direita, y para baixo, z para onde olha).
    const u32 w = comp->width(), h = comp->height();
    const scene3d::SceneCamera cam = sceneEditor_.enabled ? scene_editor_camera(w, h, sceneEditor_) : comp_camera(*comp, now, w, h);
    const Mat4& v = cam.view;
    const Vec3 vx{v.col[0].x, v.col[1].x, v.col[2].x}, vy{v.col[0].y, v.col[1].y, v.col[2].y}, vz{v.col[0].z, v.col[1].z, v.col[2].z};
    const Mat4 world = layer_world_3d(*comp, *l, now) * layer_from_file(l->model);
    auto to_model = [&](Vec3 d) {
        // Semelhança (rotação × escala): a inversa da parte linear é a transposta, a menos da escala.
        const Vec3 r{world.col[0].x * d.x + world.col[0].y * d.y + world.col[0].z * d.z,
                     world.col[1].x * d.x + world.col[1].y * d.y + world.col[1].z * d.z,
                     world.col[2].x * d.x + world.col[2].y * d.y + world.col[2].z * d.z};
        return r.normalized();
    };
    scene3d::Pose pose;
    current_pose(*comp, *l, asset, now, pose);
    if (node >= pose.nodeWorld.size()) return false;
    const Quat boneWorld = frame_rotation(pose.nodeWorld[node]);
    const Quat toBone{-boneWorld.x, -boneWorld.y, -boneWorld.z, boneWorld.w};
    // Trackball: arrastar para a direita gira em volta do "para cima" da vista;
    // para baixo, em volta do "direita"; o giro de dois dedos, em volta do olhar.
    constexpr f32 kDegPerPx = 0.45f;
    constexpr f32 kRad = 3.14159265358979323846f / 180.0f;
    Quat delta = Quat::identity();
    const f32 len = std::sqrt(dxPx * dxPx + dyPx * dyPx);
    if (len > 1e-4f) {
        const Vec3 axisView = (vx * (dyPx / len) + vy * (-dxPx / len));
        const Vec3 axisBone = rotate(toBone, to_model(axisView)).normalized();
        if (axisBone.length_sq() > 0.5f) delta = Quat::from_axis_angle(axisBone, len * kDegPerPx * kRad);
    }
    if (std::fabs(twistDeg) > 1e-4f) {
        const Vec3 axisBone = rotate(toBone, to_model(vz)).normalized();
        if (axisBone.length_sq() > 0.5f) delta = Quat::from_axis_angle(axisBone, twistDeg * kRad) * delta;
    }
    const Vec3 before = bone_euler(*l, node, local);
    const Quat next = (scene3d::bone_quat(before) * delta).normalized();
    const Vec3 e = trackball::euler_deg_from_quat(next, before);
    if (!continuing) history_.before_mutation(*comp, project_->timeline().current(), "posar osso");
    bump_model_revision_locked();
    const f32 values[3]{e.x, e.y, e.z};
    for (u32 c = 0; c < 3; ++c) {
        if (!std::isfinite(values[c])) continue;
        Track& t = l->tracks.get_or_create(TrackProperty::BonePose, node, c);
        if (t.keys.empty()) t.staticValue = values[c];
        else (void)t.set(local, values[c]);
    }
    project_->mark_dirty();
    request_render();
    return true;
}

i32 Engine::toggle_bone_key(u64 layerId, i32 node) noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    Composition* comp = project_ ? current_composition() : nullptr;
    Layer* l = model_layer(comp, layerId);
    if (!l || l->locked) return -1;
    const auto it = models_.find(l->model.scene.pack());
    if (it == models_.end() || !it->second) return -1;
    std::vector<i32> bones = scene3d::pose_bones(*it->second);
    if (node >= 0) {
        if (std::find(bones.begin(), bones.end(), node) == bones.end()) return -1;
        bones = {node};
    }
    if (bones.empty()) return -1;
    const FrameIndex now = playback_.current();
    if (!l->contains_time(now)) return -1;
    const FrameIndex local = l->local_time(now);
    bool any = false;
    for (const i32 b : bones)
        for (u32 c = 0; c < 3; ++c) {
            const Track* t = l->tracks.find(TrackProperty::BonePose, static_cast<u32>(b), c);
            any = any || (t && t->find_exact(local) != kInvalidIndex);
        }
    history_.before_mutation(*comp, project_->timeline().current(), any ? "tirar keyframe da pose" : "keyframe da pose");
    bump_model_revision_locked();
    for (const i32 b : bones) {
        const Vec3 cur = bone_euler(*l, static_cast<u32>(b), local);
        const f32 values[3]{cur.x, cur.y, cur.z};
        for (u32 c = 0; c < 3; ++c) {
            if (any) {
                Track* t = l->tracks.find(TrackProperty::BonePose, static_cast<u32>(b), c);
                if (!t || t->find_exact(local) == kInvalidIndex) continue;
                (void)t->remove(local);
                // Sem key nenhuma, o osso fica onde estava (o valor parado = o de agora).
                if (t->keys.empty()) t->staticValue = values[c];
            } else {
                Track& t = l->tracks.get_or_create(TrackProperty::BonePose, static_cast<u32>(b), c);
                if (t.keys.empty()) t.staticValue = values[c];
                (void)t.set(local, values[c]);
            }
        }
    }
    project_->mark_dirty();
    request_render();
    return any ? 0 : 1;
}

bool Engine::reset_bone_pose(u64 layerId, i32 node) noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    Composition* comp = project_ ? current_composition() : nullptr;
    Layer* l = model_layer(comp, layerId);
    if (!l || l->locked) return false;
    bool has = false;
    for (u32 i = 0; i < l->tracks.size(); ++i) {
        const Track& t = l->tracks.at(i);
        has = has || (t.property == TrackProperty::BonePose && (node < 0 || t.effectIndex == static_cast<u32>(node)));
    }
    if (!has) return false;
    history_.before_mutation(*comp, project_->timeline().current(), node < 0 ? "resetar pose" : "resetar osso");
    bump_model_revision_locked();
    l->tracks.remove_if([&](const Track& t) {
        return t.property == TrackProperty::BonePose && (node < 0 || t.effectIndex == static_cast<u32>(node));
    });
    project_->mark_dirty();
    request_render();
    return true;
}

} // namespace aurea
