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

namespace mesh_warp {
const MeshWarpData* animation(const EffectInstance& effect) noexcept {
    if (effect.type != effect_type_id(kMeshWarpKey) || effect.meshes.empty()) return nullptr;
    u32 rows, cols;
    grid_of(effect, rows, cols);
    return matches(effect.meshes.front(), rows, cols) ? &effect.meshes.front() : nullptr;
}
MeshWarpData* animation(EffectInstance& effect) noexcept {
    return const_cast<MeshWarpData*>(animation(static_cast<const EffectInstance&>(effect)));
}
MeshWarpData* ensure_animation(EffectInstance& effect) noexcept {
    if (effect.type != effect_type_id(kMeshWarpKey)) return nullptr;
    u32 rows, cols;
    grid_of(effect, rows, cols);
    if (effect.meshes.empty()) effect.meshes.emplace_back();
    MeshWarpData& data = effect.meshes.front();
    if (!matches(data, rows, cols)) {
        data = MeshWarpData{};
        data.rows = rows;
        data.cols = cols;
        identity(rows, cols, data.values);
    }
    return &data;
}
Interpolation track_interpolation(u8 legacy) noexcept {
    return legacy == 0 ? Interpolation::Hold : legacy == 2 ? Interpolation::EaseInOut : Interpolation::Linear;
}
Status key_command(Layer& layer, const Command& command, FrameIndex playhead) noexcept {
    if (layer.locked) return Errc::InvalidState;
    const bool moving = command.type == CommandType::KeyframeMove;
    const bool interpolation = command.type == CommandType::KeyframeSetInterpolation
        || command.type == CommandType::KeyframeSetBezier || command.type == CommandType::KeyframeSetEasing;
    const TrackRef& ref = moving ? command.keyframe_move.track
        : interpolation ? command.keyframe_interp.track : command.keyframe.track;
    if (ref.property != TrackProperty::MeshWarp || ref.effectParamIndex != 0) return Errc::InvalidArgument;
    EffectInstance* effect = mesh_effect(&layer, ref.effectIndex);
    if (!effect) return Errc::NotFound;
    const FrameIndex time = moving ? command.keyframe_move.fromTime
        : interpolation ? command.keyframe_interp.time
        : (command.keyframe.onlyIfChanged & kAutoKeyAtPlayhead) ? layer.local_time(playhead) : command.keyframe.time;
    MeshWarpData* data = animation(*effect);
    if (command.type == CommandType::KeyframeInsert) {
        if (data && key_at(*data, time.value) >= 0) return OkStatus;
        if (data && data->keys.size() >= kMeshWarpMaxKeys) return Errc::OutOfRange;
        u32 rows, cols;
        grid_of(*effect, rows, cols);
        std::vector<f32> values;
        (void)evaluate(data, rows, cols, static_cast<f64>(time.value), values);
        data = ensure_animation(*effect);
        MeshWarpKey key{time.value, 1, std::move(values)};
        auto where = std::lower_bound(data->keys.begin(), data->keys.end(), time.value,
            [](const MeshWarpKey& key, i64 frame) { return key.frame < frame; });
        data->keys.insert(where, std::move(key));
        return OkStatus;
    }
    if (!data) return Errc::NotFound;
    const i32 at = key_at(*data, time.value);
    if (at < 0) return Errc::NotFound;
    switch (command.type) {
        case CommandType::KeyframeDelete:
            if (data->keys.size() == 1) {
                std::vector<f32> frozen;
                (void)evaluate(data, data->rows, data->cols,
                    static_cast<f64>(layer.local_time(playhead).value), frozen);
                data->values = std::move(frozen);
            }
            data->keys.erase(data->keys.begin() + at);
            return OkStatus;
        case CommandType::KeyframeMove: {
            const i64 target = command.keyframe_move.toTime.value;
            if (target == time.value) return OkStatus;
            if (key_at(*data, target) >= 0) return Errc::AlreadyExists;
            data->keys[static_cast<usize>(at)].frame = target;
            std::sort(data->keys.begin(), data->keys.end(), [](const auto& a, const auto& b) { return a.frame < b.frame; });
            return OkStatus;
        }
        case CommandType::KeyframeSetInterpolation: {
            const Interpolation interp = command.keyframe_interp.interp;
            if (interp != Interpolation::Hold && interp != Interpolation::Linear && interp != Interpolation::EaseInOut)
                return Errc::NotSupported;
            data->keys[static_cast<usize>(at)].interp = interp == Interpolation::Hold ? 0 : interp == Interpolation::EaseInOut ? 2 : 1;
            return OkStatus;
        }
        default: return Errc::NotSupported; // A whole mesh has no scalar value, Bézier graph, or expression.
    }
}
} // namespace mesh_warp

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
    drain_commands_locked();
    Composition* comp = project_ ? current_composition() : nullptr;
    Layer* l = comp ? comp->layer(LayerId::unpack(layerId)) : nullptr;
    EffectInstance* e = mesh_effect(l, effectId);
    if (!e || l->locked || handle > 4) return false;
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
    bump_model_revision_locked();
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
    drain_commands_locked();
    Composition* comp = project_ ? current_composition() : nullptr;
    Layer* l = comp ? comp->layer(LayerId::unpack(layerId)) : nullptr;
    EffectInstance* e = mesh_effect(l, effectId);
    if (!e || l->locked) return false;
    if (e->meshes.empty()) return true;
    history_.before_mutation(*comp, project_->timeline().current(), "redefinir malha");
    bump_model_revision_locked();
    e->meshes.clear();
    project_->mark_dirty();
    request_render();
    return true;
}

bool Engine::mesh_warp_toggle_key(u64 layerId, u32 effectId) noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    drain_commands_locked();
    Composition* comp = project_ ? current_composition() : nullptr;
    Layer* layer = comp ? comp->layer(LayerId::unpack(layerId)) : nullptr;
    EffectInstance* effect = mesh_effect(layer, effectId);
    if (!effect || layer->locked) return false;
    const FrameIndex local = layer->local_time(playback_.current());
    const MeshWarpData* data = mesh_warp::animation(*effect);
    const bool remove = data && mesh_warp::key_at(*data, local.value) >= 0;
    if (!remove && data && data->keys.size() >= kMeshWarpMaxKeys) return false;
    history_.before_mutation(*comp, project_->timeline().current(), "keyframe da malha");
    Command command;
    command.type = remove ? CommandType::KeyframeDelete : CommandType::KeyframeInsert;
    command.keyframe.track = TrackRef{LayerId::unpack(layerId), TrackProperty::MeshWarp, effectId, 0};
    command.keyframe.time = local;
    if (!mesh_warp::key_command(*layer, command, playback_.current()).ok()) return false;
    bump_model_revision_locked();
    project_->mark_dirty();
    request_render();
    return true;
}

} // namespace aurea
