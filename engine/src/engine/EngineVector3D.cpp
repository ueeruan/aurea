// =============================================================================
//  Aurea / engine / EngineVector3D.cpp
//
//  Editar pontos de um caminho vetorial numa camada 3D, direto no palco:
//   - a conta grupo → composição pela câmera que o palco mostra (a ativa, ou
//     a de navegação da Cena 3D) — a MESMA projeção do render;
//   - a volta composição → grupo: raio da câmera × plano da camada;
//   - o arrasto de pontos e alças leva o dedo ao plano e aplica MovePoints /
//     MoveHandle por Engine::edit_vector_path (keyframes de forma, Auto-Key e
//     um passo de desfazer por arrasto ficam como no 2D).
//  Os pontos continuam planos: o BezierPath não tem profundidade por ponto e
//  o formato salvo não muda. Sem exceções (o Android compila sem elas).
// =============================================================================
#include "EngineVector3D.hpp"

#include "aurea/Engine.hpp"
#include "aurea/timeline/ShapeGeometry.hpp"

#include <algorithm>
#include <cmath>

namespace aurea {
namespace vector3d {
namespace {

Mat4 mat4_of(const vector::Affine2& a) noexcept {
    Mat4 m;
    m.col[0] = Vec4{a.a, a.b, 0.0f, 0.0f};
    m.col[1] = Vec4{a.c, a.d, 0.0f, 0.0f};
    m.col[3] = Vec4{a.tx, a.ty, 0.0f, 1.0f};
    return m;
}

Vec3 xyz(const Vec4& v) noexcept { return Vec3{v.x, v.y, v.z}; }

} // namespace

StageFrame stage_frame(const Composition& comp, const Layer& l, FrameIndex now, const vector::Affine2& group,
                       const SceneEditorView* editor) noexcept {
    StageFrame f;
    f.width = std::max(1u, comp.width());
    f.height = std::max(1u, comp.height());
    const Mat4 g = mat4_of(group);
    f.perspective = wants_layer_3d(comp, l, now);
    if (!f.perspective) {
        f.compFromGroup = layer_world_matrix(comp, l, now) * g;
        return f;
    }
    f.worldFromGroup = layer_world_3d(comp, l, now) * g;
    // A vista de navegação da Cena 3D é o que o palco mostra quando ligada.
    if (editor && editor->enabled) {
        f.camera = scene_editor_camera(f.width, f.height, *editor);
        f.compFromGroup = scene_editor_projection(f.width, f.height, *editor) * f.worldFromGroup;
    } else {
        f.camera = comp_camera(comp, now, f.width, f.height);
        f.compFromGroup = comp_view_projection(comp, now) * f.worldFromGroup;
    }
    return f;
}

bool project(const StageFrame& f, Vec2 g, Vec2& out) noexcept {
    const Vec4 q = f.compFromGroup * Vec4{g.x, g.y, 0.0f, 1.0f};
    if (!std::isfinite(q.w) || !(q.w > kNearW)) return false;
    out = Vec2{q.x / q.w, q.y / q.w};
    return std::isfinite(out.x) && std::isfinite(out.y);
}

bool unproject(const StageFrame& f, f32 x, f32 y, Vec2& out) noexcept {
    if (!std::isfinite(x) || !std::isfinite(y)) return false;
    if (!f.perspective) {
        // 2D: a inversa da afim (as colunas x, y e a translação).
        const Mat4& m = f.compFromGroup;
        const f64 a = m.col[0].x, b = m.col[0].y, c = m.col[1].x, d = m.col[1].y;
        const f64 det = a * d - b * c;
        if (!std::isfinite(det) || std::fabs(det) < 1e-12) return false;
        const f64 rx = f64(x) - m.col[3].x, ry = f64(y) - m.col[3].y;
        out = Vec2{static_cast<f32>((d * rx - c * ry) / det), static_cast<f32>((a * ry - b * rx) / det)};
        return std::isfinite(out.x) && std::isfinite(out.y);
    }
    // 3D: o raio da câmera sob o dedo contra o plano z = 0 da camada.
    Vec3 origin, dir;
    if (!scene_ray(f.camera, f.width, f.height, x, y, origin, dir)) return false;
    const Vec3 p0 = xyz(f.worldFromGroup.col[3]);
    const Vec3 u = xyz(f.worldFromGroup.col[0]), v = xyz(f.worldFromGroup.col[1]);
    const Vec3 n = u.cross(v);
    const f64 nn = std::sqrt(f64(n.dot(n)));
    const f64 den = f64(dir.dot(n));
    // De lado para a câmera (o raio quase paralelo ao plano): não há ponto estável.
    if (!(nn > 1e-12) || std::fabs(den) < 1e-4 * nn) return false;
    const f64 t = f64((p0 - origin).dot(n)) / den;
    if (!std::isfinite(t) || t <= 0.0) return false;   // o plano está atrás da câmera
    const Vec3 r = origin + dir * static_cast<f32>(t) - p0;
    // r = s·u + k·v (mínimos quadrados: u e v não precisam ser ortogonais).
    const f64 uu = u.dot(u), uv = u.dot(v), vv = v.dot(v), ur = u.dot(r), vr = v.dot(r);
    const f64 det = uu * vv - uv * uv;
    if (!std::isfinite(det) || std::fabs(det) < 1e-18) return false;
    out = Vec2{static_cast<f32>((vv * ur - uv * vr) / det), static_cast<f32>((uu * vr - uv * ur) / det)};
    if (!std::isfinite(out.x) || !std::isfinite(out.y)) return false;
    // O ponto do plano tem de cair na frente da câmera também pela projeção.
    Vec2 back;
    return project(f, out, back);
}

} // namespace vector3d

namespace {

const Layer* stage_vector_layer(const Composition* comp, u64 id) noexcept {
    const Layer* l = comp ? comp->layer(LayerId::unpack(id)) : nullptr;
    return l && l->kind == LayerKind::Shape && l->shape.shapeType == kShapeVector ? l : nullptr;
}

} // namespace

bool Engine::vector_path_stage_points(u64 layerId, u32 group, u32 path, std::vector<f32>& out) noexcept {
    out.clear();
    std::lock_guard<std::mutex> lock(modelMutex_);
    InlineEditScope inlineScope(*this, layerId);
    const BoneAttachScope bones(&Engine::model_lookup, this);
    const Composition* comp = project_ ? current_composition() : nullptr;
    const Layer* l = stage_vector_layer(comp, layerId);
    if (!l || group >= l->shape.vector.groups.size() || path >= l->shape.vector.groups[group].paths.size()) return false;
    const FrameIndex now = editing_frame_locked();
    const f64 local = static_cast<f64>(l->local_time(now).value);
    const VectorGroup g = vector::evaluate_group(l->shape.vector.groups[group], l->tracks, group, local, comp->fps());
    const vector3d::StageFrame f = vector3d::stage_frame(*comp, *l, now, vector::group_matrix(g), sceneEditor_.enabled ? &sceneEditor_ : nullptr);
    const BezierPath b = vector::path_at(g.paths[path], local);
    out = {static_cast<f32>(b.v.size()), f.perspective ? 1.0f : 0.0f, b.closed ? 1.0f : 0.0f};
    out.reserve(3 + b.v.size() * kVectorStageFloats);
    const auto put = [&](Vec2 p) {
        Vec2 s;
        const bool on = vector3d::project(f, p, s);
        out.push_back(on ? s.x : 0.0f);
        out.push_back(on ? s.y : 0.0f);
        out.push_back(on ? 1.0f : 0.0f);
    };
    for (const BezierVertex& v : b.v) {
        put(v.p);
        put(v.p + v.in);
        put(v.p + v.out);
    }
    return true;
}

bool Engine::vector_path_unproject(u64 layerId, u32 group, f32 compX, f32 compY, Vec2& out) noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    InlineEditScope inlineScope(*this, layerId);
    const BoneAttachScope bones(&Engine::model_lookup, this);
    const Composition* comp = project_ ? current_composition() : nullptr;
    const Layer* l = stage_vector_layer(comp, layerId);
    if (!l || group >= l->shape.vector.groups.size()) return false;
    const FrameIndex now = editing_frame_locked();
    const f64 local = static_cast<f64>(l->local_time(now).value);
    const VectorGroup g = vector::evaluate_group(l->shape.vector.groups[group], l->tracks, group, local, comp->fps());
    const vector3d::StageFrame f = vector3d::stage_frame(*comp, *l, now, vector::group_matrix(g), sceneEditor_.enabled ? &sceneEditor_ : nullptr);
    return vector3d::unproject(f, compX, compY, out);
}

i32 Engine::drag_vector_path_on_stage(u64 layerId, u32 group, u32 path, u32 handle, const u32* indices, u32 count,
                                      f32 fromX, f32 fromY, f32 toX, f32 toY, u32 locks, bool continuing) noexcept {
    if (!indices || count == 0 || handle > 2) return -1;
    Vec2 a, b;
    {
        std::lock_guard<std::mutex> lock(modelMutex_);
        InlineEditScope inlineScope(*this, layerId);
        const BoneAttachScope bones(&Engine::model_lookup, this);
        const Composition* comp = project_ ? current_composition() : nullptr;
        const Layer* l = stage_vector_layer(comp, layerId);
        if (!l || l->locked || group >= l->shape.vector.groups.size() || path >= l->shape.vector.groups[group].paths.size()) return -1;
        const FrameIndex now = editing_frame_locked();
        const f64 local = static_cast<f64>(l->local_time(now).value);
        const VectorGroup g = vector::evaluate_group(l->shape.vector.groups[group], l->tracks, group, local, comp->fps());
        const vector3d::StageFrame f = vector3d::stage_frame(*comp, *l, now, vector::group_matrix(g), sceneEditor_.enabled ? &sceneEditor_ : nullptr);
        // Os dois toques no plano da camada: o deslocamento é o do plano, não o da tela.
        if (!vector3d::unproject(f, fromX, fromY, a) || !vector3d::unproject(f, toX, toY, b)) return -1;
    }
    // A regra (travas das alças, keyframe de forma, desfazer) é a do edit_vector_path.
    const Vec2 d = b - a;
    if (handle == 0) {
        std::vector<f32> args;
        args.reserve(2 + count);
        args.push_back(d.x);
        args.push_back(d.y);
        for (u32 i = 0; i < count; ++i) args.push_back(static_cast<f32>(indices[i]));
        return edit_vector_path(layerId, group, path, PathOp::MovePoints, args.data(), static_cast<u32>(args.size()), continuing);
    }
    const f32 args[6] = {static_cast<f32>(indices[0]), handle == 2 ? 1.0f : 0.0f, d.x, d.y,
                         (locks & 1u) ? 1.0f : 0.0f, (locks & 2u) ? 1.0f : 0.0f};
    return edit_vector_path(layerId, group, path, PathOp::MoveHandle, args, 6, continuing);
}

} // namespace aurea
