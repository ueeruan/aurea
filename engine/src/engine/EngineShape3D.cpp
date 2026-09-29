// =============================================================================
//  Aurea / engine / EngineShape3D.cpp
//
//  API das FORMAS 3D (scene3d/Shape3D.hpp): criar a camada, trocar cor e
//  imagem das partes (a receita vira um asset novo — o antigo fica para o
//  desfazer, como no texto 3D) e mover/girar/escalar cada parte no cabeçote
//  (trilhas TrackProperty::ShapePart; canal com keyframe grava keyframe).
//  Padrão de mutação do motor: modelMutex_, history_.before_mutation,
//  modelRevision_, mark_dirty, request_render. `continuing` (arrasto) pula o
//  passo novo de desfazer.
// =============================================================================
#include "aurea/Engine.hpp"

#include "aurea/render/Renderer.hpp"
#include "aurea/scene3d/Shape3D.hpp"

#include <algorithm>
#include <cmath>

namespace aurea {
namespace {

Layer* shape_layer(Composition* comp, u64 id) noexcept {
    Layer* l = comp ? comp->layer(LayerId::unpack(id)) : nullptr;
    return l && l->kind == LayerKind::Model3D ? l : nullptr;
}

/// Camada ← modelo (o mesmo `layer_from_model` do renderer: Y e Z do glTF
/// viram os da composição, metros → px, pivô no centro).
Mat4 layer_from_shape(const Model3DData& m) noexcept {
    Mat4 flip;
    flip.col[1] = Vec4{0, -1, 0, 0};
    flip.col[2] = Vec4{0, 0, -1, 0};
    return flip * Mat4::scale(Vec3{m.unitScale, m.unitScale, m.unitScale}) * Mat4::translation(-m.pivot);
}

Vec3 linear_apply(const Mat4& m, Vec3 v) noexcept {
    const Vec4 r = m * Vec4{v.x, v.y, v.z, 0};
    return Vec3{r.x, r.y, r.z};
}

/// Resolve `m · x = v` só na parte linear 3×3 (Cramer). Falso = singular.
bool linear_solve(const Mat4& m, Vec3 v, Vec3& x) noexcept {
    const Vec3 a{m.col[0].x, m.col[0].y, m.col[0].z}, b{m.col[1].x, m.col[1].y, m.col[1].z}, c{m.col[2].x, m.col[2].y, m.col[2].z};
    const f32 det = a.dot(b.cross(c));
    if (!(std::fabs(det) > 1e-20f)) return false;
    x = Vec3{v.dot(b.cross(c)) / det, a.dot(v.cross(c)) / det, a.dot(b.cross(v)) / det};
    return std::isfinite(x.x) && std::isfinite(x.y) && std::isfinite(x.z);
}

AssetId add_shape_asset(Project& project, const scene3d::SceneAsset& scene, std::string name, std::string source) {
    Asset asset;
    asset.kind = AssetKind::Model3D;
    asset.name = std::move(name);
    asset.sourcePath = std::move(source);
    asset.originalFilename = scene.sourceName;
    asset.model.meshCount = scene.stats.meshes;
    asset.model.materialCount = scene.stats.materials;
    asset.model.triangleCount = scene.stats.triangles;
    asset.model.lodCount = 1;
    return project.add_asset(std::move(asset));
}

/// Os 9 canais da parte no instante local inteiro (o do cabeçote).
void part_channels(const Layer& l, u32 part, FrameIndex local, f32* out9) noexcept {
    for (u32 c = 0; c < scene3d::kShape3DChannels; ++c)
        out9[c] = scene3d::shape3d_part_value(l, part, c, static_cast<f64>(local.value));
}

/// Onde o Shape 3D Layout pôs cada parte no instante (espaço do modelo,
/// antes das trilhas dela) — as alças do palco seguem a parte que se vê.
std::vector<Mat4> laid_out(const scene3d::SceneAsset& asset, const Layer& l, FrameIndex local) {
    std::vector<Mat4> m = asset.rest_world_matrices();
    scene3d::apply_shape3d_layout(asset, l, static_cast<f64>(local.value), m);
    return m;
}

} // namespace

Result<u64> Engine::add_shape3d(u32 kind, const char* name) noexcept {
    if (kind >= scene3d::kShape3DKindCount) return Status{Errc::InvalidArgument, "forma 3D desconhecida"};
    const scene3d::Shape3DSpec spec = scene3d::default_shape3d(static_cast<scene3d::Shape3DKind>(kind));
    scene3d::ImportResult r = scene3d::build_shape3d(spec, [this](const std::string& s) { return resolve_asset_path(s); },
                                                     model_texture_cap());
    if (!r.ok()) return Status{Errc::InvalidArgument, "forma 3D sem geometria"};
    std::shared_ptr<const scene3d::SceneAsset> scene(std::move(r.asset));
    const std::string label = name && *name ? std::string(name) : std::string("Forma 3D");
    std::lock_guard<std::mutex> lock(modelMutex_);
    Composition* comp = project_ ? current_composition() : nullptr;
    if (!comp) return Status{Errc::InvalidState, "nenhum projeto aberto"};
    history_.before_mutation(*comp, project_->timeline().current(), "forma 3D");
    modelRevision_.fetch_add(1, std::memory_order_acq_rel);
    const AssetId assetId = add_shape_asset(*project_, *scene, label, scene3d::encode_shape3d(spec));
    models_[assetId.pack()] = scene;
    const LayerId lid = comp->add_layer(LayerKind::Model3D, label);
    Layer* l = comp->layer(lid);
    if (!l) return Status{Errc::OutOfMemory, "camada nao criada"};
    l->threeD = true;
    l->model.scene = assetId;
    l->model.animationClip = -1;
    // A forma (~1 unidade) ocupa ~45 % do lado menor da composição, no centro.
    const f32 w = static_cast<f32>(comp->width()), h = static_cast<f32>(comp->height());
    const Vec3 ext = scene->bounds.extent();
    l->model.unitScale = 0.45f * std::min(w, h) / std::max({ext.x, ext.y, 1e-3f});
    l->model.pivot = scene->bounds.center();
    l->transform.position = Vec3{w * 0.5f, h * 0.5f, 0.0f};
    l->transform.scale = Vec3{1.0f, 1.0f, 1.0f};
    project_->mark_dirty();
    request_render();
    return lid.pack();
}

Status Engine::set_shape3d(u64 layerId, const scene3d::Shape3DSpec& in) noexcept {
    scene3d::Shape3DSpec spec = in;
    scene3d::normalize_shape3d(spec);
    // Malha e imagens FORA do lock (decodificar uma foto grande leva tempo).
    scene3d::ImportResult r = scene3d::build_shape3d(spec, [this](const std::string& s) { return resolve_asset_path(s); },
                                                     model_texture_cap());
    if (!r.ok()) return Status{Errc::InvalidArgument, "forma 3D sem geometria"};
    std::shared_ptr<const scene3d::SceneAsset> scene(std::move(r.asset));
    std::lock_guard<std::mutex> lock(modelMutex_);
    Composition* comp = project_ ? current_composition() : nullptr;
    Layer* l = shape_layer(comp, layerId);
    if (!l) return Errc::InvalidArgument;
    if (l->locked) return Status{Errc::InvalidState, "camada bloqueada"};
    const Asset* old = project_->asset(l->model.scene);
    scene3d::Shape3DSpec prev;
    if (!old || !scene3d::decode_shape3d(old->sourcePath, prev)) return Status{Errc::InvalidArgument, "selecione uma forma 3D"};
    history_.before_mutation(*comp, project_->timeline().current(), "editar forma 3D");
    modelRevision_.fetch_add(1, std::memory_order_acq_rel);
    const AssetId assetId = add_shape_asset(*project_, *scene, old->name, scene3d::encode_shape3d(spec));
    models_[assetId.pack()] = scene;
    l->model.scene = assetId;
    l->model.animationClip = -1;
    l->model.pivot = scene->bounds.center();
    project_->mark_dirty();
    request_render();
    return OkStatus;
}

bool Engine::query_shape3d(u64 layerId, scene3d::Shape3DSpec& out) noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    Composition* comp = project_ ? current_composition() : nullptr;
    const Layer* l = shape_layer(comp, layerId);
    const Asset* a = l ? project_->asset(l->model.scene) : nullptr;
    return a && scene3d::decode_shape3d(a->sourcePath, out);
}

Status Engine::set_shape3d_part_style(u64 layerId, i32 part, const f32* rgba, const char* image) noexcept {
    scene3d::Shape3DSpec spec;
    if (!query_shape3d(layerId, spec)) return Status{Errc::InvalidArgument, "selecione uma forma 3D"};
    if (part >= static_cast<i32>(spec.parts.size())) return Errc::InvalidArgument;
    std::string stored;
    if (image && *image) stored = store_asset_path(image);
    for (u32 i = 0; i < spec.parts.size(); ++i) {
        if (part >= 0 && static_cast<u32>(part) != i) continue;
        if (rgba) {
            for (u32 c = 0; c < 4; ++c) if (!std::isfinite(rgba[c])) return Errc::InvalidArgument;
            spec.parts[i].color = Vec4{rgba[0], rgba[1], rgba[2], rgba[3]};
        }
        if (image) spec.parts[i].image = stored;
    }
    return set_shape3d(layerId, spec);
}

u32 Engine::query_shape3d_parts(u64 layerId, f32* out, u32 capacity) noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    Composition* comp = project_ ? current_composition() : nullptr;
    const Layer* l = shape_layer(comp, layerId);
    if (!l) return 0;
    const auto it = models_.find(l->model.scene.pack());
    if (it == models_.end() || !it->second || !it->second->shapeParts) return 0;
    const scene3d::SceneAsset& asset = *it->second;
    const u32 parts = static_cast<u32>(std::min<usize>(asset.nodes.size(), scene3d::kShape3DMaxParts));
    const u32 need = parts * kShapePartFloats;
    if (!out || capacity < need) return need;
    const FrameIndex now = playback_.current();
    const FrameIndex local = l->local_time(now);
    const Mat4 world = layer_world_3d(*comp, *l, now) * layer_from_shape(l->model);
    const Mat4 vp = sceneEditor_.enabled ? scene_editor_projection(comp->width(), comp->height(), sceneEditor_)
                                         : comp_view_projection(*comp, now);
    const std::vector<Mat4> laid = laid_out(asset, *l, local);
    for (u32 p = 0; p < parts; ++p) {
        f32* o = out + p * kShapePartFloats;
        part_channels(*l, p, local, o);
        u32 animated = 0, here = 0;
        for (u32 c = 0; c < scene3d::kShape3DChannels; ++c) {
            const Track* t = l->tracks.find(TrackProperty::ShapePart, p, c);
            if (!t || t->keys.empty()) continue;
            animated |= 1u << c;
            if (t->find_exact(local) != kInvalidIndex) here |= 1u << c;
        }
        o[9] = static_cast<f32>(animated);
        o[10] = static_cast<f32>(here);
        const i32 mesh = asset.nodes[p].mesh;
        const Vec3 center = mesh >= 0 && static_cast<usize>(mesh) < asset.meshes.size() ? asset.meshes[static_cast<usize>(mesh)].bounds.center() : Vec3{};
        const Vec3 at = p < laid.size() ? laid[p].transform_point(center) : center;
        const Vec3 wp = world.transform_point(at + Vec3{o[0], o[1], o[2]});
        const Vec4 clip = vp * Vec4{wp.x, wp.y, wp.z, 1};
        const bool front = clip.w > 1e-6f;
        o[11] = front ? clip.x / clip.w : 0.0f;
        o[12] = front ? clip.y / clip.w : 0.0f;
        o[13] = front ? 1.0f : 0.0f;
    }
    return need;
}

bool Engine::set_shape3d_part(u64 layerId, u32 part, const f32* values, u32 mask, bool continuing) noexcept {
    if (!values || part >= scene3d::kShape3DMaxParts) return false;
    mask &= (1u << scene3d::kShape3DChannels) - 1u;
    if (!mask) return false;
    std::lock_guard<std::mutex> lock(modelMutex_);
    Composition* comp = project_ ? current_composition() : nullptr;
    Layer* l = shape_layer(comp, layerId);
    if (!l || l->locked) return false;
    const auto it = models_.find(l->model.scene.pack());
    if (it == models_.end() || !it->second || !it->second->shapeParts || part >= it->second->nodes.size()) return false;
    const FrameIndex now = playback_.current();
    const FrameIndex local = l->local_time(now);
    const bool inside = l->contains_time(now);
    for (u32 c = 0; c < scene3d::kShape3DChannels; ++c) {
        if (!(mask & (1u << c)) || !std::isfinite(values[c])) continue;
        const Track* t = l->tracks.find(TrackProperty::ShapePart, part, c);
        // Canal animado fora da camada: não há onde gravar a key.
        if (t && !t->keys.empty() && !inside) return false;
    }
    if (!continuing) history_.before_mutation(*comp, project_->timeline().current(), "mover parte");
    modelRevision_.fetch_add(1, std::memory_order_acq_rel);
    for (u32 c = 0; c < scene3d::kShape3DChannels; ++c) {
        if (!(mask & (1u << c)) || !std::isfinite(values[c])) continue;
        const f32 v = c >= 6 ? std::clamp(values[c], -100.0f, 100.0f) : std::clamp(values[c], -1.0e5f, 1.0e5f);
        Track& t = l->tracks.get_or_create(TrackProperty::ShapePart, part, c);
        if (t.keys.empty()) t.staticValue = v;
        else t.set(local, v);
    }
    project_->mark_dirty();
    request_render();
    return true;
}

i32 Engine::toggle_shape3d_part_key(u64 layerId, u32 part) noexcept {
    if (part >= scene3d::kShape3DMaxParts) return -1;
    std::lock_guard<std::mutex> lock(modelMutex_);
    Composition* comp = project_ ? current_composition() : nullptr;
    Layer* l = shape_layer(comp, layerId);
    if (!l || l->locked) return -1;
    const auto it = models_.find(l->model.scene.pack());
    if (it == models_.end() || !it->second || !it->second->shapeParts || part >= it->second->nodes.size()) return -1;
    const FrameIndex now = playback_.current();
    if (!l->contains_time(now)) return -1;
    const FrameIndex local = l->local_time(now);
    bool any = false;
    for (u32 c = 0; c < scene3d::kShape3DChannels; ++c) {
        const Track* t = l->tracks.find(TrackProperty::ShapePart, part, c);
        any = any || (t && t->find_exact(local) != kInvalidIndex);
    }
    f32 cur[scene3d::kShape3DChannels];
    part_channels(*l, part, local, cur);
    history_.before_mutation(*comp, project_->timeline().current(), any ? "tirar keyframe da parte" : "keyframe da parte");
    modelRevision_.fetch_add(1, std::memory_order_acq_rel);
    for (u32 c = 0; c < scene3d::kShape3DChannels; ++c) {
        if (any) {
            Track* t = l->tracks.find(TrackProperty::ShapePart, part, c);
            if (!t || t->find_exact(local) == kInvalidIndex) continue;
            t->remove(local);
            // Sem key nenhuma, a parte fica onde estava (o valor parado = o de agora).
            if (t->keys.empty()) t->staticValue = cur[c];
        } else {
            Track& t = l->tracks.get_or_create(TrackProperty::ShapePart, part, c);
            if (t.keys.empty()) t.staticValue = cur[c];
            t.set(local, cur[c]);
        }
    }
    project_->mark_dirty();
    request_render();
    return any ? 0 : 1;
}

bool Engine::reset_shape3d_part(u64 layerId, i32 part) noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    Composition* comp = project_ ? current_composition() : nullptr;
    Layer* l = shape_layer(comp, layerId);
    if (!l || l->locked) return false;
    bool has = false;
    for (u32 i = 0; i < l->tracks.size(); ++i) {
        const Track& t = l->tracks.at(i);
        has = has || (t.property == TrackProperty::ShapePart && (part < 0 || t.effectIndex == static_cast<u32>(part)));
    }
    if (!has) return false;
    history_.before_mutation(*comp, project_->timeline().current(), "resetar parte");
    modelRevision_.fetch_add(1, std::memory_order_acq_rel);
    l->tracks.remove_if([&](const Track& t) {
        return t.property == TrackProperty::ShapePart && (part < 0 || t.effectIndex == static_cast<u32>(part));
    });
    project_->mark_dirty();
    request_render();
    return true;
}

bool Engine::query_shape3d_part_gizmo(u64 layerId, u32 part, f32 length, f32* out, bool localSpace) noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    Composition* comp = project_ ? current_composition() : nullptr;
    const Layer* l = shape_layer(comp, layerId);
    if (!l || !out || !std::isfinite(length)) return false;
    const auto it = models_.find(l->model.scene.pack());
    if (it == models_.end() || !it->second || !it->second->shapeParts || part >= it->second->nodes.size()) return false;
    const scene3d::SceneAsset& asset = *it->second;
    const FrameIndex now = playback_.current();
    const FrameIndex local = l->local_time(now);
    f32 c[scene3d::kShape3DChannels];
    part_channels(*l, part, local, c);
    const i32 mesh = asset.nodes[part].mesh;
    const Vec3 center = mesh >= 0 && static_cast<usize>(mesh) < asset.meshes.size() ? asset.meshes[static_cast<usize>(mesh)].bounds.center() : Vec3{};
    const Mat4 world = layer_world_3d(*comp, *l, now) * layer_from_shape(l->model);
    const std::vector<Mat4> laid = laid_out(asset, *l, local);
    const Mat4 layout = part < laid.size() ? laid[part] : Mat4{};
    const Vec3 at = layout.transform_point(center);
    const Mat4 partWorld = world * scene3d::shape3d_part_matrix(c, at) * layout;
    const Vec3 o = world.transform_point(at + Vec3{c[0], c[1], c[2]});
    const Mat4 vp = sceneEditor_.enabled ? scene_editor_projection(comp->width(), comp->height(), sceneEditor_)
                                         : comp_view_projection(*comp, now);
    auto proj = [&](Vec3 p, f32* xy) {
        const Vec4 q = vp * Vec4{p.x, p.y, p.z, 1};
        if (!(q.w > 1e-6f)) return false;
        xy[0] = q.x / q.w;
        xy[1] = q.y / q.w;
        return true;
    };
    // Os eixos da composição (X direita, Y para baixo, Z para dentro), ou os
    // da própria parte já girada (espaço local).
    auto axis = [&](u32 i) {
        if (localSpace) return linear_apply(partWorld, Vec3{i == 0 ? 1.0f : 0.0f, i == 1 ? -1.0f : 0.0f, i == 2 ? -1.0f : 0.0f}).normalized() * length;
        return Vec3{i == 0 ? length : 0, i == 1 ? length : 0, i == 2 ? length : 0};
    };
    return proj(o, out) && proj(o + axis(0), out + 2) && proj(o + axis(1), out + 4) && proj(o + axis(2), out + 6);
}

bool Engine::shape3d_part_move(u64 layerId, u32 part, u32 axis, f32 amount, f32* out) noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    Composition* comp = project_ ? current_composition() : nullptr;
    const Layer* l = shape_layer(comp, layerId);
    if (!l || !out || axis > 5 || !std::isfinite(amount) || part >= scene3d::kShape3DMaxParts) return false;
    const FrameIndex now = playback_.current();
    const FrameIndex local = l->local_time(now);
    f32 c[scene3d::kShape3DChannels];
    part_channels(*l, part, local, c);
    const Mat4 world = layer_world_3d(*comp, *l, now) * layer_from_shape(l->model);
    Vec3 d{axis == 0 ? amount : 0.0f, axis == 1 ? amount : 0.0f, axis == 2 ? amount : 0.0f};
    if (axis >= 3) {
        const Mat4 partWorld = world * scene3d::shape3d_part_matrix(c, Vec3{});
        const u32 i = axis - 3;
        d = linear_apply(partWorld, Vec3{i == 0 ? 1.0f : 0.0f, i == 1 ? -1.0f : 0.0f, i == 2 ? -1.0f : 0.0f}).normalized() * amount;
    }
    // Passo no mundo → passo no espaço do modelo (onde mora a posição da parte).
    Vec3 dm;
    if (!linear_solve(world, d, dm)) return false;
    out[0] = c[0] + dm.x;
    out[1] = c[1] + dm.y;
    out[2] = c[2] + dm.z;
    return true;
}

} // namespace aurea
