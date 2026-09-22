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

#include "aurea/vector/Vector.hpp"

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
        g.name = "Caminho " + std::to_string(index + 1);
    } else {
        static const char* kNames[] = {"Caminho", "Retângulo", "Elipse", "Polígono", "Estrela"};
        g.name = std::string(kNames[static_cast<u32>(p.kind)]) + " " + std::to_string(index + 1);
    }
    return g;
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
    modelRevision_.fetch_add(1, std::memory_order_acq_rel);
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
    l->end = FrameIndex{std::max<i64>(t + 1, comp->duration().value)};
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
    if (d == l->shape.vector) return true;
    if (!continuing) history_.before_mutation(*comp, project_->timeline().current(), "camada vetorial");
    modelRevision_.fetch_add(1, std::memory_order_acq_rel);
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
    const VectorGroup g = vector::evaluate_group(l->shape.vector.groups[group], l->tracks, group, local);
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
    modelRevision_.fetch_add(1, std::memory_order_acq_rel);
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

bool Engine::toggle_vector_path_key(u64 layerId, u32 group, u32 path) noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    Composition* comp = project_ ? current_composition() : nullptr;
    Layer* l = vector_layer(comp, layerId);
    if (!l || group >= l->shape.vector.groups.size() || path >= l->shape.vector.groups[group].paths.size()) return false;
    history_.before_mutation(*comp, project_->timeline().current(), "keyframe de forma");
    modelRevision_.fetch_add(1, std::memory_order_acq_rel);
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

i32 Engine::add_vector_group(u64 layerId, u32 pathKind) noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    Composition* comp = project_ ? current_composition() : nullptr;
    Layer* l = vector_layer(comp, layerId);
    if (!l || l->shape.vector.groups.size() >= 4096) return -1;
    history_.before_mutation(*comp, project_->timeline().current(), "novo grupo vetorial");
    modelRevision_.fetch_add(1, std::memory_order_acq_rel);
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
    modelRevision_.fetch_add(1, std::memory_order_acq_rel);
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
    modelRevision_.fetch_add(1, std::memory_order_acq_rel);
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
    modelRevision_.fetch_add(1, std::memory_order_acq_rel);
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
    modelRevision_.fetch_add(1, std::memory_order_acq_rel);
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
    if (!l || group >= l->shape.vector.groups.size() || param >= kVecParamCount) return false;
    VectorGroup& g = l->shape.vector.groups[group];
    f32* r = vector::param_ref(g, param);
    if (!r) return false;
    if (!continuing) history_.before_mutation(*comp, project_->timeline().current(), "valor vetorial");
    modelRevision_.fetch_add(1, std::memory_order_acq_rel);
    value = vector::clamp_param(param, value);
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

bool Engine::toggle_vector_param_key(u64 layerId, u32 group, u32 param) noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    Composition* comp = project_ ? current_composition() : nullptr;
    Layer* l = vector_layer(comp, layerId);
    if (!l || group >= l->shape.vector.groups.size() || param >= kVecParamCount) return false;
    f32* r = vector::param_ref(l->shape.vector.groups[group], param);
    if (!r) return false;
    history_.before_mutation(*comp, project_->timeline().current(), "keyframe vetorial");
    modelRevision_.fetch_add(1, std::memory_order_acq_rel);
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

namespace {
/// Camada de forma SDF (a vetorial edita pelo caminho, não por estes valores).
Layer* sdf_shape_layer(Composition* comp, u64 layerId) noexcept {
    Layer* l = comp ? comp->layer(LayerId::unpack(layerId)) : nullptr;
    return l && l->kind == LayerKind::Shape && l->shape.shapeType != kShapeVector ? l : nullptr;
}
/// Onde cada parâmetro mora na ShapeData (o índice é o de ShapeSetParam).
f32* shape_param_ref(ShapeData& sh, u32 param) noexcept {
    switch (param) {
        case 1: return &sh.cornerRadius;
        case 2: return &sh.points;
        case 3: return &sh.innerRadius;
        case 4: return &sh.strokeWidth;
        case 5: return &sh.bounds.w;
        case 6: return &sh.bounds.h;
        default: return nullptr;   // 0 = tipo da forma, não é animável
    }
}
f32 clamp_shape_param(u32 param, f32 v) noexcept {
    switch (param) {
        case 1: return std::max(0.0f, v);
        case 2: return std::clamp(std::round(v), 3.0f, 64.0f);
        case 3: return std::clamp(v, 0.05f, 0.95f);
        case 4: return std::clamp(v, 0.0f, 500.0f);
        default: return std::clamp(v, 1.0f, 16384.0f);
    }
}
} // namespace

u32 Engine::query_shape_params(u64 layerId, f32* out, u32 capacity) noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
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
        out[p] = r ? *r : 0.0f;
    }
    out[kShapeParamCount] = static_cast<f32>(anim);
    out[kShapeParamCount + 1] = static_cast<f32>(keys);
    return kShapeParamFloats;
}

bool Engine::set_shape_param(u64 layerId, u32 param, f32 value, bool continuing) noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    Composition* comp = project_ ? current_composition() : nullptr;
    Layer* l = sdf_shape_layer(comp, layerId);
    if (!l || param == 0 || param >= kShapeParamCount) return false;
    f32* r = shape_param_ref(l->shape, param);
    if (!r) return false;
    if (!continuing) history_.before_mutation(*comp, project_->timeline().current(), "valor da forma");
    modelRevision_.fetch_add(1, std::memory_order_acq_rel);
    value = clamp_shape_param(param, value);
    Track* tr = l->tracks.find(TrackProperty::ShapeParam, 0, param);
    if (tr && !tr->keys.empty()) {
        const FrameIndex local = l->local_time(playback_.current());
        const u32 k = tr->find_exact(local);
        if (k != kInvalidIndex) tr->keys[k].value = value;
        else (void)tr->set(local, value, Interpolation::Bezier);
    } else {
        *r = value;
        // Tamanho parado muda em volta do centro (a âncora acompanha a metade).
        if (param == 5) l->transform.anchor.x = value * 0.5f;
        if (param == 6) l->transform.anchor.y = value * 0.5f;
    }
    project_->mark_dirty();
    request_render();
    return true;
}

bool Engine::toggle_shape_param_key(u64 layerId, u32 param) noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    Composition* comp = project_ ? current_composition() : nullptr;
    Layer* l = sdf_shape_layer(comp, layerId);
    if (!l || param == 0 || param >= kShapeParamCount) return false;
    f32* r = shape_param_ref(l->shape, param);
    if (!r) return false;
    history_.before_mutation(*comp, project_->timeline().current(), "keyframe da forma");
    modelRevision_.fetch_add(1, std::memory_order_acq_rel);
    const FrameIndex local = l->local_time(playback_.current());
    Track& tr = l->tracks.get_or_create(TrackProperty::ShapeParam, 0, param);
    const u32 k = tr.find_exact(local);
    if (k != kInvalidIndex) {
        if (tr.keys.size() == 1) *r = clamp_shape_param(param, tr.keys[0].value);
        (void)tr.remove(local);
    } else {
        (void)tr.set(local, tr.keys.empty() ? *r : tr.sample(local), Interpolation::Bezier);
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
    modelRevision_.fetch_add(1, std::memory_order_acq_rel);
    const f32 w = static_cast<f32>(comp->width()), h = static_cast<f32>(comp->height());
    u64 result = layerId;
    if (!l) {
        const LayerId lid = comp->add_layer(LayerKind::Shape, "Desenho à mão livre");
        result = lid.pack();
        l = comp->layer(lid);
        if (!l) return Status{Errc::OutOfMemory, "camada nao criada"};
        l->shape.shapeType = kShapeVector;
        l->shape.bounds = Rect{0.0f, 0.0f, w, h};
        const i64 t = std::clamp<i64>(playback_.current().value, 0, std::max<i64>(0, comp->duration().value - 1));
        l->start = FrameIndex{t};
        l->end = FrameIndex{std::max<i64>(t + 1, comp->duration().value)};
        l->transform.anchor = Vec3{w * 0.5f, h * 0.5f, 0.0f};
        l->transform.position = Vec3{w * 0.5f, h * 0.5f, 0.0f};
    }
    // Composição → camada (o traço fica onde o dedo passou, com a camada onde estiver).
    const vector::Affine2 inv = affine_of(layer_world_matrix(*comp, *l, playback_.current())).inverse();
    std::vector<Vec2> pts;
    pts.reserve(count / 2);
    for (usize i = 0; i + 1 < count; i += 2) pts.push_back(inv.apply(Vec2{xy[i], xy[i + 1]}));
    VectorGroup g = default_group(0, Vec2{}, 1.0f, static_cast<u32>(l->shape.vector.groups.size()));
    g.name = "Traço " + std::to_string(l->shape.vector.groups.size() + 1);
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
    modelRevision_.fetch_add(1, std::memory_order_acq_rel);
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
    l->end = FrameIndex{std::max<i64>(t + 1, comp->duration().value)};
    // Centrado; maior que 80% da composição encolhe para caber (escala da camada).
    const f32 k = std::min({1.0f, 0.8f * w / sz.x, 0.8f * h / sz.y});
    l->transform.anchor = Vec3{sz.x * 0.5f, sz.y * 0.5f, 0.0f};
    l->transform.position = Vec3{w * 0.5f, h * 0.5f, 0.0f};
    l->transform.scale = Vec3{k, k, 1.0f};
    project_->mark_dirty();
    request_render();
    return lid.pack();
}

bool Engine::set_text_path(u64 layerId, u64 pathLayer, f32 offset, bool perpendicular, bool reverse) noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    Composition* comp = project_ ? current_composition() : nullptr;
    Layer* l = comp ? comp->layer(LayerId::unpack(layerId)) : nullptr;
    if (!l || l->kind != LayerKind::Text) return false;
    if (pathLayer != 0 && (pathLayer == layerId || !vector_layer(comp, pathLayer))) return false;
    history_.before_mutation(*comp, project_->timeline().current(), "texto no caminho");
    modelRevision_.fetch_add(1, std::memory_order_acq_rel);
    l->text.pathLayer = pathLayer;
    l->text.pathOffset = std::clamp(offset, -100000.0f, 100000.0f);
    l->text.pathPerpendicular = perpendicular;
    l->text.pathReverse = reverse;
    project_->mark_dirty();
    request_render();
    return true;
}

bool Engine::query_text_path(u64 layerId, u64& pathLayer, f32& offset, bool& perpendicular, bool& reverse) noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    Composition* comp = project_ ? current_composition() : nullptr;
    const Layer* l = comp ? comp->layer(LayerId::unpack(layerId)) : nullptr;
    if (!l || l->kind != LayerKind::Text) return false;
    pathLayer = l->text.pathLayer;
    offset = l->text.pathOffset;
    perpendicular = l->text.pathPerpendicular;
    reverse = l->text.pathReverse;
    return true;
}

} // namespace aurea
