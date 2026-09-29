// =============================================================================
//  Aurea / engine / EngineRig.cpp
//
//  API do RIG 2D (timeline/Rig.hpp): montar o esqueleto sobre a imagem
//  (juntas em px da imagem; a UI fala em px da composição) e posar no
//  playhead (FK / IK de 2 ossos, keyframe nas trilhas RigBone). Padrão de
//  mutação do motor: modelMutex_, history_.before_mutation, modelRevision_,
//  mark_dirty, request_render. `continuing` (arrasto) pula o passo novo.
// =============================================================================
#include "aurea/Engine.hpp"

#include "aurea/render/Renderer.hpp"
#include "aurea/timeline/Rig.hpp"

#include <algorithm>
#include <iterator>
#include <cmath>

namespace aurea {
namespace {

Layer* rig_layer(Composition* comp, u64 id) noexcept {
    Layer* l = comp ? comp->layer(LayerId::unpack(id)) : nullptr;
    return l && l->kind == LayerKind::Image ? l : nullptr;
}

/// Composição ← camada no plano (a afim 2D da matriz; camada 3D = aproximação).
struct Plane {
    f32 a = 1, b = 0, c = 0, d = 1, tx = 0, ty = 0;
    [[nodiscard]] Vec2 to_comp(Vec2 p) const noexcept { return Vec2{a * p.x + c * p.y + tx, b * p.x + d * p.y + ty}; }
    [[nodiscard]] bool to_layer(Vec2 q, Vec2& p) const noexcept {
        const f32 det = a * d - b * c;
        if (std::fabs(det) < 1e-12f) return false;
        const f32 x = q.x - tx, y = q.y - ty;
        p = Vec2{(d * x - c * y) / det, (-b * x + a * y) / det};
        return std::isfinite(p.x) && std::isfinite(p.y);
    }
};

Plane plane_of(const Composition& comp, const Layer& l, FrameIndex t) noexcept {
    const Mat4 m = layer_comp_matrix(comp, l, t);
    return Plane{m.col[0].x, m.col[0].y, m.col[1].x, m.col[1].y, m.col[3].x, m.col[3].y};
}

} // namespace

u32 Engine::query_rig(u64 layerId, bool bind, f32* out, u32 capacity) noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    Composition* comp = project_ ? current_composition() : nullptr;
    const Layer* l = rig_layer(comp, layerId);
    if (!l) return 0;
    const u32 need = static_cast<u32>(l->rig.joints.size()) * kRigJointFloats;
    if (!out || capacity < need) return need;
    const FrameIndex t = playback_.current();
    const FrameIndex local = l->local_time(t);
    std::vector<f32> deg;
    std::vector<rig::Affine> bones;
    std::vector<Vec2> pos;
    rig::sample_angles(*l, local, deg);
    rig::pose(l->rig, deg, bones);
    rig::posed_joints(l->rig, bones, pos);
    const Plane pl = plane_of(*comp, *l, t);
    for (u32 i = 0; i < l->rig.joints.size(); ++i) {
        const RigJoint& j = l->rig.joints[i];
        const Vec2 q = pl.to_comp(bind ? j.pos : pos[i]);
        const Track* tr = l->tracks.find(TrackProperty::RigBone, kInvalidIndex, j.id);
        f32* o = out + i * kRigJointFloats;
        o[0] = static_cast<f32>(j.id);
        o[1] = j.parent == kInvalidIndex || rig::index_of(l->rig, j.parent) < 0 ? -1.0f : static_cast<f32>(j.parent);
        o[2] = q.x;
        o[3] = q.y;
        o[4] = tr && tr->find_exact(local) != kInvalidIndex ? 1.0f : 0.0f;
    }
    return need;
}

i32 Engine::rig_add_joint(u64 layerId, i32 parentId, f32 compX, f32 compY) noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    Composition* comp = project_ ? current_composition() : nullptr;
    Layer* l = rig_layer(comp, layerId);
    if (!l || l->rig.joints.size() >= rig::kMaxJoints) return -1;
    const bool hasParent = parentId >= 0 && rig::index_of(l->rig, static_cast<u32>(parentId)) >= 0;
    if (parentId >= 0 && !hasParent) return -1;
    Vec2 p;
    if (!plane_of(*comp, *l, playback_.current()).to_layer(Vec2{compX, compY}, p)) return -1;
    history_.before_mutation(*comp, project_->timeline().current(), "junta do rig");
    modelRevision_.fetch_add(1, std::memory_order_acq_rel);
    RigJoint j;
    j.id = l->rig.nextJointId++;
    j.parent = hasParent ? static_cast<u32>(parentId) : kInvalidIndex;
    j.pos = p;
    l->rig.joints.push_back(j);
    project_->mark_dirty();
    request_render();
    return static_cast<i32>(j.id);
}

bool Engine::rig_move_joint(u64 layerId, u32 jointId, f32 compX, f32 compY, bool continuing) noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    Composition* comp = project_ ? current_composition() : nullptr;
    Layer* l = rig_layer(comp, layerId);
    const i32 i = l ? rig::index_of(l->rig, jointId) : -1;
    if (i < 0) return false;
    Vec2 p;
    if (!plane_of(*comp, *l, playback_.current()).to_layer(Vec2{compX, compY}, p)) return false;
    if (!continuing) history_.before_mutation(*comp, project_->timeline().current(), "mover junta");
    modelRevision_.fetch_add(1, std::memory_order_acq_rel);
    l->rig.joints[static_cast<u32>(i)].pos = p;
    project_->mark_dirty();
    request_render();
    return true;
}

bool Engine::rig_remove_joint(u64 layerId, u32 jointId) noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    Composition* comp = project_ ? current_composition() : nullptr;
    Layer* l = rig_layer(comp, layerId);
    const i32 i = l ? rig::index_of(l->rig, jointId) : -1;
    if (i < 0) return false;
    history_.before_mutation(*comp, project_->timeline().current(), "apagar junta");
    modelRevision_.fetch_add(1, std::memory_order_acq_rel);
    const u32 up = l->rig.joints[static_cast<u32>(i)].parent;
    l->rig.joints.erase(l->rig.joints.begin() + i);
    for (RigJoint& j : l->rig.joints)
        if (j.parent == jointId) j.parent = up;
    l->tracks.remove_if([&](const Track& t) { return t.property == TrackProperty::RigBone && t.effectParamIndex == jointId; });
    project_->mark_dirty();
    request_render();
    return true;
}

bool Engine::rig_clear(u64 layerId) noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    Composition* comp = project_ ? current_composition() : nullptr;
    Layer* l = rig_layer(comp, layerId);
    if (!l || l->rig.joints.empty()) return false;
    history_.before_mutation(*comp, project_->timeline().current(), "tirar rig");
    modelRevision_.fetch_add(1, std::memory_order_acq_rel);
    l->rig = RigData{};
    l->tracks.remove_if([](const Track& t) { return t.property == TrackProperty::RigBone; });
    project_->mark_dirty();
    request_render();
    return true;
}

u32 Engine::rig_auto_humanoid(u64 layerId) noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    Composition* comp = project_ ? current_composition() : nullptr;
    Layer* l = rig_layer(comp, layerId);
    if (!l) return 0;
    const auto it = images_.find(l->source.pack());
    if (it == images_.end() || !it->second.width || !it->second.height) return 0;
    const ImagePixels& px = it->second;
    // A caixa do que aparece (alfa > 16), amostrada em no máximo ~256×256;
    // foto sem transparência = a imagem inteira.
    const u32 w = px.width, h = px.height;
    f32 x0 = 0, y0 = 0, x1 = static_cast<f32>(w), y1 = static_cast<f32>(h);
    if (px.rgba.size() >= static_cast<usize>(w) * h * 4) {
        const u32 step = std::max(1u, std::max(w, h) / 256u);
        u32 minX = w, minY = h, maxX = 0, maxY = 0;
        for (u32 y = 0; y < h; y += step)
            for (u32 x = 0; x < w; x += step)
                if (px.rgba[(static_cast<usize>(y) * w + x) * 4 + 3] > 16) {
                    minX = std::min(minX, x); maxX = std::max(maxX, x);
                    minY = std::min(minY, y); maxY = std::max(maxY, y);
                }
        if (maxX > minX && maxY > minY) {
            x0 = static_cast<f32>(minX); y0 = static_cast<f32>(minY);
            x1 = static_cast<f32>(std::min(w, maxX + step)); y1 = static_cast<f32>(std::min(h, maxY + step));
        }
    }
    const f32 bw = x1 - x0, bh = y1 - y0;
    if (bw < 4.0f || bh < 4.0f) return 0;
    // Corpo inteiro (alto e estreito) ganha pernas; busto/meio corpo, não.
    const bool fullBody = bh / bw > 1.6f;
    struct T { f32 u, v; i32 parent; };
    // Índices: 0 quadril (raiz), 1 peito, 2 pescoço, 3 cabeça, 4-6 braço esq.,
    // 7-9 braço dir., 10-11 perna esq., 12-13 perna dir. (u, v em 0..1 da caixa).
    const T full[] = {
        {0.50f, 0.52f, -1}, {0.50f, 0.32f, 0}, {0.50f, 0.20f, 1}, {0.50f, 0.06f, 2},
        {0.36f, 0.23f, 1}, {0.28f, 0.37f, 4}, {0.24f, 0.51f, 5},
        {0.64f, 0.23f, 1}, {0.72f, 0.37f, 7}, {0.76f, 0.51f, 8},
        {0.43f, 0.74f, 0}, {0.42f, 0.95f, 10},
        {0.57f, 0.74f, 0}, {0.58f, 0.95f, 12},
    };
    const T bust[] = {
        {0.50f, 0.92f, -1}, {0.50f, 0.62f, 0}, {0.50f, 0.42f, 1}, {0.50f, 0.12f, 2},
        {0.30f, 0.55f, 1}, {0.20f, 0.75f, 4}, {0.18f, 0.95f, 5},
        {0.70f, 0.55f, 1}, {0.80f, 0.75f, 7}, {0.82f, 0.95f, 8},
    };
    const T* tpl = fullBody ? full : bust;
    const u32 n = fullBody ? static_cast<u32>(std::size(full)) : static_cast<u32>(std::size(bust));
    history_.before_mutation(*comp, project_->timeline().current(), "esqueleto automatico");
    modelRevision_.fetch_add(1, std::memory_order_acq_rel);
    l->rig = RigData{};
    l->tracks.remove_if([](const Track& t) { return t.property == TrackProperty::RigBone; });
    for (u32 i = 0; i < n; ++i) {
        RigJoint j;
        j.id = l->rig.nextJointId++;
        j.parent = tpl[i].parent < 0 ? kInvalidIndex : static_cast<u32>(tpl[i].parent);
        j.pos = Vec2{x0 + tpl[i].u * bw, y0 + tpl[i].v * bh};
        l->rig.joints.push_back(j);
    }
    project_->mark_dirty();
    request_render();
    return n;
}

bool Engine::rig_pose_joint(u64 layerId, u32 jointId, f32 compX, f32 compY, bool continuing) noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    Composition* comp = project_ ? current_composition() : nullptr;
    Layer* l = rig_layer(comp, layerId);
    const i32 idx = l ? rig::index_of(l->rig, jointId) : -1;
    if (idx < 0) return false;
    const u32 i = static_cast<u32>(idx);
    const FrameIndex t = playback_.current();
    if (!l->contains_time(t)) return false;
    Vec2 target;
    if (!plane_of(*comp, *l, t).to_layer(Vec2{compX, compY}, target)) return false;
    const FrameIndex local = l->local_time(t);
    std::vector<f32> deg;
    std::vector<rig::Affine> bones;
    rig::sample_angles(*l, local, deg);
    rig::pose(l->rig, deg, bones);
    // Ponta de cadeia (sem filhos) com avô: IK; senão FK do osso da junta.
    bool leaf = true;
    for (const RigJoint& j : l->rig.joints) leaf = leaf && j.parent != jointId;
    struct Change { u32 index; f32 delta; };
    Change changes[2]{};
    u32 count = 0;
    f32 dp = 0.0f, dj = 0.0f;
    if (leaf && rig::ik_two_bone(l->rig, bones, i, target, dp, dj)) {
        const u32 parent = static_cast<u32>(rig::index_of(l->rig, l->rig.joints[i].parent));
        changes[count++] = Change{parent, dp};
        changes[count++] = Change{i, dj};
    } else {
        const f32 d = rig::fk_delta(l->rig, bones, i, target);
        if (l->rig.joints[i].parent == kInvalidIndex || rig::index_of(l->rig, l->rig.joints[i].parent) < 0) return false;
        changes[count++] = Change{i, d};
    }
    if (!continuing) history_.before_mutation(*comp, project_->timeline().current(), "posar rig");
    modelRevision_.fetch_add(1, std::memory_order_acq_rel);
    for (u32 k = 0; k < count; ++k) {
        const Change& c = changes[k];
        Track& tr = l->tracks.get_or_create(TrackProperty::RigBone, kInvalidIndex, l->rig.joints[c.index].id);
        tr.set(local, deg[c.index] + c.delta);
    }
    project_->mark_dirty();
    request_render();
    return true;
}

void Engine::set_rig_setup_layer(u64 layerId) noexcept {
    renderer_.set_rig_setup_layer(layerId);
    request_render();
}

} // namespace aurea
