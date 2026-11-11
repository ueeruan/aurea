// =============================================================================
//  Aurea / engine / EngineVector.cpp
//
//  API da camada vetorial (Fase 7D): criar, ler/escrever o documento, editar
//  caminhos (com keyframes de forma), valores animáveis dos grupos, desenho à
//  mão livre, SVG e texto no caminho. O padrão de toda mutação é o do motor:
//  modelMutex_, history_.before_mutation, modelRevision_, mark_dirty,
//  request_render. `continuing` (arrasto) pula o passo de desfazer novo.
// =============================================================================
#include "aurea/Engine.hpp"
#include "aurea/DefaultNames.hpp"

#include "aurea/timeline/ShapeGeometry.hpp"
#include "aurea/vector/Vector.hpp"

#include <cstdio>

#include <algorithm>
#include <cmath>

namespace aurea {
namespace {

Layer* vector_layer(Composition* comp, u64 id) noexcept {
    Layer* l = comp ? comp->layer(LayerId::unpack(id)) : nullptr;
    return l && l->kind == LayerKind::Shape && l->shape.shapeType == kShapeVector ? l : nullptr;
}

vector::Affine2 affine_of(const Mat4& m) noexcept {
    return vector::Affine2{m.col[0].x, m.col[0].y, m.col[1].x, m.col[1].y, m.col[3].x, m.col[3].y};
}

/// Grupo padrão: branco preenchido (paramétrico) ou contorno branco (livre).
VectorGroup default_group(u32 kind, Vec2 center, f32 base, u32 index) {
    VectorGroup g;
    VectorPath p;
    p.kind = static_cast<VectorPathKind>(std::min<u32>(kind, 4));
    p.center = center;
    p.size = Vec2{base, base};
    p.outerRadius = base * 0.5f;
    p.innerRadius = base * 0.22f;
    p.points = p.kind == VectorPathKind::Polygon ? 6.0f : 5.0f;
    p.path.closed = false;
    g.paths.push_back(p);
    g.fill.paint.color = Vec4{1, 1, 1, 1};
    g.stroke.paint.color = Vec4{1, 1, 1, 1};
    if (p.kind == VectorPathKind::Free) {
        g.fill.enabled = false;
        g.stroke.enabled = true;
        g.stroke.width = 6.0f;
        g.stroke.cap = 1;
        g.stroke.join = 1;
        g.name = default_name(DefaultName::Path) + " " + std::to_string(index + 1);
    } else {
        static constexpr DefaultName kNames[] = {DefaultName::Path, DefaultName::Rectangle, DefaultName::Ellipse,
                                                 DefaultName::Polygon, DefaultName::Star};
        g.name = default_name(kNames[static_cast<u32>(p.kind)]) + " " + std::to_string(index + 1);
    }
    return g;
}

/// Parâmetro animável do grupo: os comuns (VectorParam) e os dos operadores.
bool valid_vector_param(u32 param) noexcept { return param < kVecParamCount || is_vector_modifier_param(param); }

/// Faixa do parâmetro: a do VectorParam ou a do tipo do operador.
f32 clamp_group_param(const VectorGroup& g, u32 param, f32 v) noexcept {
    if (is_vector_modifier_param(param)) {
        const u32 m = (param - kVecModParamBase) / kVecModParams;
        if (m < g.modifiers.size()) return vector::clamp_modifier_param(g.modifiers[m].type, (param - kVecModParamBase) % kVecModParams, v);
    }
    return vector::clamp_param(param, v);
}

/// Trilhas dos operadores do grupo quando a lista muda: `map(índice antigo)`
/// devolve o novo índice ou -1 (operador removido: a trilha sai).
template <class Map>
void remap_modifier_tracks(Layer& l, u32 group, Map map) {
    l.tracks.remove_if([&](const Track& t) {
        return t.property == TrackProperty::VectorParam && t.effectIndex == group && is_vector_modifier_param(t.effectParamIndex)
            && map((t.effectParamIndex - kVecModParamBase) / kVecModParams) < 0;
    });
    for (u32 i = 0; i < l.tracks.size(); ++i) {
        Track& t = l.tracks.at(i);
        if (t.property != TrackProperty::VectorParam || t.effectIndex != group || !is_vector_modifier_param(t.effectParamIndex)) continue;
        const u32 slot = (t.effectParamIndex - kVecModParamBase) % kVecModParams;
        const i32 to = map((t.effectParamIndex - kVecModParamBase) / kVecModParams);
        t.effectParamIndex = vector_modifier_param(static_cast<u32>(to), slot);
    }
}

/// Trilhas do grupo removido saem; as dos seguintes descem um índice.
void drop_group_tracks(Layer& l, u32 group) {
    l.tracks.remove_if([&](const Track& t) { return t.property == TrackProperty::VectorParam && t.effectIndex == group; });
    for (u32 i = 0; i < l.tracks.size(); ++i) {
        Track& t = l.tracks.at(i);
        if (t.property == TrackProperty::VectorParam && t.effectIndex > group) --t.effectIndex;
    }
}

} // namespace

Result<u64> Engine::add_vector_layer(u32 preset) noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    if (!project_) return Status{Errc::InvalidState, "nenhum projeto aberto"};
    Composition* comp = current_composition();
    if (!comp) return Status{Errc::InvalidState, "projeto sem composicao"};
    history_.before_mutation(*comp, project_->timeline().current(), "camada vetorial");
    bump_model_revision_locked();
    const LayerId lid = comp->add_layer(LayerKind::Shape, preset == 0 ? "Desenho vetorial" : "Vetor");
    Layer* l = comp->layer(lid);
    if (!l) return Status{Errc::OutOfMemory, "camada nao criada"};
    const f32 w = static_cast<f32>(comp->width()), h = static_cast<f32>(comp->height());
    ShapeData& sh = l->shape;
    sh.shapeType = kShapeVector;
    sh.bounds = Rect{0.0f, 0.0f, w, h};
    sh.vector.groups.push_back(default_group(std::min<u32>(preset, 4), Vec2{w * 0.5f, h * 0.5f}, 0.3f * std::min(w, h), 0));
    const i64 t = std::clamp<i64>(playback_.current().value, 0, std::max<i64>(0, comp->duration().value - 1));
    l->start = FrameIndex{t};
    l->end = FrameIndex{new_layer_end_locked(comp, l, t)};
    l->transform.anchor = Vec3{w * 0.5f, h * 0.5f, 0.0f};
    l->transform.position = Vec3{w * 0.5f, h * 0.5f, 0.0f};
    project_->mark_dirty();
    request_render();
    return lid.pack();
}

bool Engine::vector_document(u64 layerId, std::vector<f32>& out, std::string& names) noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    const Layer* l = vector_layer(project_ ? current_composition() : nullptr, layerId);
    if (!l) return false;
    vector::encode_document(l->shape.vector, out, names);
    return true;
}

bool Engine::set_vector_document(u64 layerId, const f32* data, usize count, const std::string& names, bool continuing) noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    Composition* comp = project_ ? current_composition() : nullptr;
    Layer* l = vector_layer(comp, layerId);
    if (!l) return false;
    VectorData d;
    if (!vector::decode_document(data, count, names, d) || d.groups.size() != l->shape.vector.groups.size()) return false;
    // Omega conteúdo (área 06): um fluxo anterior à v3 não carrega o furo, o
    // apagar, o perfil do pincel nem a marca de desenho — ficam os da camada.
    // Um fluxo anterior à v6 não carrega os operadores de caminho nem a escala
    // Y do repetidor: ficam os da camada (as trilhas deles continuam valendo).
    if (count > 0 && data[0] < 6.0f) {
        const VectorData& cur = l->shape.vector;
        for (usize gi = 0; gi < d.groups.size(); ++gi) {
            d.groups[gi].modifiers = cur.groups[gi].modifiers;
            d.groups[gi].repeater.scaleY = cur.groups[gi].repeater.scaleY;
            d.groups[gi].repeater.scaleLinked = cur.groups[gi].repeater.scaleLinked;
        }
    }
    if (count > 0 && data[0] < 3.0f) {
        const VectorData& cur = l->shape.vector;
        d.drawing = cur.drawing;
        for (usize gi = 0; gi < d.groups.size(); ++gi) {
            d.groups[gi].composite = cur.groups[gi].composite;
            d.groups[gi].stroke.widthProfile = cur.groups[gi].stroke.widthProfile;
            if (d.groups[gi].paths.size() != cur.groups[gi].paths.size()) continue;
            for (usize pi = 0; pi < d.groups[gi].paths.size(); ++pi) d.groups[gi].paths[pi].exclude = cur.groups[gi].paths[pi].exclude;
        }
    }
    if (d == l->shape.vector) return true;
    if (!continuing) history_.before_mutation(*comp, project_->timeline().current(), "camada vetorial");
    bump_model_revision_locked();
    l->shape.vector = std::move(d);
    project_->mark_dirty();
    request_render();
    return true;
}

bool Engine::vector_path_at(u64 layerId, u32 group, u32 path, std::vector<f32>& out) noexcept {
    out.clear();
    std::lock_guard<std::mutex> lock(modelMutex_);
    Composition* comp = project_ ? current_composition() : nullptr;
    const Layer* l = vector_layer(comp, layerId);
    if (!l || group >= l->shape.vector.groups.size() || path >= l->shape.vector.groups[group].paths.size()) return false;
    const FrameIndex now = playback_.current();
    const f64 local = static_cast<f64>(l->local_time(now).value);
    const VectorGroup g = vector::evaluate_group(l->shape.vector.groups[group], l->tracks, group, local, comp->fps());
    const vector::Affine2 M = affine_of(layer_world_matrix(*comp, *l, now)) * vector::group_matrix(g);
    const VectorPath& p = g.paths[path];
    u32 flags = p.kind == VectorPathKind::Free ? 1u : 0u;
    if (!p.keys.empty()) flags |= 2u;
    for (const PathKey& k : p.keys) if (k.frame == static_cast<i64>(local)) flags |= 4u;
    out = {M.a, M.b, M.c, M.d, M.tx, M.ty, static_cast<f32>(flags)};
    vector::encode_path(vector::path_at(p, local), out);
    return true;
}

bool Engine::set_vector_path(u64 layerId, u32 group, u32 path, const f32* bez, usize count, bool continuing) noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    Composition* comp = project_ ? current_composition() : nullptr;
    Layer* l = vector_layer(comp, layerId);
    if (!l || !bez || group >= l->shape.vector.groups.size() || path >= l->shape.vector.groups[group].paths.size()) return false;
    BezierPath b;
    usize pos = 0;
    if (!vector::decode_path(bez, count, pos, b)) return false;
    if (!continuing) history_.before_mutation(*comp, project_->timeline().current(), "editar caminho");
    bump_model_revision_locked();
    VectorPath& p = l->shape.vector.groups[group].paths[path];
    if (p.kind != VectorPathKind::Free) vector::make_editable(p);
    if (p.keys.empty()) {
        p.path = std::move(b);
    } else {
        // Forma animada: o que se edita é o keyframe do cabeçote.
        const i64 local = l->local_time(playback_.current()).value;
        auto it = std::find_if(p.keys.begin(), p.keys.end(), [&](const PathKey& k) { return k.frame == local; });
        if (it != p.keys.end()) {
            it->path = std::move(b);
        } else {
            PathKey k;
            k.frame = local;
            k.path = std::move(b);
            p.keys.insert(std::upper_bound(p.keys.begin(), p.keys.end(), local, [](i64 f, const PathKey& x) { return f < x.frame; }), std::move(k));
        }
    }
    project_->mark_dirty();
    request_render();
    return true;
}

bool Engine::ensure_vector_path_key(u64 layerId, u32 group, u32 path) noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    Composition* comp = project_ ? current_composition() : nullptr;
    Layer* l = vector_layer(comp, layerId);
    if (!l || group >= l->shape.vector.groups.size() || path >= l->shape.vector.groups[group].paths.size()) return false;
    history_.before_mutation(*comp, project_->timeline().current(), "keyframe de forma");
    bump_model_revision_locked();
    VectorPath& p = l->shape.vector.groups[group].paths[path];
    if (p.kind != VectorPathKind::Free) vector::make_editable(p);
    const i64 local = l->local_time(playback_.current()).value;
    PathKey nk;
    nk.frame = local;
    nk.path = vector::path_at(p, static_cast<f64>(local));
    auto it = std::find_if(p.keys.begin(), p.keys.end(), [&](const PathKey& k) { return k.frame == local; });
    if (it != p.keys.end()) {
        it->path = std::move(nk.path);   // já havia keyframe aqui: regrava a forma (a curva fica)
    } else {
        p.keys.insert(std::upper_bound(p.keys.begin(), p.keys.end(), local,
                                       [](i64 f, const PathKey& x) { return f < x.frame; }),
                      std::move(nk));
    }
    project_->mark_dirty();
    request_render();
    return true;
}

bool Engine::toggle_vector_path_key(u64 layerId, u32 group, u32 path) noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    Composition* comp = project_ ? current_composition() : nullptr;
    Layer* l = vector_layer(comp, layerId);
    if (!l || group >= l->shape.vector.groups.size() || path >= l->shape.vector.groups[group].paths.size()) return false;
    history_.before_mutation(*comp, project_->timeline().current(), "keyframe de forma");
    bump_model_revision_locked();
    VectorPath& p = l->shape.vector.groups[group].paths[path];
    if (p.kind != VectorPathKind::Free) vector::make_editable(p);
    const i64 local = l->local_time(playback_.current()).value;
    auto it = std::find_if(p.keys.begin(), p.keys.end(), [&](const PathKey& k) { return k.frame == local; });
    if (it != p.keys.end()) {
        // Tirar o último keyframe devolve a forma parada (a do instante).
        if (p.keys.size() == 1) p.path = it->path;
        p.keys.erase(it);
    } else {
        PathKey k;
        k.frame = local;
        k.path = vector::path_at(p, static_cast<f64>(local));
        p.keys.insert(std::upper_bound(p.keys.begin(), p.keys.end(), local, [](i64 f, const PathKey& x) { return f < x.frame; }), std::move(k));
    }
    project_->mark_dirty();
    request_render();
    return true;
}

bool Engine::toggle_vector_path_points_key(u64 layerId, u32 group, u32 path, const u32* indices, u32 count) noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    Composition* comp = project_ ? current_composition() : nullptr;
    Layer* l = vector_layer(comp, layerId);
    if (!l || !indices || count == 0 || group >= l->shape.vector.groups.size() || path >= l->shape.vector.groups[group].paths.size()) return false;
    VectorPath& p = l->shape.vector.groups[group].paths[path];
    VectorPath work = p;
    if (work.kind != VectorPathKind::Free) vector::make_editable(work);
    const i64 local = l->local_time(playback_.current()).value;
    BezierPath now = vector::path_at(work, static_cast<f64>(local));
    const usize n = now.v.size();
    std::vector<u8> chosen(n, 0);
    u32 valid = 0;
    for (u32 i = 0; i < count; ++i) {
        if (indices[i] >= n) continue;
        if (!chosen[indices[i]]) ++valid;
        chosen[indices[i]] = 1;
    }
    if (valid == 0) return false;
    history_.before_mutation(*comp, project_->timeline().current(), "keyframe de pontos");
    bump_model_revision_locked();
    if (p.kind != VectorPathKind::Free) vector::make_editable(p);
    auto it = std::find_if(p.keys.begin(), p.keys.end(), [&](const PathKey& k) { return k.frame == local; });
    if (it == p.keys.end()) {
        PathKey k;
        k.frame = local;
        k.path = std::move(now);
        if (valid < n) k.points = std::move(chosen);
        p.keys.insert(std::upper_bound(p.keys.begin(), p.keys.end(), local, [](i64 f, const PathKey& x) { return f < x.frame; }), std::move(k));
    } else {
        std::vector<u8> mask = it->points.size() == n ? it->points : std::vector<u8>(n, 1);
        bool allOn = true;
        for (usize i = 0; i < n; ++i) allOn &= !chosen[i] || mask[i];
        for (usize i = 0; i < n; ++i) if (chosen[i]) mask[i] = allOn ? 0 : 1;
        const usize on = static_cast<usize>(std::count(mask.begin(), mask.end(), u8{1}));
        // O key guarda a forma do instante nos pontos que passam a valer.
        if (!allOn) for (usize i = 0; i < n && i < it->path.v.size(); ++i) if (chosen[i]) it->path.v[i] = now.v[i];
        if (on == 0) {
            if (p.keys.size() == 1) p.path = it->path;
            p.keys.erase(it);
        } else {
            it->points = on == n ? std::vector<u8>{} : std::move(mask);
        }
    }
    project_->mark_dirty();
    request_render();
    return true;
}

i32 Engine::add_vector_group(u64 layerId, u32 pathKind) noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    Composition* comp = project_ ? current_composition() : nullptr;
    Layer* l = vector_layer(comp, layerId);
    if (!l || l->shape.vector.groups.size() >= 4096) return -1;
    history_.before_mutation(*comp, project_->timeline().current(), "novo grupo vetorial");
    bump_model_revision_locked();
    const Rect b = l->shape.bounds;
    const u32 idx = static_cast<u32>(l->shape.vector.groups.size());
    l->shape.vector.groups.push_back(default_group(pathKind, Vec2{b.x + b.w * 0.5f, b.y + b.h * 0.5f}, 0.3f * std::max(1.0f, std::min(b.w, b.h)), idx));
    project_->mark_dirty();
    request_render();
    return static_cast<i32>(idx);
}

bool Engine::remove_vector_group(u64 layerId, u32 group) noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    Composition* comp = project_ ? current_composition() : nullptr;
    Layer* l = vector_layer(comp, layerId);
    if (!l || group >= l->shape.vector.groups.size()) return false;
    history_.before_mutation(*comp, project_->timeline().current(), "remover grupo vetorial");
    bump_model_revision_locked();
    l->shape.vector.groups.erase(l->shape.vector.groups.begin() + group);
    drop_group_tracks(*l, group);
    project_->mark_dirty();
    request_render();
    return true;
}

i32 Engine::add_vector_path(u64 layerId, u32 group, u32 pathKind, const f32* bez, usize count) noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    Composition* comp = project_ ? current_composition() : nullptr;
    Layer* l = vector_layer(comp, layerId);
    if (!l || group >= l->shape.vector.groups.size()) return -1;
    VectorGroup& g = l->shape.vector.groups[group];
    VectorPath p = default_group(pathKind, Vec2{l->shape.bounds.w * 0.5f, l->shape.bounds.h * 0.5f},
                                 0.25f * std::max(1.0f, std::min(l->shape.bounds.w, l->shape.bounds.h)), 0).paths[0];
    if (bez && count > 0) {
        usize pos = 0;
        if (!vector::decode_path(bez, count, pos, p.path)) return -1;
        p.kind = VectorPathKind::Free;
    }
    history_.before_mutation(*comp, project_->timeline().current(), "novo caminho");
    bump_model_revision_locked();
    g.paths.push_back(std::move(p));
    project_->mark_dirty();
    request_render();
    return static_cast<i32>(g.paths.size() - 1);
}

bool Engine::remove_vector_path(u64 layerId, u32 group, u32 path) noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    Composition* comp = project_ ? current_composition() : nullptr;
    Layer* l = vector_layer(comp, layerId);
    if (!l || group >= l->shape.vector.groups.size() || path >= l->shape.vector.groups[group].paths.size()) return false;
    history_.before_mutation(*comp, project_->timeline().current(), "remover caminho");
    bump_model_revision_locked();
    auto& ps = l->shape.vector.groups[group].paths;
    ps.erase(ps.begin() + path);
    project_->mark_dirty();
    request_render();
    return true;
}

bool Engine::make_vector_path_editable(u64 layerId, u32 group, u32 path) noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    Composition* comp = project_ ? current_composition() : nullptr;
    Layer* l = vector_layer(comp, layerId);
    if (!l || group >= l->shape.vector.groups.size() || path >= l->shape.vector.groups[group].paths.size()) return false;
    VectorPath& p = l->shape.vector.groups[group].paths[path];
    if (p.kind == VectorPathKind::Free) return true;
    history_.before_mutation(*comp, project_->timeline().current(), "converter em caminho");
    bump_model_revision_locked();
    vector::make_editable(p);
    project_->mark_dirty();
    request_render();
    return true;
}

u32 Engine::query_vector_params(u64 layerId, u32 group, f32* out, u32 capacity) noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    Composition* comp = project_ ? current_composition() : nullptr;
    const Layer* l = vector_layer(comp, layerId);
    if (!l || !out || capacity < kVectorParamFloats || group >= l->shape.vector.groups.size()) return 0;
    const FrameIndex local = l->local_time(playback_.current());
    VectorGroup g = vector::evaluate_group(l->shape.vector.groups[group], l->tracks, group, static_cast<f64>(local.value));
    u32 anim = 0, keys = 0;
    for (u32 p = 0; p < kVecParamCount; ++p) {
        const f32* r = vector::param_ref(g, p);
        out[p] = r ? *r : 0.0f;
        const Track* tr = l->tracks.find(TrackProperty::VectorParam, group, p);
        if (!tr || tr->keys.empty()) continue;
        anim |= 1u << p;
        if (tr->find_exact(local) != kInvalidIndex) keys |= 1u << p;
    }
    out[kVecParamCount] = static_cast<f32>(anim);
    out[kVecParamCount + 1] = static_cast<f32>(keys);
    return kVectorParamFloats;
}

bool Engine::set_vector_param(u64 layerId, u32 group, u32 param, f32 value, bool continuing) noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    Composition* comp = project_ ? current_composition() : nullptr;
    Layer* l = vector_layer(comp, layerId);
    if (!l || group >= l->shape.vector.groups.size() || !valid_vector_param(param)) return false;
    VectorGroup& g = l->shape.vector.groups[group];
    f32* r = vector::param_ref(g, param);
    if (!r) return false;
    if (!continuing) history_.before_mutation(*comp, project_->timeline().current(), "valor vetorial");
    bump_model_revision_locked();
    value = clamp_group_param(g, param, value);
    Track* tr = l->tracks.find(TrackProperty::VectorParam, group, param);
    if (tr && !tr->keys.empty()) {
        const FrameIndex local = l->local_time(playback_.current());
        const u32 k = tr->find_exact(local);
        if (k != kInvalidIndex) tr->keys[k].value = value;
        else (void)tr->set(local, value, Interpolation::Bezier);
    } else {
        // Escala do grupo é um valor só (uniforme): Y acompanha.
        if (param == kVecScale) g.scale.y = g.scale.x != 0.0f ? g.scale.y / g.scale.x * value : value;
        *r = value;
    }
    project_->mark_dirty();
    request_render();
    return true;
}

bool Engine::ensure_vector_param_key(u64 layerId, u32 group, u32 param) noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    Composition* comp = project_ ? current_composition() : nullptr;
    Layer* l = vector_layer(comp, layerId);
    if (!l || group >= l->shape.vector.groups.size() || !valid_vector_param(param)) return false;
    f32* r = vector::param_ref(l->shape.vector.groups[group], param);
    if (!r) return false;
    history_.before_mutation(*comp, project_->timeline().current(), "keyframe vetorial");
    bump_model_revision_locked();
    const FrameIndex local = l->local_time(playback_.current());
    Track& tr = l->tracks.get_or_create(TrackProperty::VectorParam, group, param);
    const f32 atual = tr.keys.empty() ? *r : tr.sample(local);
    (void)tr.set(local, clamp_group_param(l->shape.vector.groups[group], param, atual), Interpolation::Bezier);
    project_->mark_dirty();
    request_render();
    return true;
}

bool Engine::toggle_vector_param_key(u64 layerId, u32 group, u32 param) noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    Composition* comp = project_ ? current_composition() : nullptr;
    Layer* l = vector_layer(comp, layerId);
    if (!l || group >= l->shape.vector.groups.size() || !valid_vector_param(param)) return false;
    f32* r = vector::param_ref(l->shape.vector.groups[group], param);
    if (!r) return false;
    history_.before_mutation(*comp, project_->timeline().current(), "keyframe vetorial");
    bump_model_revision_locked();
    const FrameIndex local = l->local_time(playback_.current());
    Track& tr = l->tracks.get_or_create(TrackProperty::VectorParam, group, param);
    const u32 k = tr.find_exact(local);
    if (k != kInvalidIndex) {
        if (tr.keys.size() == 1) *r = tr.keys[0].value;
        (void)tr.remove(local);
    } else {
        (void)tr.set(local, tr.keys.empty() ? *r : tr.sample(local), Interpolation::Bezier);
    }
    project_->mark_dirty();
    request_render();
    return true;
}

u32 Engine::query_vector_modifiers(u64 layerId, u32 group, f32* out, u32 capacity) noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    Composition* comp = project_ ? current_composition() : nullptr;
    const Layer* l = vector_layer(comp, layerId);
    if (!l || !out || group >= l->shape.vector.groups.size()) return 0;
    const FrameIndex local = l->local_time(playback_.current());
    const VectorGroup g = vector::evaluate_group(l->shape.vector.groups[group], l->tracks, group, static_cast<f64>(local.value), comp->fps());
    u32 written = 0;
    for (u32 m = 0; m < g.modifiers.size() && (written + 1) * kVectorModifierFloats <= capacity; ++m, ++written) {
        f32* v = out + written * kVectorModifierFloats;
        u32 anim = 0, keys = 0;
        for (u32 p = 0; p < kVecModParams; ++p) {
            v[4 + p] = g.modifiers[m].params[p];
            const Track* tr = l->tracks.find(TrackProperty::VectorParam, group, vector_modifier_param(m, p));
            if (!tr || tr->keys.empty()) continue;
            anim |= 1u << p;
            if (tr->find_exact(local) != kInvalidIndex) keys |= 1u << p;
        }
        v[0] = static_cast<f32>(g.modifiers[m].type);
        v[1] = g.modifiers[m].enabled ? 1.0f : 0.0f;
        v[2] = static_cast<f32>(anim);
        v[3] = static_cast<f32>(keys);
    }
    return written;
}

i32 Engine::add_vector_modifier(u64 layerId, u32 group, u32 type) noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    Composition* comp = project_ ? current_composition() : nullptr;
    Layer* l = vector_layer(comp, layerId);
    if (!l || group >= l->shape.vector.groups.size() || type >= static_cast<u32>(VectorModifierType::Count)) return -1;
    auto& mods = l->shape.vector.groups[group].modifiers;
    if (mods.size() >= kVecMaxModifiers) return -1;
    history_.before_mutation(*comp, project_->timeline().current(), "operador de caminho");
    bump_model_revision_locked();
    mods.push_back(vector::make_modifier(static_cast<VectorModifierType>(type)));
    project_->mark_dirty();
    request_render();
    return static_cast<i32>(mods.size() - 1);
}

bool Engine::remove_vector_modifier(u64 layerId, u32 group, u32 index) noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    Composition* comp = project_ ? current_composition() : nullptr;
    Layer* l = vector_layer(comp, layerId);
    if (!l || group >= l->shape.vector.groups.size() || index >= l->shape.vector.groups[group].modifiers.size()) return false;
    history_.before_mutation(*comp, project_->timeline().current(), "remover operador de caminho");
    bump_model_revision_locked();
    auto& mods = l->shape.vector.groups[group].modifiers;
    mods.erase(mods.begin() + index);
    remap_modifier_tracks(*l, group, [index](u32 m) { return m == index ? -1 : static_cast<i32>(m > index ? m - 1 : m); });
    project_->mark_dirty();
    request_render();
    return true;
}

bool Engine::move_vector_modifier(u64 layerId, u32 group, u32 from, u32 to) noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    Composition* comp = project_ ? current_composition() : nullptr;
    Layer* l = vector_layer(comp, layerId);
    if (!l || group >= l->shape.vector.groups.size()) return false;
    auto& mods = l->shape.vector.groups[group].modifiers;
    if (from >= mods.size() || to >= mods.size()) return false;
    if (from == to) return true;
    history_.before_mutation(*comp, project_->timeline().current(), "ordem dos operadores de caminho");
    bump_model_revision_locked();
    VectorModifier moved = mods[from];
    mods.erase(mods.begin() + from);
    mods.insert(mods.begin() + to, std::move(moved));
    remap_modifier_tracks(*l, group, [from, to](u32 m) {
        if (m == from) return static_cast<i32>(to);
        if (from < to && m > from && m <= to) return static_cast<i32>(m - 1);
        if (from > to && m >= to && m < from) return static_cast<i32>(m + 1);
        return static_cast<i32>(m);
    });
    project_->mark_dirty();
    request_render();
    return true;
}

bool Engine::set_vector_modifier_enabled(u64 layerId, u32 group, u32 index, bool enabled) noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    Composition* comp = project_ ? current_composition() : nullptr;
    Layer* l = vector_layer(comp, layerId);
    if (!l || group >= l->shape.vector.groups.size() || index >= l->shape.vector.groups[group].modifiers.size()) return false;
    VectorModifier& m = l->shape.vector.groups[group].modifiers[index];
    if (m.enabled == enabled) return true;
    history_.before_mutation(*comp, project_->timeline().current(), "operador de caminho");
    bump_model_revision_locked();
    m.enabled = enabled;
    project_->mark_dirty();
    request_render();
    return true;
}

namespace {
/// Camada de forma SDF (a vetorial edita pelo caminho, não por estes valores).
Layer* sdf_shape_layer(Composition* comp, u64 layerId) noexcept {
    Layer* l = comp ? comp->layer(LayerId::unpack(layerId)) : nullptr;
    return l && l->kind == LayerKind::Shape && l->shape.shapeType != kShapeVector ? l : nullptr;
}
/// Onde cada parâmetro mora na ShapeData (o índice é o de ShapeSetParam;
/// 0 = tipo da forma, não é animável). Tabela em timeline/ShapeGeometry.
f32* shape_param_ref(ShapeData& sh, u32 param) noexcept { return shape::param_field(sh, param); }
f32 clamp_shape_param(u32 param, f32 v) noexcept { return shape::clamp_param(param, v); }
/// Valor parado do parâmetro (o campo; nos 7..14 o "padrão da forma" resolvido).
f32 shape_param_static(const ShapeData& sh, u32 param, const f32* field) noexcept {
    return param >= shape::kParamDepth ? shape::param_value(sh, param) : *field;
}
static_assert(Engine::kShapeParamCount == shape::kParamTotal, "parâmetros da forma: Engine e ShapeGeometry juntos");
} // namespace

u32 Engine::query_shape_params(u64 layerId, f32* out, u32 capacity) noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    drain_commands_locked();
    Composition* comp = project_ ? current_composition() : nullptr;
    Layer* l = sdf_shape_layer(comp, layerId);
    if (!l || !out || capacity < kShapeParamFloats) return 0;
    const FrameIndex local = l->local_time(playback_.current());
    ShapeData sh = l->shape;
    u32 anim = 0, keys = 0;
    out[0] = static_cast<f32>(sh.shapeType);
    for (u32 p = 1; p < kShapeParamCount; ++p) {
        f32* r = shape_param_ref(sh, p);
        const Track* tr = l->tracks.find(TrackProperty::ShapeParam, 0, p);
        if (tr && !tr->keys.empty()) {
            anim |= 1u << p;
            if (tr->find_exact(local) != kInvalidIndex) keys |= 1u << p;
            if (r) *r = clamp_shape_param(p, tr->sample(local));
        }
        // 7..14: o valor efetivo (o "padrão da forma" já resolvido).
        out[p] = p >= shape::kParamDepth ? shape::param_value(sh, p) : (r ? *r : 0.0f);
    }
    out[kShapeParamCount] = static_cast<f32>(anim);
    out[kShapeParamCount + 1] = static_cast<f32>(keys);
    return kShapeParamFloats;
}

namespace {
/// A forma com os valores das trilhas no instante (o mesmo da consulta acima).
ShapeData shape_at(const Layer& l, FrameIndex local) noexcept {
    ShapeData sh = l.shape;
    for (u32 p = 1; p < Engine::kShapeParamCount; ++p) {
        const Track* tr = l.tracks.find(TrackProperty::ShapeParam, 0, p);
        if (!tr || tr->keys.empty()) continue;
        if (f32* r = shape_param_ref(sh, p)) *r = clamp_shape_param(p, tr->sample(local));
    }
    return sh;
}
} // namespace

u32 Engine::query_shape_param_handles(u64 layerId, f32* out, u32 capacity) noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    drain_commands_locked();
    Composition* comp = project_ ? current_composition() : nullptr;
    Layer* l = sdf_shape_layer(comp, layerId);
    if (!l || !out) return 0;
    shape::ParamHandle handles[shape::kMaxParamHandles];
    const u32 n = shape::param_handles(shape_at(*l, l->local_time(playback_.current())), handles);
    u32 written = 0;
    for (u32 i = 0; i < n && (written + 1) * kShapeHandleFloats <= capacity; ++i, ++written) {
        f32* v = out + written * kShapeHandleFloats;
        v[0] = handles[i].pos.x; v[1] = handles[i].pos.y;
        v[2] = static_cast<f32>(handles[i].param); v[3] = static_cast<f32>(handles[i].style);
        v[4] = handles[i].push.x; v[5] = handles[i].push.y;
    }
    return written;
}

bool Engine::drag_shape_param_handle(u64 layerId, u32 param, f32 x, f32 y, bool continuing) noexcept {
    f32 value = 0.0f;
    {
        std::lock_guard<std::mutex> lock(modelMutex_);
        drain_commands_locked();
        Composition* comp = project_ ? current_composition() : nullptr;
        Layer* l = sdf_shape_layer(comp, layerId);
        if (!l || l->locked) return false;
        if (!shape::value_from_handle(shape_at(*l, l->local_time(playback_.current())), param, Vec2{x, y}, value)) return false;
    }
    return set_shape_param(layerId, param, value, continuing);
}

bool Engine::set_shape_param(u64 layerId, u32 param, f32 value, bool continuing) noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    drain_commands_locked();
    Composition* comp = project_ ? current_composition() : nullptr;
    Layer* l = sdf_shape_layer(comp, layerId);
    if (!l || param == 0 || param >= kShapeParamCount) return false;
    f32* r = shape_param_ref(l->shape, param);
    if (!r) return false;
    if (!continuing) history_.before_mutation(*comp, project_->timeline().current(), "valor da forma");
    bump_model_revision_locked();
    value = clamp_shape_param(param, value);
    Track* tr = l->tracks.find(TrackProperty::ShapeParam, 0, param);
    if (tr && !tr->keys.empty()) {
        const FrameIndex local = l->local_time(playback_.current());
        const u32 k = tr->find_exact(local);
        if (k != kInvalidIndex) tr->keys[k].value = value;
        else (void)tr->set(local, value, Interpolation::Bezier);
    } else {
        *r = value;
        // Once tracked, the source stays centered even after the last key is
        // removed. Updating its authored anchor would move custom anchor keys.
        bool centered = false;
        for (u32 p = 1; p <= shape::kParamHeight; ++p)
            centered |= l->tracks.find(TrackProperty::ShapeParam, 0, p) != nullptr;
        if (!centered) {
            if (param == 5) l->transform.anchor.x = value * 0.5f;
            if (param == 6) l->transform.anchor.y = value * 0.5f;
        }
    }
    project_->mark_dirty();
    request_render();
    return true;
}

bool Engine::ensure_shape_param_key(u64 layerId, u32 param) noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    drain_commands_locked();
    Composition* comp = project_ ? current_composition() : nullptr;
    Layer* l = sdf_shape_layer(comp, layerId);
    if (!l || param == 0 || param >= kShapeParamCount) return false;
    f32* r = shape_param_ref(l->shape, param);
    if (!r) return false;
    history_.before_mutation(*comp, project_->timeline().current(), "keyframe da forma");
    bump_model_revision_locked();
    const FrameIndex local = l->local_time(playback_.current());
    Track& tr = l->tracks.get_or_create(TrackProperty::ShapeParam, 0, param);
    // COM keyframe aqui: regrava o valor AVALIADO (não apaga). É o que o
    // losango do painel faz — "criar keyframe" nunca pode devolver a forma ao
    // estado anterior só porque o quadro já tinha um.
    const f32 atual = tr.keys.empty() ? shape_param_static(l->shape, param, r) : tr.sample(local);
    (void)tr.set(local, clamp_shape_param(param, atual), Interpolation::Bezier);
    project_->mark_dirty();
    request_render();
    return true;
}

bool Engine::toggle_shape_param_key(u64 layerId, u32 param) noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    drain_commands_locked();
    Composition* comp = project_ ? current_composition() : nullptr;
    Layer* l = sdf_shape_layer(comp, layerId);
    if (!l || param == 0 || param >= kShapeParamCount) return false;
    f32* r = shape_param_ref(l->shape, param);
    if (!r) return false;
    history_.before_mutation(*comp, project_->timeline().current(), "keyframe da forma");
    bump_model_revision_locked();
    const FrameIndex local = l->local_time(playback_.current());
    Track& tr = l->tracks.get_or_create(TrackProperty::ShapeParam, 0, param);
    const u32 k = tr.find_exact(local);
    if (k != kInvalidIndex) {
        if (tr.keys.size() == 1) {
            *r = clamp_shape_param(param, tr.sample(local));
        }
        (void)tr.remove(local);
    } else {
        (void)tr.set(local, tr.keys.empty() ? shape_param_static(l->shape, param, r) : tr.sample(local), Interpolation::Bezier);
    }
    project_->mark_dirty();
    request_render();
    return true;
}

Result<u64> Engine::add_freehand_path(u64 layerId, const f32* xy, usize count, f32 error) noexcept {
    if (!xy || count < 4) return Status{Errc::InvalidArgument, "traço vazio"};
    std::lock_guard<std::mutex> lock(modelMutex_);
    Composition* comp = project_ ? current_composition() : nullptr;
    if (!comp) return Status{Errc::InvalidState, "nenhum projeto aberto"};
    Layer* l = layerId ? vector_layer(comp, layerId) : nullptr;
    if (layerId && !l) return Status{Errc::NotFound, "camada vetorial nao encontrada"};
    history_.before_mutation(*comp, project_->timeline().current(), "desenho à mão livre");
    bump_model_revision_locked();
    const f32 w = static_cast<f32>(comp->width()), h = static_cast<f32>(comp->height());
    u64 result = layerId;
    if (!l) {
        const LayerId lid = comp->add_layer(LayerKind::Shape, default_name(DefaultName::Freehand));
        result = lid.pack();
        l = comp->layer(lid);
        if (!l) return Status{Errc::OutOfMemory, "camada nao criada"};
        l->shape.shapeType = kShapeVector;
        l->shape.bounds = Rect{0.0f, 0.0f, w, h};
        const i64 t = std::clamp<i64>(playback_.current().value, 0, std::max<i64>(0, comp->duration().value - 1));
        l->start = FrameIndex{t};
        l->end = FrameIndex{new_layer_end_locked(comp, l, t)};
        l->transform.anchor = Vec3{w * 0.5f, h * 0.5f, 0.0f};
        l->transform.position = Vec3{w * 0.5f, h * 0.5f, 0.0f};
    }
    // Composição → camada (o traço fica onde o dedo passou, com a camada onde estiver).
    const vector::Affine2 inv = affine_of(layer_world_matrix(*comp, *l, playback_.current())).inverse();
    std::vector<Vec2> pts;
    pts.reserve(count / 2);
    for (usize i = 0; i + 1 < count; i += 2) pts.push_back(inv.apply(Vec2{xy[i], xy[i + 1]}));
    VectorGroup g = default_group(0, Vec2{}, 1.0f, static_cast<u32>(l->shape.vector.groups.size()));
    g.name = default_name(DefaultName::Stroke) + " " + std::to_string(l->shape.vector.groups.size() + 1);
    g.stroke.width = 8.0f;
    g.paths[0].path = vector::fit_curve(pts, std::clamp(error, 0.25f, 50.0f), true);
    l->shape.vector.groups.push_back(std::move(g));
    project_->mark_dirty();
    request_render();
    return result;
}

Result<u64> Engine::import_svg(const std::string& text, const char* name) noexcept {
    vector::SvgResult svg;
    std::string err;
    if (!vector::parse_svg(text, svg, &err)) return Status{Errc::UnsupportedFormat, err.empty() ? "SVG sem formas" : err.c_str()};
    std::lock_guard<std::mutex> lock(modelMutex_);
    if (!project_) return Status{Errc::InvalidState, "nenhum projeto aberto"};
    Composition* comp = current_composition();
    if (!comp) return Status{Errc::InvalidState, "projeto sem composicao"};
    history_.before_mutation(*comp, project_->timeline().current(), "importar SVG");
    bump_model_revision_locked();
    const LayerId lid = comp->add_layer(LayerKind::Shape, name && *name ? name : "SVG");
    Layer* l = comp->layer(lid);
    if (!l) return Status{Errc::OutOfMemory, "camada nao criada"};
    const f32 w = static_cast<f32>(comp->width()), h = static_cast<f32>(comp->height());
    const Vec2 sz{std::max(1.0f, svg.size.x), std::max(1.0f, svg.size.y)};
    l->shape.shapeType = kShapeVector;
    l->shape.bounds = Rect{0.0f, 0.0f, sz.x, sz.y};
    l->shape.vector = std::move(svg.data);
    const i64 t = std::clamp<i64>(playback_.current().value, 0, std::max<i64>(0, comp->duration().value - 1));
    l->start = FrameIndex{t};
    l->end = FrameIndex{new_layer_end_locked(comp, l, t)};
    // Centrado; maior que 80% da composição encolhe para caber (escala da camada).
    const f32 k = std::min({1.0f, 0.8f * w / sz.x, 0.8f * h / sz.y});
    l->transform.anchor = Vec3{sz.x * 0.5f, sz.y * 0.5f, 0.0f};
    l->transform.position = Vec3{w * 0.5f, h * 0.5f, 0.0f};
    l->transform.scale = Vec3{k, k, 1.0f};
    project_->mark_dirty();
    request_render();
    return lid.pack();
}

namespace {
/// Margem do texto no caminho: com keyframes na trilha, grava o keyframe do
/// instante (como o espaçamento); senão, o valor parado da camada.
void write_path_margin(Layer& l, TrackProperty property, f32& still, f32 desired, FrameIndex local) noexcept {
    desired = std::isfinite(desired) ? std::clamp(desired, -100000.0f, 100000.0f) : 0.0f;
    Track* tr = l.tracks.find(property);
    if (tr && !tr->keys.empty()) {
        if (std::fabs(tr->value_or(local, still) - desired) > 1e-4f) (void)tr->set(local, desired, Interpolation::Linear);
        return;
    }
    still = desired;
}

f32 read_path_margin(const Layer& l, TrackProperty property, f32 still, FrameIndex local) noexcept {
    const Track* tr = l.tracks.find(property);
    return tr && tr->driven() ? tr->value_or(local, still) : still;
}
} // namespace

bool Engine::set_text_path(u64 layerId, u64 pathLayer, f32 offset, bool perpendicular, bool reverse) noexcept {
    TextPathOptions o;
    if (!query_text_path_options(layerId, o)) return false;
    o.pathLayer = pathLayer;
    o.mask = kInvalidIndex;
    o.firstMargin = offset;
    o.perpendicular = perpendicular;
    o.reverse = reverse;
    return set_text_path_options(layerId, o);
}

bool Engine::query_text_path(u64 layerId, u64& pathLayer, f32& offset, bool& perpendicular, bool& reverse) noexcept {
    TextPathOptions o;
    if (!query_text_path_options(layerId, o)) return false;
    pathLayer = o.pathLayer;
    offset = o.firstMargin;
    perpendicular = o.perpendicular;
    reverse = o.reverse;
    return true;
}

bool Engine::set_text_path_options(u64 layerId, const TextPathOptions& o) noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    drain_commands_locked();   // o cabeçote dos comandos já enviados (o keyframe vai no quadro mostrado)
    Composition* comp = project_ ? current_composition() : nullptr;
    Layer* l = comp ? comp->layer(LayerId::unpack(layerId)) : nullptr;
    if (!l || l->kind != LayerKind::Text || l->locked) return false;
    if (o.mask != kInvalidIndex) {
        if (std::none_of(l->masks.begin(), l->masks.end(), [&](const Mask& m) { return m.id == o.mask; })) return false;
    } else if (o.pathLayer != 0 && (o.pathLayer == layerId || !vector_layer(comp, o.pathLayer))) {
        return false;
    }
    TextData& t = l->text;
    const u64 guide = o.mask != kInvalidIndex ? 0 : o.pathLayer;
    const FrameIndex local = l->local_time(playback_.current());
    const f32 first = read_path_margin(*l, TrackProperty::TextPathFirstMargin, t.pathOffset, local);
    const f32 last = read_path_margin(*l, TrackProperty::TextPathLastMargin, t.pathLastMargin, local);
    const bool sourceChanged = t.pathLayer != guide || t.pathMask != o.mask;
    if (!sourceChanged && t.pathPerpendicular == o.perpendicular && t.pathReverse == o.reverse && t.pathForceAlign == o.forceAlign
        && std::fabs(first - o.firstMargin) <= 1e-4f && std::fabs(last - o.lastMargin) <= 1e-4f) return true;
    history_.before_mutation(*comp, project_->timeline().current(), "texto no caminho");
    bump_model_revision_locked();
    // Trocar o caminho, a margem final ou forçar o alinhamento passa o texto
    // de um projeto antigo para o layout do After Effects.
    if (sourceChanged || t.pathForceAlign != o.forceAlign || std::fabs(last - o.lastMargin) > 1e-4f) t.pathLegacyLayout = false;
    t.pathLayer = guide;
    t.pathMask = o.mask;
    t.pathPerpendicular = o.perpendicular;
    t.pathReverse = o.reverse;
    t.pathForceAlign = o.forceAlign;
    write_path_margin(*l, TrackProperty::TextPathFirstMargin, t.pathOffset, o.firstMargin, local);
    write_path_margin(*l, TrackProperty::TextPathLastMargin, t.pathLastMargin, o.lastMargin, local);
    project_->mark_dirty();
    request_render();
    return true;
}

bool Engine::query_text_path_options(u64 layerId, TextPathOptions& o) noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    drain_commands_locked();
    Composition* comp = project_ ? current_composition() : nullptr;
    const Layer* l = comp ? comp->layer(LayerId::unpack(layerId)) : nullptr;
    if (!l || l->kind != LayerKind::Text) return false;
    const TextData& t = l->text;
    const FrameIndex local = l->local_time(playback_.current());
    const bool maskExists = t.pathMask != kInvalidIndex
        && std::any_of(l->masks.begin(), l->masks.end(), [&](const Mask& m) { return m.id == t.pathMask; });
    o.pathLayer = maskExists ? 0 : t.pathLayer;
    o.mask = maskExists ? t.pathMask : kInvalidIndex;
    o.firstMargin = read_path_margin(*l, TrackProperty::TextPathFirstMargin, t.pathOffset, local);
    o.lastMargin = read_path_margin(*l, TrackProperty::TextPathLastMargin, t.pathLastMargin, local);
    o.perpendicular = t.pathPerpendicular;
    o.reverse = t.pathReverse;
    o.forceAlign = t.pathForceAlign;
    return true;
}

} // namespace aurea
