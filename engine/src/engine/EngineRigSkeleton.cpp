// =============================================================================
//  Aurea / engine / EngineRigSkeleton.cpp
//
//  ESQUELETO 3D mais fácil (0.4.5, pedido de ⚡ᴍᴀʀᴋᴢ, pauloph2517 e Jess):
//  anéis de rotação na articulação (eixos LOCAIS X/Y/Z do osso), rotação
//  numérica e colar pose em lote, espelhar L/R e IK simples (arrastar a
//  ponta leva a cadeia de dois ossos e grava FK). Tudo nas trilhas BonePose
//  de sempre (scene3d/RigPose.hpp): o projeto salvo não muda de formato, e
//  cada gesto é UM passo de desfazer (`continuing`).
//
//  Regra de key igual à do drag_bone: canal com keyframe grava key no
//  cabeçote; sem keyframe, muda a pose parada. Canal animado com o cabeçote
//  fora da camada não tem onde gravar — o comando recusa.
// =============================================================================
#include "aurea/Engine.hpp"

#include "aurea/core/Trackball.hpp"
#include "aurea/render/Renderer.hpp"
#include "aurea/rig/Ik.hpp"
#include "aurea/scene3d/Animation.hpp"
#include "aurea/scene3d/RigPose.hpp"
#include "aurea/scene3d/RigSkeleton.hpp"

#include <algorithm>
#include <cmath>

namespace aurea {
namespace {

Layer* skeleton_layer(Composition* comp, u64 id) noexcept {
    Layer* l = comp ? comp->layer(LayerId::unpack(id)) : nullptr;
    return l && l->kind == LayerKind::Model3D ? l : nullptr;
}

/// O mesmo `layer_from_model` do renderer (Y/Z do arquivo → composição, m → px).
Mat4 skeleton_file_to_layer(const Model3DData& m) noexcept {
    Mat4 flip;
    flip.col[1] = Vec4{0, -1, 0, 0};
    flip.col[2] = Vec4{0, 0, -1, 0};
    return flip * Mat4::scale(Vec3{m.unitScale, m.unitScale, m.unitScale}) * Mat4::translation(-m.pivot);
}

/// Clipe e tempo do clipe da camada no cabeçote.
void clip_at(const Composition& comp, const Layer& l, const scene3d::SceneAsset& asset, FrameIndex now, i32& clip, f32& t,
             f64& local) {
    const f64 fps = comp.fps() > 0.0 ? comp.fps() : 30.0;
    local = static_cast<f64>(l.local_time(now).value);
    clip = l.model.animationClip >= 0 && l.model.animationClip < static_cast<i32>(asset.animations.size()) ? l.model.animationClip : -1;
    t = clip >= 0 ? scene3d::clip_time(asset.animations[static_cast<usize>(clip)], local / fps * l.model.timeScale) : 0.0f;
}

/// Pose que a prévia mostra (clipe + pose do usuário + IK).
void user_pose(const Composition& comp, const Layer& l, const scene3d::SceneAsset& asset, FrameIndex now, scene3d::Pose& pose) {
    i32 clip;
    f32 t;
    f64 local;
    clip_at(comp, l, asset, now, clip, t, local);
    scene3d::evaluate_rig_pose(asset, l, clip, t, local, pose);
}

/// Pose SEM a pose do usuário (só o arquivo): o quadro em que as rotações
/// de pose são lidas — o mesmo dos dois lados, para espelhar.
void file_pose(const Composition& comp, const Layer& l, const scene3d::SceneAsset& asset, FrameIndex now, scene3d::Pose& pose) {
    i32 clip;
    f32 t;
    f64 local;
    clip_at(comp, l, asset, now, clip, t, local);
    scene3d::evaluate_pose(asset, clip, t, pose);
}

Vec3 channels(const Layer& l, u32 node, u32 first, FrameIndex local) noexcept {
    Vec3 e{0, 0, 0};
    for (u32 c = 0; c < 3; ++c) {
        const Track* t = l.tracks.find(TrackProperty::BonePose, node, first + c);
        const f32 v = t ? t->value_or(local, t->staticValue) : 0.0f;
        (c == 0 ? e.x : c == 1 ? e.y : e.z) = std::isfinite(v) ? v : 0.0f;
    }
    return e;
}

bool has_channels(const Layer& l, u32 node, u32 first) noexcept {
    for (u32 c = 0; c < 3; ++c)
        if (l.tracks.find(TrackProperty::BonePose, node, first + c)) return true;
    return false;
}

/// Pode gravar nos canais [first, first+3) do osso agora?
bool writable(const Layer& l, u32 node, u32 first, bool inside) noexcept {
    if (inside) return true;
    for (u32 c = 0; c < 3; ++c) {
        const Track* t = l.tracks.find(TrackProperty::BonePose, node, first + c);
        if (t && !t->keys.empty()) return false;
    }
    return true;
}

void write3(Layer& l, u32 node, u32 first, FrameIndex local, Vec3 v) {
    const f32 values[3]{v.x, v.y, v.z};
    for (u32 c = 0; c < 3; ++c) {
        if (!std::isfinite(values[c])) continue;
        Track& t = l.tracks.get_or_create(TrackProperty::BonePose, node, first + c);
        if (t.keys.empty()) t.staticValue = values[c];
        else (void)t.set(local, values[c]);
    }
}

bool is_pose_bone(const std::vector<i32>& bones, i32 node) noexcept {
    return node >= 0 && std::find(bones.begin(), bones.end(), node) != bones.end();
}

/// Parte linear (3×3, sem translação) de uma matriz afim.
Mat4 linear_of(const Mat4& m) noexcept {
    Mat4 r = m;
    r.col[3] = Vec4{0, 0, 0, 1};
    return r;
}

/// Base dos anéis do osso na tela (ver Engine::kBoneGizmoFloats).
struct GizmoBasis {
    Vec2 center{0, 0};
    bool front = false;
    Vec2 axis[3];    ///< eixos LOCAIS X/Y/Z do osso projetados, em px, já no raio pedido
    f32 toward[3]{0, 0, 0};   ///< quanto cada eixo aponta para a câmera (−1..1)
};

bool gizmo_basis(const Mat4& clipFromModel, f32 unitPx, const Mat4& nodeWorld, f32 radiusPx, GizmoBasis& g) noexcept {
    const Vec3 j = rig::matrix_position(nodeWorld);
    const Quat w = rig::matrix_rotation(nodeWorld);
    const Vec4 cj = clipFromModel * Vec4{j, 1.0f};
    if (!(cj.w > 1e-6f) || !std::isfinite(cj.x) || !std::isfinite(cj.y)) return false;
    g.center = Vec2{cj.x / cj.w, cj.y / cj.w};
    g.front = true;
    // Um passo de ~4 px da composição no espaço do modelo: a derivada da projeção.
    const f32 step = 4.0f / std::max(unitPx, 1e-6f);
    const Vec3 e[3]{rig::rotate(w, Vec3{1, 0, 0}), rig::rotate(w, Vec3{0, 1, 0}), rig::rotate(w, Vec3{0, 0, 1})};
    Vec2 p[3];
    f32 dw[3], dz[3];
    f32 sum = 0.0f;
    for (int a = 0; a < 3; ++a) {
        const Vec4 c = clipFromModel * Vec4{j + e[a] * step, 1.0f};
        if (!(c.w > 1e-6f)) return false;
        p[a] = Vec2{c.x / c.w, c.y / c.w} - g.center;
        dw[a] = c.w - cj.w;
        dz[a] = c.z / c.w - cj.z / cj.w;
        sum += p[a].length_sq();
    }
    // Base ortonormal: as projeções somam 2·k² (k = px por passo no plano da vista).
    const f32 k = std::sqrt(sum * 0.5f);
    if (!(k > 1e-6f) || !std::isfinite(k)) return false;
    const bool perspective = std::fabs(dw[0]) + std::fabs(dw[1]) + std::fabs(dw[2]) > 1e-6f * std::fabs(cj.w);
    for (int a = 0; a < 3; ++a) {
        g.axis[a] = p[a] * (radiusPx / k);
        const f32 depth = std::sqrt(std::max(0.0f, 1.0f - p[a].length_sq() / (k * k)));
        // Mais longe: w cresce (perspectiva) ou a profundidade da tela cresce (orto).
        const bool away = perspective ? dw[a] > 0.0f : dz[a] > 0.0f;
        g.toward[a] = away ? -depth : depth;
    }
    return true;
}

} // namespace

u32 Engine::query_bone_gizmo(u64 layerId, i32 node, f32 radiusPx, f32* out, u32 capacity) noexcept {
    if (!std::isfinite(radiusPx) || radiusPx <= 0.0f) return 0;
    std::lock_guard<std::mutex> lock(modelMutex_);
    Composition* comp = project_ ? current_composition() : nullptr;
    const Layer* l = skeleton_layer(comp, layerId);
    if (!l || node < 0) return 0;
    const auto it = models_.find(l->model.scene.pack());
    if (it == models_.end() || !it->second || static_cast<usize>(node) >= it->second->nodes.size()) return 0;
    const scene3d::SceneAsset& asset = *it->second;
    if (!is_pose_bone(scene3d::pose_bones(asset), node)) return 0;
    if (!out || capacity < kBoneGizmoFloats) return kBoneGizmoFloats;
    const FrameIndex now = playback_.current();
    scene3d::Pose pose;
    user_pose(*comp, *l, asset, now, pose);
    if (static_cast<usize>(node) >= pose.nodeWorld.size()) return 0;
    const Mat4 world = layer_world_3d(*comp, *l, now) * skeleton_file_to_layer(l->model);
    const Mat4 vp = sceneEditor_.enabled ? scene_editor_projection(comp->width(), comp->height(), sceneEditor_)
                                         : comp_view_projection(*comp, now);
    const f32 unitPx = Vec3{world.col[0].x, world.col[0].y, world.col[0].z}.length();
    for (u32 i = 0; i < kBoneGizmoFloats; ++i) out[i] = 0.0f;
    GizmoBasis g;
    if (!gizmo_basis(vp * world, unitPx, pose.nodeWorld[static_cast<usize>(node)], radiusPx, g)) return kBoneGizmoFloats;
    out[0] = g.center.x;
    out[1] = g.center.y;
    out[2] = 1.0f;
    for (u32 a = 0; a < 3; ++a) {
        // Anel do eixo a: U = eixo seguinte, V = o outro (u × v = a, giro positivo de U para V).
        const u32 u = (a + 1) % 3, v = (a + 2) % 3;
        f32* o = out + 3 + a * 6;
        o[0] = g.axis[u].x; o[1] = g.axis[u].y; o[2] = g.toward[u];
        o[3] = g.axis[v].x; o[4] = g.axis[v].y; o[5] = g.toward[v];
    }
    return kBoneGizmoFloats;
}

bool Engine::rotate_bone_axis(u64 layerId, u32 node, u32 axis, f32 deg, bool continuing) noexcept {
    if (axis > 2 || !std::isfinite(deg)) return false;
    std::lock_guard<std::mutex> lock(modelMutex_);
    Composition* comp = project_ ? current_composition() : nullptr;
    Layer* l = skeleton_layer(comp, layerId);
    if (!l || l->locked) return false;
    const auto it = models_.find(l->model.scene.pack());
    if (it == models_.end() || !it->second || node >= it->second->nodes.size()) return false;
    if (!is_pose_bone(scene3d::pose_bones(*it->second), static_cast<i32>(node))) return false;
    const FrameIndex now = playback_.current();
    const FrameIndex local = l->local_time(now);
    if (!writable(*l, node, 0, l->contains_time(now))) return false;
    // Girar no eixo LOCAL do osso, na articulação: q' = q · R(eixo, θ).
    const Vec3 axisV{axis == 0 ? 1.0f : 0.0f, axis == 1 ? 1.0f : 0.0f, axis == 2 ? 1.0f : 0.0f};
    const Vec3 before = channels(*l, node, 0, local);
    const Quat next = (scene3d::bone_quat(before) * Quat::from_axis_angle(axisV, deg * 3.14159265358979323846f / 180.0f)).normalized();
    const Vec3 e = trackball::euler_deg_from_quat(next, before);
    if (!continuing) history_.before_mutation(*comp, project_->timeline().current(), "girar osso");
    bump_model_revision_locked();
    write3(*l, node, 0, local, e);
    project_->mark_dirty();
    request_render();
    return true;
}

bool Engine::drag_bone_ring(u64 layerId, u32 node, u32 axis, f32 x0, f32 y0, f32 x1, f32 y1, f32 radiusPx,
                            bool continuing) noexcept {
    if (axis > 3 || !std::isfinite(x0) || !std::isfinite(y0) || !std::isfinite(x1) || !std::isfinite(y1)) return false;
    f32 g[kBoneGizmoFloats];
    if (query_bone_gizmo(layerId, static_cast<i32>(node), radiusPx, g, kBoneGizmoFloats) != kBoneGizmoFloats || g[2] < 0.5f)
        return false;
    const Vec2 c{g[0], g[1]};
    if (axis == 3) {
        // Anel de fora: gira em volta do olhar (o mesmo "twist" de dois dedos).
        const f32 twist = scene3d::swept_degrees(c, Vec2{x0, y0}, Vec2{x1, y1});
        return std::fabs(twist) < 1e-5f ? continuing : drag_bone(layerId, node, 0.0f, 0.0f, twist, continuing);
    }
    const f32* o = g + 3 + axis * 6;
    const f32 deg = scene3d::ring_drag_degrees(Vec2{o[0], o[1]}, Vec2{o[3], o[4]}, Vec2{x0, y0} - c, Vec2{x1, y1} - c, radiusPx);
    if (std::fabs(deg) < 1e-5f) return continuing;
    return rotate_bone_axis(layerId, node, axis, deg, continuing);
}

bool Engine::set_bone_rotations(u64 layerId, const f32* nodeXyz, u32 count, bool continuing) noexcept {
    if (!nodeXyz || count == 0) return false;
    std::lock_guard<std::mutex> lock(modelMutex_);
    Composition* comp = project_ ? current_composition() : nullptr;
    Layer* l = skeleton_layer(comp, layerId);
    if (!l || l->locked) return false;
    const auto it = models_.find(l->model.scene.pack());
    if (it == models_.end() || !it->second) return false;
    const std::vector<i32> bones = scene3d::pose_bones(*it->second);
    const FrameIndex now = playback_.current();
    const bool inside = l->contains_time(now);
    for (u32 i = 0; i < count; ++i) {
        const f32* r = nodeXyz + static_cast<usize>(i) * 4;
        if (!std::isfinite(r[0]) || !std::isfinite(r[1]) || !std::isfinite(r[2]) || !std::isfinite(r[3])) return false;
        const i32 node = static_cast<i32>(r[0]);
        if (!is_pose_bone(bones, node) || !writable(*l, static_cast<u32>(node), 0, inside)) return false;
    }
    const FrameIndex local = l->local_time(now);
    if (!continuing) history_.before_mutation(*comp, project_->timeline().current(), count == 1 ? "girar osso" : "pose");
    bump_model_revision_locked();
    for (u32 i = 0; i < count; ++i) {
        const f32* r = nodeXyz + static_cast<usize>(i) * 4;
        write3(*l, static_cast<u32>(r[0]), 0, local, Vec3{r[1], r[2], r[3]});
    }
    project_->mark_dirty();
    request_render();
    return true;
}

i32 Engine::bone_mirror_partner(u64 layerId, i32 node) noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    Composition* comp = project_ ? current_composition() : nullptr;
    const Layer* l = skeleton_layer(comp, layerId);
    if (!l) return -1;
    const auto it = models_.find(l->model.scene.pack());
    if (it == models_.end() || !it->second) return -1;
    const std::vector<i32> bones = scene3d::pose_bones(*it->second);
    return is_pose_bone(bones, node) ? scene3d::mirror_partner(*it->second, bones, node) : -1;
}

bool Engine::mirror_bone_pose(u64 layerId, i32 node) noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    Composition* comp = project_ ? current_composition() : nullptr;
    Layer* l = skeleton_layer(comp, layerId);
    if (!l || l->locked) return false;
    const auto it = models_.find(l->model.scene.pack());
    if (it == models_.end() || !it->second) return false;
    const scene3d::SceneAsset& asset = *it->second;
    const std::vector<i32> bones = scene3d::pose_bones(asset);
    if (bones.empty() || (node >= 0 && !is_pose_bone(bones, node))) return false;
    const FrameIndex now = playback_.current();
    const FrameIndex local = l->local_time(now);
    const bool inside = l->contains_time(now);
    scene3d::Pose rest;
    file_pose(*comp, *l, asset, now, rest);
    const u32 axis = scene3d::mirror_axis(asset, bones, rest);
    auto frame = [&](i32 n) {
        return static_cast<usize>(n) < rest.nodeWorld.size() ? rig::matrix_rotation(rest.nodeWorld[static_cast<usize>(n)]) : Quat::identity();
    };
    auto parent_linear = [&](i32 n) {
        const i32 p = asset.nodes[static_cast<usize>(n)].parent;
        return p >= 0 && static_cast<usize>(p) < rest.nodeWorld.size() ? linear_of(rest.nodeWorld[static_cast<usize>(p)]) : Mat4::identity();
    };
    struct Write { i32 node; Vec3 euler; bool hasTr; Vec3 tr; };
    std::vector<Write> writes;
    // A pose de `src` levada, espelhada, para `dst`: no quadro do arquivo de
    // cada lado (D = F·q·F⁻¹ no modelo; refletida; q' = F'⁻¹·D'·F').
    auto mirrored = [&](i32 src, i32 dst) {
        Write w{dst, Vec3{0, 0, 0}, false, Vec3{0, 0, 0}};
        const Quat fs = frame(src), fd = frame(dst);
        const Quat q = scene3d::bone_quat(channels(*l, static_cast<u32>(src), 0, local));
        const Quat d = scene3d::mirror_rotation((fs * q * rig::conj(fs)).normalized(), axis);
        const Quat qd = (rig::conj(fd) * d * fd).normalized();
        w.euler = trackball::euler_deg_from_quat(qd, channels(*l, static_cast<u32>(dst), 0, local));
        // Translação (o quadril): no espaço do pai, refletida no modelo.
        if (has_channels(*l, static_cast<u32>(src), 3) || has_channels(*l, static_cast<u32>(dst), 3)) {
            const Vec3 t = channels(*l, static_cast<u32>(src), 3, local);
            const Vec4 m = parent_linear(src) * Vec4{t, 0.0f};
            Vec3 mm{m.x, m.y, m.z};
            (axis == 0 ? mm.x : axis == 1 ? mm.y : mm.z) *= -1.0f;
            Mat4 inv;
            if (rig::inverse(parent_linear(dst), inv)) {
                const Vec4 back = inv * Vec4{mm, 0.0f};
                w.hasTr = true;
                w.tr = Vec3{back.x, back.y, back.z};
            }
        }
        return w;
    };
    auto posed_any = [&](i32 n) { return has_channels(*l, static_cast<u32>(n), 0) || has_channels(*l, static_cast<u32>(n), 3); };
    if (node >= 0) {
        const i32 dst = scene3d::mirror_partner(asset, bones, node);
        if (dst < 0) return false;
        writes.push_back(mirrored(node, dst));
    } else {
        for (const i32 b : bones) {
            const i32 p = scene3d::mirror_partner(asset, bones, b);
            const i32 src = p >= 0 ? p : b;   // osso do meio: espelha nele mesmo
            if (!posed_any(src) && !posed_any(b)) continue;
            writes.push_back(mirrored(src, b));
        }
    }
    if (writes.empty()) return false;
    for (const Write& w : writes)
        if (!writable(*l, static_cast<u32>(w.node), 0, inside) || (w.hasTr && !writable(*l, static_cast<u32>(w.node), 3, inside))) return false;
    history_.before_mutation(*comp, project_->timeline().current(), node >= 0 ? "espelhar osso" : "espelhar pose");
    bump_model_revision_locked();
    for (const Write& w : writes) {
        write3(*l, static_cast<u32>(w.node), 0, local, w.euler);
        if (w.hasTr) write3(*l, static_cast<u32>(w.node), 3, local, w.tr);
    }
    project_->mark_dirty();
    request_render();
    return true;
}

bool Engine::drag_bone_ik(u64 layerId, u32 node, f32 dxPx, f32 dyPx, bool continuing) noexcept {
    if (!std::isfinite(dxPx) || !std::isfinite(dyPx)) return false;
    std::lock_guard<std::mutex> lock(modelMutex_);
    Composition* comp = project_ ? current_composition() : nullptr;
    Layer* l = skeleton_layer(comp, layerId);
    if (!l || l->locked) return false;
    const auto it = models_.find(l->model.scene.pack());
    if (it == models_.end() || !it->second || node >= it->second->nodes.size()) return false;
    const scene3d::SceneAsset& asset = *it->second;
    const std::vector<i32> bones = scene3d::pose_bones(asset);
    i32 mid = -1, root = -1;
    if (!is_pose_bone(bones, static_cast<i32>(node)) || !scene3d::ik_chain(asset, static_cast<i32>(node), mid, root)
        || !is_pose_bone(bones, mid) || !is_pose_bone(bones, root))
        return false;
    const FrameIndex now = playback_.current();
    const FrameIndex local = l->local_time(now);
    const bool inside = l->contains_time(now);
    if (!writable(*l, static_cast<u32>(root), 0, inside) || !writable(*l, static_cast<u32>(mid), 0, inside)) return false;
    scene3d::Pose pose;
    user_pose(*comp, *l, asset, now, pose);
    if (node >= pose.nodeWorld.size()) return false;
    const Mat4 world = layer_world_3d(*comp, *l, now) * skeleton_file_to_layer(l->model);
    const Mat4 vp = sceneEditor_.enabled ? scene_editor_projection(comp->width(), comp->height(), sceneEditor_)
                                         : comp_view_projection(*comp, now);
    const Mat4 m = vp * world;
    const Vec3 a = rig::matrix_position(pose.nodeWorld[static_cast<usize>(root)]);
    const Vec3 b = rig::matrix_position(pose.nodeWorld[static_cast<usize>(mid)]);
    const Vec3 c = rig::matrix_position(pose.nodeWorld[node]);
    // Alvo: a ponta andou (dx, dy) na tela, no plano da vista que passa por ela.
    const Vec4 cc = m * Vec4{c, 1.0f};
    if (!(cc.w > 1e-6f)) return false;
    Mat4 inv;
    if (!rig::inverse(m, inv)) return false;
    const Vec4 q = inv * Vec4{cc.x / cc.w + dxPx, cc.y / cc.w + dyPx, cc.z / cc.w, 1.0f};
    if (std::fabs(q.w) < 1e-12f) return false;
    const Vec3 target{q.x / q.w, q.y / q.w, q.z / q.w};
    if (!std::isfinite(target.x) || !std::isfinite(target.y) || !std::isfinite(target.z)) return false;
    // Polo = o próprio meio: a dobra continua para o lado em que já estava.
    const rig::TwoBoneIk s = rig::solve_two_bone(a, b, c, target, b, 1.0f);
    // Mundo → pose local (q' = q · W⁻¹·Δ·W, W = rotação de mundo do osso agora).
    const Quat wr = rig::matrix_rotation(pose.nodeWorld[static_cast<usize>(root)]);
    const Quat wm = (s.rootDelta * rig::matrix_rotation(pose.nodeWorld[static_cast<usize>(mid)])).normalized();
    const Vec3 er = channels(*l, static_cast<u32>(root), 0, local), em = channels(*l, static_cast<u32>(mid), 0, local);
    const Quat qr = (scene3d::bone_quat(er) * (rig::conj(wr) * s.rootDelta * wr)).normalized();
    const Quat qm = (scene3d::bone_quat(em) * (rig::conj(wm) * s.midDelta * wm)).normalized();
    if (!continuing) history_.before_mutation(*comp, project_->timeline().current(), "IK do osso");
    bump_model_revision_locked();
    write3(*l, static_cast<u32>(root), 0, local, trackball::euler_deg_from_quat(qr, er));
    write3(*l, static_cast<u32>(mid), 0, local, trackball::euler_deg_from_quat(qm, em));
    project_->mark_dirty();
    request_render();
    return true;
}

} // namespace aurea
