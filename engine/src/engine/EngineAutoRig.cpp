// =============================================================================
//  Aurea / engine / EngineAutoRig.cpp
//
//  AutoRig 3D (0.3.6 BETA): o comando assíncrono (thread própria, progresso e
//  cancelar), "Corrigir esqueleto" (mover junta / reatribuir osso), o IK de
//  dois ossos como controlador animável, a translação do quadril e as keys de
//  osso em lote do Motion Capture. Tudo nas trilhas BonePose da camada
//  (rig/Humanoid.hpp) — desfazer, curvas, prévia e export são os de sempre.
// =============================================================================
#include "aurea/Engine.hpp"

#include "aurea/render/Renderer.hpp"
#include "aurea/rig/AutoRig.hpp"
#include "aurea/rig/Ik.hpp"
#include "aurea/scene3d/Animation.hpp"
#include "aurea/scene3d/ModelBudget.hpp"
#include "aurea/scene3d/RigPose.hpp"

#include <algorithm>
#include <cmath>
#include <optional>
#include <thread>

namespace aurea {

struct Engine::AutoRigJob {
    std::thread thread;
    std::atomic<bool> cancel{false}, finished{true};
    std::atomic<i32> state{0}, error{0};
    std::atomic<f32> progress{0.0f};
    u64 layer = 0;
};

namespace {

using rig::HumanoidBone;

Layer* rig_layer(Composition* comp, u64 id) noexcept {
    Layer* l = comp ? comp->layer(LayerId::unpack(id)) : nullptr;
    return l && l->kind == LayerKind::Model3D ? l : nullptr;
}

/// O `layer_from_model` do renderer (Y/Z do arquivo → composição, m → px).
Mat4 file_to_layer(const Model3DData& m) noexcept {
    Mat4 flip;
    flip.col[1] = Vec4{0, -1, 0, 0};
    flip.col[2] = Vec4{0, 0, -1, 0};
    return flip * Mat4::scale(Vec3{m.unitScale, m.unitScale, m.unitScale}) * Mat4::translation(-m.pivot);
}

void posed(const Composition& comp, const Layer& l, const scene3d::SceneAsset& asset, FrameIndex now, scene3d::Pose& pose) {
    const f64 fps = comp.fps() > 0.0 ? comp.fps() : 30.0;
    const f64 local = static_cast<f64>(l.local_time(now).value);
    const i32 clip = l.model.animationClip >= 0 && l.model.animationClip < static_cast<i32>(asset.animations.size())
        ? l.model.animationClip : -1;
    const f32 t = clip >= 0 ? scene3d::clip_time(asset.animations[static_cast<usize>(clip)], local / fps * l.model.timeScale) : 0.0f;
    scene3d::evaluate_rig_pose(asset, l, clip, t, local, pose);
}

bool is_recipe(const std::string& src) noexcept {
    scene3d::Text3DSpec spec;
    return src.empty() || scene3d::decode_text3d(src, spec) || scene3d::is_shape3d_source(src);
}

bool is_auto_spec(const std::string& spec) noexcept { return spec.rfind("arig1 a", 0) == 0; }

/// Tira o rig do AutoRig (nós "aurea:*" no fim, skins, pesos): volta ao
/// modelo do arquivo para recalcular com juntas novas.
void strip_autorig(scene3d::SceneAsset& a) {
    usize first = a.nodes.size();
    for (usize i = 0; i < a.nodes.size(); ++i)
        if (a.nodes[i].name.rfind(rig::kAutoRigNodePrefix, 0) == 0) { first = i; break; }
    a.nodes.resize(first);
    a.roots.erase(std::remove_if(a.roots.begin(), a.roots.end(), [&](i32 r) { return r < 0 || static_cast<usize>(r) >= first; }),
                  a.roots.end());
    for (scene3d::Node& n : a.nodes) {
        n.skin = -1;
        n.children.erase(std::remove_if(n.children.begin(), n.children.end(), [&](i32 c) { return c < 0 || static_cast<usize>(c) >= first; }),
                         n.children.end());
    }
    a.skins.clear();
    for (scene3d::Mesh& m : a.meshes)
        for (scene3d::Primitive& p : m.primitives) { p.joints.clear(); p.weights.clear(); }
}

/// Membro do IK → (ponta, meio canônico).
bool limb_end(u32 limb, HumanoidBone& end, HumanoidBone& mid) noexcept {
    switch (limb) {
        case 0: end = HumanoidBone::LeftHand; mid = HumanoidBone::LeftLowerArm; return true;
        case 1: end = HumanoidBone::RightHand; mid = HumanoidBone::RightLowerArm; return true;
        case 2: end = HumanoidBone::LeftFoot; mid = HumanoidBone::LeftLowerLeg; return true;
        case 3: end = HumanoidBone::RightFoot; mid = HumanoidBone::RightLowerLeg; return true;
        default: return false;
    }
}

Vec3 ik_value(const Layer& l, u32 effect, u32 first, FrameIndex local) noexcept {
    Vec3 v{0, 0, 0};
    for (u32 c = 0; c < 3; ++c) {
        const Track* t = l.tracks.find(TrackProperty::BonePose, effect, first + c);
        const f32 x = t ? t->value_or(local, t->staticValue) : 0.0f;
        (c == 0 ? v.x : c == 1 ? v.y : v.z) = std::isfinite(x) ? x : 0.0f;
    }
    return v;
}

/// Grava um valor: com keyframes, key no cabeçote; sem, o valor parado.
void write_channel(Layer& l, u32 effect, u32 param, FrameIndex local, f32 value) {
    if (!std::isfinite(value)) return;
    Track& t = l.tracks.get_or_create(TrackProperty::BonePose, effect, param);
    if (t.keys.empty()) t.staticValue = value;
    else (void)t.set(local, value);
}

/// Ponto da tela (px da composição) → espaço do modelo, na profundidade de `ref`.
bool unproject(const Mat4& clipFromModel, Vec3 ref, f32 sx, f32 sy, Vec3& out) noexcept {
    const Vec4 c = clipFromModel * Vec4{ref, 1.0f};
    if (!(c.w > 1e-6f)) return false;
    Mat4 inv;
    if (!rig::inverse(clipFromModel, inv)) return false;
    const Vec4 q = inv * Vec4{sx, sy, c.z / c.w, 1.0f};
    if (std::fabs(q.w) < 1e-12f) return false;
    out = Vec3{q.x / q.w, q.y / q.w, q.z / q.w};
    return std::isfinite(out.x) && std::isfinite(out.y) && std::isfinite(out.z);
}

bool to_screen(const Mat4& clipFromModel, Vec3 p, f32& sx, f32& sy) noexcept {
    const Vec4 c = clipFromModel * Vec4{p, 1.0f};
    if (!(c.w > 1e-6f)) { sx = sy = 0.0f; return false; }
    sx = c.x / c.w;
    sy = c.y / c.w;
    return true;
}

} // namespace

// -----------------------------------------------------------------------------
// Estado do rig
// -----------------------------------------------------------------------------

i32 Engine::humanoid_status(u64 layerId) noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    Composition* comp = project_ ? current_composition() : nullptr;
    const Layer* l = rig_layer(comp, layerId);
    if (!l) return 0;
    const auto it = models_.find(l->model.scene.pack());
    const Asset* a = project_->asset(l->model.scene);
    if (it == models_.end() || !it->second || !a) return 0;
    if (is_auto_spec(a->model.rig)) return 3;
    if (rig::humanoid_map(*it->second, a->model.rig).usable()) return 2;
    return it->second->skins.empty() ? 0 : 1;
}

bool Engine::query_humanoid(u64 layerId, rig::HumanoidRig& out) noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    Composition* comp = project_ ? current_composition() : nullptr;
    const Layer* l = rig_layer(comp, layerId);
    if (!l) return false;
    const auto it = models_.find(l->model.scene.pack());
    const Asset* a = project_->asset(l->model.scene);
    if (it == models_.end() || !it->second || !a) return false;
    out.map = rig::humanoid_map(*it->second, a->model.rig);
    out.rest = rig::rest_pose(*it->second, out.map);
    out.autoRig = is_auto_spec(a->model.rig);
    out.compatible = out.map.usable();
    return out.compatible;
}

std::string Engine::model_node_name(u64 layerId, i32 node) noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    Composition* comp = project_ ? current_composition() : nullptr;
    const Layer* l = rig_layer(comp, layerId);
    if (!l || node < 0) return {};
    const auto it = models_.find(l->model.scene.pack());
    if (it == models_.end() || !it->second || static_cast<usize>(node) >= it->second->nodes.size()) return {};
    return it->second->nodes[static_cast<usize>(node)].name;
}

// -----------------------------------------------------------------------------
// AutoRig assíncrono
// -----------------------------------------------------------------------------

void Engine::join_autorig() noexcept {
    if (!autoRig_) return;
    autoRig_->cancel.store(true);
    if (autoRig_->thread.joinable()) autoRig_->thread.join();
}

bool Engine::autorig_start(u64 layerId) noexcept { return start_autorig_job(layerId, nullptr); }

Engine::AutoRigProgress Engine::autorig_progress() noexcept {
    AutoRigProgress p;
    const auto j = autoRig_;
    if (!j) return p;
    p.state = j->state.load();
    p.progress = j->progress.load();
    p.error = j->error.load();
    p.layer = j->layer;
    return p;
}

void Engine::autorig_cancel() noexcept {
    const auto j = autoRig_;
    if (!j) return;
    j->cancel.store(true);
    if (j->state.load() == 1) j->state.store(4);
}

bool Engine::start_autorig_job(u64 layerId, const rig::AutoRigJoints* edit) noexcept {
    if (autoRig_ && !autoRig_->finished.load() && !autoRig_->cancel.load()) return false;   // um por vez
    join_autorig();
    auto job = std::make_shared<AutoRigJob>();
    job->layer = layerId;
    auto fail = [&](rig::AutoRigError e) {
        job->error.store(static_cast<i32>(e));
        job->state.store(3);
        autoRig_ = job;
        return false;
    };
    std::shared_ptr<const scene3d::SceneAsset> source;
    Asset copy;
    AssetId old{};
    u64 session = 0;
    CompositionId composition{};
    {
        std::lock_guard<std::mutex> lock(modelMutex_);
        Composition* comp = project_ ? current_composition() : nullptr;
        const Layer* l = rig_layer(comp, layerId);
        if (!l || l->locked) return fail(rig::AutoRigError::Failed);
        const Asset* a = project_->asset(l->model.scene);
        const auto it = models_.find(l->model.scene.pack());
        if (!a || it == models_.end() || !it->second) return fail(rig::AutoRigError::Failed);
        if (is_recipe(a->sourcePath)) return fail(rig::AutoRigError::NotImported);
        // Esqueleto do arquivo: nunca substitui (rig compatível ou "Corrigir").
        if (!is_auto_spec(a->model.rig) && rig::has_skin(*it->second)) return fail(rig::AutoRigError::AlreadyRigged);
        if (edit && !is_auto_spec(a->model.rig)) return fail(rig::AutoRigError::Failed);
        source = it->second;
        copy = *a;
        old = l->model.scene;
        session = projectSession_;
        composition = project_->timeline().current();
    }
    std::optional<rig::AutoRigJoints> joints;
    if (edit) joints = *edit;
    job->state.store(1);
    job->finished.store(false);
    autoRig_ = job;
    // Ponteiro cru: o Engine é dono do job e o junta antes de soltar.
    job->thread = std::thread([this, j = job.get(), source, copy, old, session, composition, joints, layerId]() mutable {
        struct Done { AutoRigJob* j; ~Done() { j->finished.store(true); } } done{j};
        auto finish = [&](rig::AutoRigError e) {
            j->error.store(static_cast<i32>(e));
            j->state.store(e == rig::AutoRigError::None ? 2 : e == rig::AutoRigError::Cancelled ? 4 : 3);
            if (e == rig::AutoRigError::None) j->progress.store(1.0f);
        };
        rig::AutoRigControl ctl{&j->progress, &j->cancel};
        auto work = std::make_shared<scene3d::SceneAsset>(*source);
        const bool rigged = is_auto_spec(copy.model.rig);
        if (rigged) strip_autorig(*work);
        rig::AutoRigJoints pts;
        if (joints) {
            pts = *joints;
        } else {
            const rig::AutoRigError e = rig::detect_tpose(*work, pts, ctl);
            if (e != rig::AutoRigError::None) return finish(j->cancel.load() ? rig::AutoRigError::Cancelled : e);
        }
        if (const rig::AutoRigError e = rig::build_rigged_asset(*work, pts, ctl); e != rig::AutoRigError::None)
            return finish(j->cancel.load() ? rig::AutoRigError::Cancelled : e);
        if (j->cancel.load()) return finish(rig::AutoRigError::Cancelled);
        std::lock_guard<std::mutex> lock(modelMutex_);
        Composition* comp = project_ && projectSession_ == session ? current_composition() : nullptr;
        if (!comp || project_->timeline().current() != composition) return finish(rig::AutoRigError::Cancelled);
        Layer* l = rig_layer(comp, layerId);
        if (!l || l->locked || l->model.scene != old) return finish(rig::AutoRigError::Cancelled);
        if (scene3d::scene_asset_memory_bytes(*work) > source_asset_room_locked() + scene3d::scene_asset_memory_bytes(*source))
            return finish(rig::AutoRigError::TooHeavy);
        history_.before_mutation(*comp, project_->timeline().current(), joints ? "corrigir esqueleto" : "AutoRig 3D");
        bump_model_revision_locked();
        copy.model.rig = rig::encode_autorig_spec(pts);
        copy.model.hasSkeleton = true;
        // Asset novo (o id novo sobe a malha com skin na GPU); desfazer volta
        // a camada para o asset antigo, que continua no projeto.
        const AssetId fresh = project_->add_asset(std::move(copy));
        models_[fresh.pack()] = std::shared_ptr<const scene3d::SceneAsset>(std::move(work));
        l->model.scene = fresh;
        project_->mark_dirty();
        request_render();
        finish(rig::AutoRigError::None);
    });
    return true;
}

// -----------------------------------------------------------------------------
// Corrigir esqueleto
// -----------------------------------------------------------------------------

u32 Engine::query_humanoid_joints(u64 layerId, f32* out, u32 capacity) noexcept {
    constexpr u32 need = static_cast<u32>(rig::kHumanoidBoneCount) * kHumanoidJointFloats;
    std::lock_guard<std::mutex> lock(modelMutex_);
    Composition* comp = project_ ? current_composition() : nullptr;
    const Layer* l = rig_layer(comp, layerId);
    if (!l) return 0;
    const auto it = models_.find(l->model.scene.pack());
    const Asset* a = project_->asset(l->model.scene);
    if (it == models_.end() || !it->second || !a || it->second->skins.empty()) return 0;
    if (!out || capacity < need) return need;
    const scene3d::SceneAsset& asset = *it->second;
    const rig::HumanoidMap map = rig::humanoid_map(asset, a->model.rig);
    const rig::HumanoidRest rest = rig::rest_pose(asset, map);
    const FrameIndex now = playback_.current();
    scene3d::Pose pose;
    posed(*comp, *l, asset, now, pose);
    const Mat4 world = layer_world_3d(*comp, *l, now) * file_to_layer(l->model);
    const Mat4 vp = sceneEditor_.enabled ? scene_editor_projection(comp->width(), comp->height(), sceneEditor_)
                                         : comp_view_projection(*comp, now);
    const Mat4 clipFromModel = vp * world;
    for (usize b = 0; b < rig::kHumanoidBoneCount; ++b) {
        f32* o = out + b * kHumanoidJointFloats;
        const i32 node = map.joint[b];
        o[0] = static_cast<f32>(node);
        o[1] = o[2] = o[3] = 0.0f;
        o[4] = rest.position[b].x; o[5] = rest.position[b].y; o[6] = rest.position[b].z;
        const HumanoidBone p = rig::parent_of(static_cast<HumanoidBone>(b));
        o[7] = p == HumanoidBone::Count ? -1.0f : static_cast<f32>(p);
        if (node < 0 || static_cast<usize>(node) >= pose.nodeWorld.size()) continue;
        o[3] = to_screen(clipFromModel, rig::matrix_position(pose.nodeWorld[static_cast<usize>(node)]), o[1], o[2]) ? 1.0f : 0.0f;
    }
    return need;
}

bool Engine::humanoid_move_joint(u64 layerId, u32 bone, f32 compX, f32 compY) noexcept {
    if (bone >= rig::kHumanoidBoneCount || !std::isfinite(compX) || !std::isfinite(compY)) return false;
    rig::AutoRigJoints joints;
    {
        std::lock_guard<std::mutex> lock(modelMutex_);
        Composition* comp = project_ ? current_composition() : nullptr;
        const Layer* l = rig_layer(comp, layerId);
        if (!l || l->locked) return false;
        const auto it = models_.find(l->model.scene.pack());
        const Asset* a = project_->asset(l->model.scene);
        if (it == models_.end() || !it->second || !a || !is_auto_spec(a->model.rig)) return false;
        bool isAuto = false;
        rig::HumanoidMap unused;
        if (!rig::decode_rig_spec(a->model.rig, isAuto, joints, unused) || !isAuto) return false;
        const scene3d::SceneAsset& asset = *it->second;
        const rig::HumanoidMap map = rig::humanoid_map(asset, a->model.rig);
        const i32 node = map.joint[bone];
        if (node < 0) return false;
        const FrameIndex now = playback_.current();
        scene3d::Pose pose;
        posed(*comp, *l, asset, now, pose);
        if (static_cast<usize>(node) >= pose.nodeWorld.size()) return false;
        const Mat4 world = layer_world_3d(*comp, *l, now) * file_to_layer(l->model);
        const Mat4 vp = sceneEditor_.enabled ? scene_editor_projection(comp->width(), comp->height(), sceneEditor_)
                                             : comp_view_projection(*comp, now);
        const Mat4& posedWorld = pose.nodeWorld[static_cast<usize>(node)];
        Vec3 target;
        if (!unproject(vp * world, rig::matrix_position(posedWorld), compX, compY, target)) return false;
        // Da pose de agora para o repouso: o que a junta carrega (restW · posedW⁻¹).
        const std::vector<Mat4> restWorld = asset.rest_world_matrices();
        Mat4 invPosed;
        if (!rig::inverse(posedWorld, invPosed)) return false;
        const Vec3 rest = (restWorld[static_cast<usize>(node)] * invPosed).transform_point(target);
        const Vec3 delta = rest - joints.position[bone];
        joints.position[bone] = rest;
        // As pontas dos ossos-folha andam junto.
        switch (static_cast<HumanoidBone>(bone)) {
            case HumanoidBone::Head: joints.headTop = joints.headTop + delta; break;
            case HumanoidBone::LeftHand: joints.leftHandTip = joints.leftHandTip + delta; break;
            case HumanoidBone::RightHand: joints.rightHandTip = joints.rightHandTip + delta; break;
            case HumanoidBone::LeftFoot: joints.leftToe = joints.leftToe + delta; break;
            case HumanoidBone::RightFoot: joints.rightToe = joints.rightToe + delta; break;
            default: break;
        }
    }
    return start_autorig_job(layerId, &joints);
}

bool Engine::humanoid_assign_bone(u64 layerId, u32 bone, i32 node) noexcept {
    if (bone >= rig::kHumanoidBoneCount) return false;
    std::lock_guard<std::mutex> lock(modelMutex_);
    Composition* comp = project_ ? current_composition() : nullptr;
    Layer* l = rig_layer(comp, layerId);
    if (!l || l->locked) return false;
    const auto it = models_.find(l->model.scene.pack());
    const Asset* a = project_->asset(l->model.scene);
    if (it == models_.end() || !it->second || !a || is_auto_spec(a->model.rig)) return false;
    const scene3d::SceneAsset& asset = *it->second;
    if (node >= 0) {
        const std::vector<i32> bones = scene3d::pose_bones(asset);
        if (std::find(bones.begin(), bones.end(), node) == bones.end()) return false;
    }
    rig::HumanoidMap map = rig::humanoid_map(asset, a->model.rig);
    if (map.joint[bone] == node) return false;
    // O nó deixa o osso canônico onde estava: um nó, um osso.
    for (i32& j : map.joint) if (node >= 0 && j == node) j = -1;
    map.joint[bone] = node;
    Asset copy = *a;
    copy.model.rig = rig::encode_map_spec(map);
    history_.before_mutation(*comp, project_->timeline().current(), "corrigir esqueleto");
    bump_model_revision_locked();
    const AssetId fresh = project_->add_asset(std::move(copy));
    models_[fresh.pack()] = it->second;
    l->model.scene = fresh;
    project_->mark_dirty();
    request_render();
    return true;
}

// -----------------------------------------------------------------------------
// Keys em lote (Motion Capture) e translação do quadril
// -----------------------------------------------------------------------------

Status Engine::set_bone_keys(u64 layerId, const rig::BoneKey* keys, usize count, const char* undoLabel,
                             i64 clearFrom, i64 clearTo) noexcept {
    if (!keys && count) return Status{Errc::InvalidArgument};
    std::lock_guard<std::mutex> lock(modelMutex_);
    Composition* comp = project_ ? current_composition() : nullptr;
    Layer* l = rig_layer(comp, layerId);
    if (!l) return Status{Errc::InvalidArgument, "selecione um modelo 3D"};
    if (l->locked) return Status{Errc::InvalidState, "camada bloqueada"};
    const auto it = models_.find(l->model.scene.pack());
    if (it == models_.end() || !it->second) return Status{Errc::InvalidState, "modelo nao carregado"};
    const usize nodes = it->second->nodes.size();
    for (usize i = 0; i < count; ++i)
        if (keys[i].node < 0 || static_cast<usize>(keys[i].node) >= nodes) return Status{Errc::InvalidArgument, "osso invalido"};
    history_.before_mutation(*comp, project_->timeline().current(), undoLabel && *undoLabel ? undoLabel : "keys de ossos");
    bump_model_revision_locked();
    if (clearFrom <= clearTo) {
        std::vector<i32> touched;
        for (usize i = 0; i < count; ++i) touched.push_back(keys[i].node);
        std::sort(touched.begin(), touched.end());
        touched.erase(std::unique(touched.begin(), touched.end()), touched.end());
        for (const i32 n : touched)
            for (u32 c = 0; c < 6; ++c)
                if (Track* t = l->tracks.find(TrackProperty::BonePose, static_cast<u32>(n), c)) {
                    std::vector<FrameIndex> gone;
                    for (const auto& k : t->keys)
                        if (k.time.value >= clearFrom && k.time.value <= clearTo) gone.push_back(k.time);
                    for (const FrameIndex g : gone) (void)t->remove(g);
                }
    }
    for (usize i = 0; i < count; ++i) {
        const rig::BoneKey& k = keys[i];
        const FrameIndex at{k.frame};
        const f32 v[6]{k.eulerDeg.x, k.eulerDeg.y, k.eulerDeg.z, k.translation.x, k.translation.y, k.translation.z};
        const u32 channels = k.hasTranslation ? 6u : 3u;
        for (u32 c = 0; c < channels; ++c) {
            if (!std::isfinite(v[c])) continue;
            Track& t = l->tracks.get_or_create(TrackProperty::BonePose, static_cast<u32>(k.node), c);
            (void)t.set(at, v[c]);
        }
    }
    project_->mark_dirty();
    request_render();
    return OkStatus;
}

bool Engine::drag_bone_translate(u64 layerId, u32 node, f32 dxPx, f32 dyPx, bool continuing) noexcept {
    if (!std::isfinite(dxPx) || !std::isfinite(dyPx)) return false;
    std::lock_guard<std::mutex> lock(modelMutex_);
    Composition* comp = project_ ? current_composition() : nullptr;
    Layer* l = rig_layer(comp, layerId);
    if (!l || l->locked) return false;
    const auto it = models_.find(l->model.scene.pack());
    if (it == models_.end() || !it->second || node >= it->second->nodes.size()) return false;
    const scene3d::SceneAsset& asset = *it->second;
    const FrameIndex now = playback_.current();
    const FrameIndex local = l->local_time(now);
    for (u32 c = 3; c < 6; ++c) {
        const Track* t = l->tracks.find(TrackProperty::BonePose, node, c);
        if (t && !t->keys.empty() && !l->contains_time(now)) return false;
    }
    scene3d::Pose pose;
    posed(*comp, *l, asset, now, pose);
    if (node >= pose.nodeWorld.size()) return false;
    const Mat4 world = layer_world_3d(*comp, *l, now) * file_to_layer(l->model);
    const Mat4 vp = sceneEditor_.enabled ? scene_editor_projection(comp->width(), comp->height(), sceneEditor_)
                                         : comp_view_projection(*comp, now);
    const Vec3 p = rig::matrix_position(pose.nodeWorld[node]);
    f32 sx = 0, sy = 0;
    Vec3 q;
    if (!to_screen(vp * world, p, sx, sy) || !unproject(vp * world, p, sx + dxPx, sy + dyPx, q)) return false;
    // Deslocamento no modelo → unidades do pai (os params 3..5 somam na translação local).
    Mat4 parentLin = Mat4::identity();
    const i32 parent = asset.nodes[node].parent;
    if (parent >= 0 && static_cast<usize>(parent) < pose.nodeWorld.size()) parentLin = pose.nodeWorld[static_cast<usize>(parent)];
    parentLin.col[3] = Vec4{0, 0, 0, 1};
    Mat4 inv;
    if (!rig::inverse(parentLin, inv)) return false;
    const Vec4 d = inv * Vec4{q - p, 0.0f};
    const Vec3 before = ik_value(*l, node, 3, local);
    if (!continuing) history_.before_mutation(*comp, project_->timeline().current(), "mover osso");
    bump_model_revision_locked();
    write_channel(*l, node, 3, local, before.x + d.x);
    write_channel(*l, node, 4, local, before.y + d.y);
    write_channel(*l, node, 5, local, before.z + d.z);
    project_->mark_dirty();
    request_render();
    return true;
}

// -----------------------------------------------------------------------------
// IK de dois ossos
// -----------------------------------------------------------------------------

bool Engine::humanoid_set_ik(u64 layerId, u32 limb, bool on) noexcept {
    HumanoidBone endBone, midBone;
    if (!limb_end(limb, endBone, midBone)) return false;
    std::lock_guard<std::mutex> lock(modelMutex_);
    Composition* comp = project_ ? current_composition() : nullptr;
    Layer* l = rig_layer(comp, layerId);
    if (!l || l->locked) return false;
    const auto it = models_.find(l->model.scene.pack());
    const Asset* a = project_->asset(l->model.scene);
    if (it == models_.end() || !it->second || !a) return false;
    const scene3d::SceneAsset& asset = *it->second;
    const rig::HumanoidMap map = rig::humanoid_map(asset, a->model.rig);
    const i32 end = map[endBone];
    i32 mid = -1, root = -1;
    if (end < 0 || !scene3d::ik_chain(asset, end, mid, root)) return false;
    const u32 effect = rig::kBoneIkBase + static_cast<u32>(end);
    bool has = false;
    for (u32 i = 0; i < l->tracks.size(); ++i)
        has = has || (l->tracks.at(i).property == TrackProperty::BonePose && l->tracks.at(i).effectIndex == effect);
    if (on == has) return false;
    history_.before_mutation(*comp, project_->timeline().current(), on ? "ligar IK" : "desligar IK");
    bump_model_revision_locked();
    if (!on) {
        l->tracks.remove_if([&](const Track& t) { return t.property == TrackProperty::BonePose && t.effectIndex == effect; });
    } else {
        const FrameIndex now = playback_.current();
        scene3d::Pose pose;
        posed(*comp, *l, asset, now, pose);
        const Vec3 pa = rig::matrix_position(pose.nodeWorld[static_cast<usize>(root)]);
        const Vec3 pb = rig::matrix_position(pose.nodeWorld[static_cast<usize>(mid)]);
        const Vec3 pc = rig::matrix_position(pose.nodeWorld[static_cast<usize>(end)]);
        // Polo: para onde o meio já dobra; reto, cotovelo para trás e joelho para frente.
        const rig::HumanoidRest rest = rig::rest_pose(asset, map);
        Vec3 left = rest.position[static_cast<usize>(HumanoidBone::LeftUpperArm)] - rest.position[static_cast<usize>(HumanoidBone::RightUpperArm)];
        left.y = 0.0f;
        const Vec3 front = left.length_sq() > 1e-12f ? left.normalized().cross(Vec3{0, 1, 0}) : Vec3{0, 0, 1};
        const f32 len = (pb - pa).length() + (pc - pb).length();
        const Vec3 axis = (pc - pa).normalized();
        Vec3 bend = (pb - pa) - axis * axis.dot(pb - pa);
        if (bend.length() < len * 0.05f) bend = limb >= 2 ? front : -front;
        const Vec3 pole = pb + bend.normalized() * std::max(len * 0.5f, 1e-3f);
        const f32 values[7]{pc.x, pc.y, pc.z, pole.x, pole.y, pole.z, 1.0f};
        for (u32 c = 0; c < 7; ++c) {
            Track& t = l->tracks.get_or_create(TrackProperty::BonePose, effect, c);
            t.staticValue = values[c];
        }
    }
    project_->mark_dirty();
    request_render();
    return true;
}

u32 Engine::query_humanoid_ik(u64 layerId, f32* out, u32 capacity) noexcept {
    constexpr u32 need = 4 * kHumanoidIkFloats;
    std::lock_guard<std::mutex> lock(modelMutex_);
    Composition* comp = project_ ? current_composition() : nullptr;
    const Layer* l = rig_layer(comp, layerId);
    if (!l) return 0;
    const auto it = models_.find(l->model.scene.pack());
    const Asset* a = project_->asset(l->model.scene);
    if (it == models_.end() || !it->second || !a || it->second->skins.empty()) return 0;
    if (!out || capacity < need) return need;
    const rig::HumanoidMap map = rig::humanoid_map(*it->second, a->model.rig);
    const FrameIndex now = playback_.current();
    const FrameIndex local = l->local_time(now);
    const Mat4 world = layer_world_3d(*comp, *l, now) * file_to_layer(l->model);
    const Mat4 vp = sceneEditor_.enabled ? scene_editor_projection(comp->width(), comp->height(), sceneEditor_)
                                         : comp_view_projection(*comp, now);
    for (u32 limb = 0; limb < 4; ++limb) {
        f32* o = out + limb * kHumanoidIkFloats;
        std::fill(o, o + kHumanoidIkFloats, 0.0f);
        HumanoidBone endBone, midBone;
        (void)limb_end(limb, endBone, midBone);
        const i32 end = map[endBone];
        if (end < 0) { o[0] = -1.0f; continue; }   // −1: membro sem osso (não dá para ligar)
        const u32 effect = rig::kBoneIkBase + static_cast<u32>(end);
        const Track* w = l->tracks.find(TrackProperty::BonePose, effect, rig::kIkParamWeight);
        if (!w) continue;
        o[0] = 1.0f;
        (void)to_screen(vp * world, ik_value(*l, effect, rig::kIkParamTargetX, local), o[1], o[2]);
        (void)to_screen(vp * world, ik_value(*l, effect, rig::kIkParamPoleX, local), o[3], o[4]);
        bool here = false;
        for (u32 c = 0; c < 7; ++c) {
            const Track* t = l->tracks.find(TrackProperty::BonePose, effect, c);
            here = here || (t && t->find_exact(local) != kInvalidIndex);
        }
        o[5] = here ? 1.0f : 0.0f;
        o[6] = std::clamp(w->value_or(local, w->staticValue), 0.0f, 1.0f);
    }
    return need;
}

bool Engine::drag_humanoid_ik(u64 layerId, u32 limb, bool pole, f32 dxPx, f32 dyPx, bool continuing) noexcept {
    HumanoidBone endBone, midBone;
    if (!limb_end(limb, endBone, midBone) || !std::isfinite(dxPx) || !std::isfinite(dyPx)) return false;
    std::lock_guard<std::mutex> lock(modelMutex_);
    Composition* comp = project_ ? current_composition() : nullptr;
    Layer* l = rig_layer(comp, layerId);
    if (!l || l->locked) return false;
    const auto it = models_.find(l->model.scene.pack());
    const Asset* a = project_->asset(l->model.scene);
    if (it == models_.end() || !it->second || !a) return false;
    const i32 end = rig::humanoid_map(*it->second, a->model.rig)[endBone];
    if (end < 0) return false;
    const u32 effect = rig::kBoneIkBase + static_cast<u32>(end);
    if (!l->tracks.find(TrackProperty::BonePose, effect, rig::kIkParamWeight)) return false;
    const FrameIndex now = playback_.current();
    const FrameIndex local = l->local_time(now);
    const u32 first = pole ? rig::kIkParamPoleX : rig::kIkParamTargetX;
    for (u32 c = first; c < first + 3; ++c) {
        const Track* t = l->tracks.find(TrackProperty::BonePose, effect, c);
        if (t && !t->keys.empty() && !l->contains_time(now)) return false;
    }
    const Mat4 world = layer_world_3d(*comp, *l, now) * file_to_layer(l->model);
    const Mat4 vp = sceneEditor_.enabled ? scene_editor_projection(comp->width(), comp->height(), sceneEditor_)
                                         : comp_view_projection(*comp, now);
    const Vec3 p = ik_value(*l, effect, first, local);
    f32 sx = 0, sy = 0;
    Vec3 q;
    if (!to_screen(vp * world, p, sx, sy) || !unproject(vp * world, p, sx + dxPx, sy + dyPx, q)) return false;
    if (!continuing) history_.before_mutation(*comp, project_->timeline().current(), pole ? "mover polo do IK" : "mover alvo do IK");
    bump_model_revision_locked();
    write_channel(*l, effect, first, local, q.x);
    write_channel(*l, effect, first + 1, local, q.y);
    write_channel(*l, effect, first + 2, local, q.z);
    project_->mark_dirty();
    request_render();
    return true;
}

i32 Engine::toggle_humanoid_ik_key(u64 layerId, u32 limb) noexcept {
    HumanoidBone endBone, midBone;
    if (!limb_end(limb, endBone, midBone)) return -1;
    std::lock_guard<std::mutex> lock(modelMutex_);
    Composition* comp = project_ ? current_composition() : nullptr;
    Layer* l = rig_layer(comp, layerId);
    if (!l || l->locked) return -1;
    const auto it = models_.find(l->model.scene.pack());
    const Asset* a = project_->asset(l->model.scene);
    if (it == models_.end() || !it->second || !a) return -1;
    const i32 end = rig::humanoid_map(*it->second, a->model.rig)[endBone];
    if (end < 0) return -1;
    const u32 effect = rig::kBoneIkBase + static_cast<u32>(end);
    if (!l->tracks.find(TrackProperty::BonePose, effect, rig::kIkParamWeight)) return -1;
    const FrameIndex now = playback_.current();
    if (!l->contains_time(now)) return -1;
    const FrameIndex local = l->local_time(now);
    bool any = false;
    for (u32 c = 0; c < 7; ++c) {
        const Track* t = l->tracks.find(TrackProperty::BonePose, effect, c);
        any = any || (t && t->find_exact(local) != kInvalidIndex);
    }
    history_.before_mutation(*comp, project_->timeline().current(), any ? "tirar keyframe do IK" : "keyframe do IK");
    bump_model_revision_locked();
    for (u32 c = 0; c < 7; ++c) {
        Track* t = l->tracks.find(TrackProperty::BonePose, effect, c);
        if (!t) continue;
        const f32 v = t->value_or(local, t->staticValue);
        if (any) {
            if (t->find_exact(local) == kInvalidIndex) continue;
            (void)t->remove(local);
            if (t->keys.empty()) t->staticValue = v;
        } else {
            if (t->keys.empty()) t->staticValue = v;
            (void)t->set(local, v);
        }
    }
    project_->mark_dirty();
    request_render();
    return any ? 0 : 1;
}

} // namespace aurea
