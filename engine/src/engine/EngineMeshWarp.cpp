// =============================================================================
//  Aurea / engine / EngineMeshWarp.cpp
//
//  API da MALHA DE DEFORMAÇÃO no palco (aurea/effects/MeshWarp.hpp): ler a
//  malha no cabeçote, arrastar vértice/alça (key no cabeçote quando animada ou
//  com auto-key) e redefinir. Coordenadas normalizadas à caixa da camada; a UI
//  converte pelos cantos. Padrão de mutação do motor (como EngineRig.cpp):
//  modelMutex_, history_.before_mutation, modelRevision_, mark_dirty,
//  request_render. `continuing` (arrasto) pula o passo novo.
// =============================================================================
#include "aurea/Engine.hpp"

#include "aurea/effects/MeshWarp.hpp"

#include <algorithm>
#include <cmath>

namespace aurea {
namespace {

EffectInstance* mesh_effect(Layer* l, u32 effectId) noexcept {
    if (!l) return nullptr;
    for (EffectInstance& e : l->effects)
        if (e.id == effectId && e.type == effect_type_id(kMeshWarpKey)) return &e;
    return nullptr;
}

void grid_of(const EffectInstance& e, u32& rows, u32& cols) noexcept {
    auto read = [&](u32 i, i32 def) {
        const i32 v = i < e.params.size() ? e.params[i].constant.as_int() : def;
        return static_cast<u32>(std::clamp(v, 1, static_cast<i32>(kMeshWarpMaxDivisions)));
    };
    rows = read(0, 7);
    cols = read(1, 7);
}

} // namespace

u32 Engine::query_mesh_warp(u64 layerId, u32 effectId, f32* out, u32 capacity) noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    Composition* comp = project_ ? current_composition() : nullptr;
    Layer* l = comp ? comp->layer(LayerId::unpack(layerId)) : nullptr;
    const EffectInstance* e = mesh_effect(l, effectId);
    if (!e) return 0;
    u32 rows, cols;
    grid_of(*e, rows, cols);
    const u32 need = kMeshWarpHeaderFloats + mesh_warp::value_count(rows, cols);
    if (!out || capacity < need) return need;
    const FrameIndex local = l->local_time(playback_.current());
    const MeshWarpData* d = e->meshes.empty() ? nullptr : &e->meshes.front();
    std::vector<f32> v;
    (void)mesh_warp::evaluate(d, rows, cols, static_cast<f64>(local.value), v);
    const bool valid = d && mesh_warp::matches(*d, rows, cols);
    out[0] = static_cast<f32>(rows);
    out[1] = static_cast<f32>(cols);
    out[2] = valid && mesh_warp::key_at(*d, local.value) >= 0 ? 1.0f : 0.0f;
    out[3] = valid && !d->keys.empty() ? 1.0f : 0.0f;
    std::copy(v.begin(), v.end(), out + kMeshWarpHeaderFloats);
    return need;
}

bool Engine::mesh_warp_drag(u64 layerId, u32 effectId, u32 vertex, u32 handle, f32 u, f32 v, bool autoKey,
                            bool continuing) noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    Composition* comp = project_ ? current_composition() : nullptr;
    Layer* l = comp ? comp->layer(LayerId::unpack(layerId)) : nullptr;
    EffectInstance* e = mesh_effect(l, effectId);
    if (!e || handle > 4) return false;
    u32 rows, cols;
    grid_of(*e, rows, cols);
    if (vertex >= mesh_warp::vertex_count(rows, cols) || !std::isfinite(u) || !std::isfinite(v)) return false;
    const FrameIndex local = l->local_time(playback_.current());
    // A malha que a pessoa VÊ agora é a base do arrasto.
    const MeshWarpData* old = e->meshes.empty() ? nullptr : &e->meshes.front();
    std::vector<f32> values;
    (void)mesh_warp::evaluate(old, rows, cols, static_cast<f64>(local.value), values);
    if (!mesh_warp::move_handle(values, rows, cols, vertex, handle, Vec2{u, v})) return false;
    if (!continuing) history_.before_mutation(*comp, project_->timeline().current(), "malha de deformacao");
    modelRevision_.fetch_add(1, std::memory_order_acq_rel);
    if (e->meshes.empty()) e->meshes.emplace_back();
    MeshWarpData& d = e->meshes.front();
    if (!mesh_warp::matches(d, rows, cols)) {
        // Linhas/Colunas mudaram: a malha antiga não vale mais (redefinida).
        d = MeshWarpData{};
        d.rows = rows;
        d.cols = cols;
    }
    if (!d.keys.empty() || autoKey) {
        const i32 at = mesh_warp::key_at(d, local.value);
        if (at >= 0) {
            d.keys[static_cast<u32>(at)].values = std::move(values);
        } else {
            if (d.keys.size() >= kMeshWarpMaxKeys) return false;
            // Primeiro key de uma malha parada: a forma parada vira a base.
            MeshWarpKey k;
            k.frame = local.value;
            k.interp = 1;
            k.values = std::move(values);
            auto pos = std::lower_bound(d.keys.begin(), d.keys.end(), k.frame,
                                        [](const MeshWarpKey& a, i64 f) { return a.frame < f; });
            d.keys.insert(pos, std::move(k));
        }
    } else {
        d.values = std::move(values);
    }
    project_->mark_dirty();
    request_render();
    return true;
}

bool Engine::mesh_warp_reset(u64 layerId, u32 effectId) noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    Composition* comp = project_ ? current_composition() : nullptr;
    Layer* l = comp ? comp->layer(LayerId::unpack(layerId)) : nullptr;
    EffectInstance* e = mesh_effect(l, effectId);
    if (!e) return false;
    if (e->meshes.empty()) return true;
    history_.before_mutation(*comp, project_->timeline().current(), "redefinir malha");
    modelRevision_.fetch_add(1, std::memory_order_acq_rel);
    e->meshes.clear();
    project_->mark_dirty();
    request_render();
    return true;
}

} // namespace aurea
