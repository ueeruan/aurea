// =============================================================================
//  Aurea / engine / EngineAeAnimation.cpp
//
//  AE animation (2026-11-11, gap_system itens 5, 6 e 7). O After Effects é só
//  a especificação de comportamento; Android e iOS só chamam, a regra mora aqui.
//
//  - Texto-fonte (Source Text): keys congelados de conteúdo + trechos + estilo,
//    na trilha virtual TrackProperty::TextSource (dados em
//    TextData::sourceKeys; a amostragem é text::apply_source_text, usada pelo
//    render, pelo export e pelas consultas da UI).
//  - Auto-Orientar (Layer::autoOrient) e câmera de dois nós (CameraData::twoNode
//    + poi): a conta mora em timeline/LayerWorld.cpp; aqui só as edições.
// =============================================================================
#include "EngineAeAnimation.hpp"

#include "aurea/Engine.hpp"
#include "aurea/text/Text.hpp"
#include "aurea/timeline/Composition.hpp"
#include "aurea/timeline/Layer.hpp"

#include <algorithm>
#include <cmath>

namespace aurea {

namespace ae_anim {
namespace {
i32 key_at(const TextData& t, i64 frame) noexcept {
    for (usize i = 0; i < t.sourceKeys.size(); ++i) if (t.sourceKeys[i].frame == frame) return static_cast<i32>(i);
    return -1;
}

/// O key no quadro `local` com o que aparece ali (o key congelado ou a base).
TextKey key_from_shown(const TextData& t, i64 local) {
    TextData shown;
    const TextData* src = &t;
    if (text::apply_source_text(t, local, shown)) src = &shown;
    TextKey key;
    key.frame = local;
    text::capture_source_key(*src, key);
    return key;
}

/// Trechos depois do fim do conteúdo saem; os que cruzam o fim são cortados.
void clip_spans(std::vector<TextSpan>& spans, usize length) {
    const u32 n = static_cast<u32>(std::min<usize>(length, 0xFFFFFFFFu));
    std::vector<TextSpan> kept;
    for (TextSpan sp : spans) {
        if (sp.start >= n) continue;
        sp.end = std::min(sp.end, n);
        if (sp.end > sp.start) kept.push_back(sp);
    }
    spans.swap(kept);
}
} // namespace

Status text_source_key_command(Layer& layer, const Command& command, FrameIndex playhead) noexcept {
    if (layer.locked) return Errc::InvalidState;
    if (layer.kind != LayerKind::Text) return Errc::NotFound;
    TextData& t = layer.text;
    const bool moving = command.type == CommandType::KeyframeMove;
    const bool interpolation = command.type == CommandType::KeyframeSetInterpolation
                            || command.type == CommandType::KeyframeSetBezier
                            || command.type == CommandType::KeyframeSetEasing;
    const FrameIndex time = moving ? command.keyframe_move.fromTime
        : interpolation ? command.keyframe_interp.time
        : (command.keyframe.onlyIfChanged & kAutoKeyAtPlayhead) ? layer.local_time(playhead) : command.keyframe.time;
    if (command.type == CommandType::KeyframeInsert) {
        if (key_at(t, time.value) >= 0) return OkStatus;
        (void)text::upsert_source_key(t, key_from_shown(t, time.value));
        return OkStatus;
    }
    const i32 at = key_at(t, time.value);
    if (at < 0) return Errc::NotFound;
    switch (command.type) {
        case CommandType::KeyframeDelete:
            // Tirar o último key deixa parado o texto que aparecia no cabeçote.
            if (t.sourceKeys.size() == 1) (void)text::apply_source_text(t, layer.local_time(playhead).value, t);
            t.sourceKeys.erase(t.sourceKeys.begin() + at);
            return OkStatus;
        case CommandType::KeyframeMove: {
            const i64 target = command.keyframe_move.toTime.value;
            if (target == time.value) return OkStatus;
            if (key_at(t, target) >= 0) return Errc::AlreadyExists;
            TextKey key = t.sourceKeys[static_cast<usize>(at)];
            t.sourceKeys.erase(t.sourceKeys.begin() + at);
            key.frame = target;
            (void)text::upsert_source_key(t, std::move(key));
            return OkStatus;
        }
        case CommandType::KeyframeSetInterpolation:
        case CommandType::KeyframeSetBezier:
        case CommandType::KeyframeSetEasing:
            return OkStatus;   // o texto-fonte é sempre congelado (como no AE)
        default:
            return Errc::NotSupported;   // texto não tem valor escalar nem expressão
    }
}

void begin_text_edit(Layer& layer, FrameIndex playhead) noexcept {
    if (layer.kind != LayerKind::Text || layer.text.sourceKeys.empty()) return;
    (void)text::apply_source_text(layer.text, layer.local_time(playhead).value, layer.text);
}

void end_text_edit(Layer& layer, FrameIndex playhead) noexcept {
    if (layer.kind != LayerKind::Text || layer.text.sourceKeys.empty()) return;
    TextKey key;
    key.frame = layer.local_time(playhead).value;
    text::capture_source_key(layer.text, key);
    (void)text::upsert_source_key(layer.text, std::move(key));
}

} // namespace ae_anim

// =============================================================================
// Texto-fonte
// =============================================================================
i64 Engine::add_text_source_key(u64 layerId) noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    drain_commands_locked();
    Composition* comp = project_ ? current_composition() : nullptr;
    Layer* l = comp ? comp->layer(LayerId::unpack(layerId)) : nullptr;
    if (!l || l->locked || l->kind != LayerKind::Text || !l->captions.empty()) return -1;
    const i64 local = l->local_time(playback_.current()).value;
    history_.before_mutation(*comp, project_->timeline().current(), "key do texto-fonte");
    bump_model_revision_locked();
    (void)text::upsert_source_key(l->text, ae_anim::key_from_shown(l->text, local));
    project_->mark_dirty();
    request_render();
    return local;
}

bool Engine::set_text_source_key(u64 layerId, i64 frame, const std::string& content) noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    drain_commands_locked();
    Composition* comp = project_ ? current_composition() : nullptr;
    Layer* l = comp ? comp->layer(LayerId::unpack(layerId)) : nullptr;
    if (!l || l->locked || l->kind != LayerKind::Text) return false;
    const i32 at = ae_anim::key_at(l->text, frame);
    if (at < 0 || content.size() > 64u * 1024u) return false;
    TextKey& key = l->text.sourceKeys[static_cast<usize>(at)];
    if (key.content == content) return true;
    history_.before_mutation(*comp, project_->timeline().current(), "texto do key");
    bump_model_revision_locked();
    TextKey& k = l->text.sourceKeys[static_cast<usize>(at)];   // o histórico copia a composição, não a mexe
    k.content = content;
    ae_anim::clip_spans(k.spans, content.size());
    project_->mark_dirty();
    request_render();
    return true;
}

bool Engine::delete_text_source_key(u64 layerId, i64 frame) noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    drain_commands_locked();
    Composition* comp = project_ ? current_composition() : nullptr;
    Layer* l = comp ? comp->layer(LayerId::unpack(layerId)) : nullptr;
    if (!l || l->locked || l->kind != LayerKind::Text || ae_anim::key_at(l->text, frame) < 0) return false;
    history_.before_mutation(*comp, project_->timeline().current(), "apagar key do texto-fonte");
    bump_model_revision_locked();
    Command c;
    c.type = CommandType::KeyframeDelete;
    c.keyframe.track = TrackRef{LayerId::unpack(layerId), TrackProperty::TextSource, 0, 0};
    c.keyframe.time = FrameIndex{frame};
    c.keyframe.value = 0.0f;
    c.keyframe.onlyIfChanged = 0;
    const bool ok = ae_anim::text_source_key_command(*l, c, playback_.current()).ok();
    project_->mark_dirty();
    request_render();
    return ok;
}

u32 Engine::query_text_source_keys(u64 layerId, i64* frames, u32 capacity) noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    Composition* comp = project_ ? current_composition() : nullptr;
    const Layer* l = comp ? comp->layer(LayerId::unpack(layerId)) : nullptr;
    if (!l || l->kind != LayerKind::Text) return 0;
    const auto& keys = l->text.sourceKeys;
    for (u32 i = 0; frames && i < capacity && i < keys.size(); ++i) frames[i] = keys[i].frame;
    return static_cast<u32>(keys.size());
}

bool Engine::text_source_key_content(u64 layerId, i64 frame, std::string& out) noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    Composition* comp = project_ ? current_composition() : nullptr;
    const Layer* l = comp ? comp->layer(LayerId::unpack(layerId)) : nullptr;
    if (!l || l->kind != LayerKind::Text) return false;
    const i32 at = ae_anim::key_at(l->text, frame);
    if (at < 0) return false;
    out = l->text.sourceKeys[static_cast<usize>(at)].content;
    return true;
}

// =============================================================================
// Auto-Orientar e câmera de dois nós
// =============================================================================
bool Engine::set_layer_auto_orient(u64 layerId, u32 mode) noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    drain_commands_locked();
    Composition* comp = project_ ? current_composition() : nullptr;
    Layer* l = comp ? comp->layer(LayerId::unpack(layerId)) : nullptr;
    if (!l || l->locked || mode > 2) return false;
    if (mode == 2 && (l->kind == LayerKind::Camera || l->kind == LayerKind::Light)) return false;
    if (l->autoOrient == mode) return true;
    history_.before_mutation(*comp, project_->timeline().current(), "auto-orientar");
    bump_model_revision_locked();
    l->autoOrient = static_cast<u8>(mode);
    project_->mark_dirty();
    request_render();
    return true;
}

i32 Engine::query_layer_auto_orient(u64 layerId) noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    Composition* comp = project_ ? current_composition() : nullptr;
    const Layer* l = comp ? comp->layer(LayerId::unpack(layerId)) : nullptr;
    return l ? static_cast<i32>(l->autoOrient) : -1;
}

namespace {
/// Valor da trilha no quadro local (keys/expressão), ou o parado.
f32 track_value(const Layer& l, TrackProperty p, FrameIndex local, f32 fallback) noexcept {
    const Track* t = l.tracks.find(p);
    return t ? t->value_or(local, fallback) : fallback;
}
} // namespace

bool Engine::set_camera_two_node(u64 layerId, bool on) noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    drain_commands_locked();
    Composition* comp = project_ ? current_composition() : nullptr;
    Layer* l = comp ? comp->layer(LayerId::unpack(layerId)) : nullptr;
    if (!l || l->locked || l->kind != LayerKind::Camera) return false;
    if (l->camera.twoNode == on) return true;
    history_.before_mutation(*comp, project_->timeline().current(), on ? "câmera de dois nós" : "câmera de um nó");
    bump_model_revision_locked();
    l->camera.twoNode = on;
    if (on) {
        // O POI nasce no eixo da câmera sem a rotação (posição + Z × foco): a
        // mira é a identidade e a rotação X/Y/Z continua girando por cima —
        // a imagem não pula ao ligar. Keys de POI que já existam mandam.
        const FrameIndex local = l->local_time(playback_.current());
        const Vec3 pos{track_value(*l, TrackProperty::PositionX, local, l->transform.position.x),
                       track_value(*l, TrackProperty::PositionY, local, l->transform.position.y),
                       track_value(*l, TrackProperty::PositionZ, local, l->transform.position.z)};
        const f32 d = std::isfinite(l->camera.focusDistance) && l->camera.focusDistance > 1.0f ? l->camera.focusDistance : 1000.0f;
        l->camera.poi = Vec3{pos.x, pos.y, pos.z + d};
        for (const TrackProperty p : {TrackProperty::CameraPoiX, TrackProperty::CameraPoiY, TrackProperty::CameraPoiZ}) {
            if (Track* t = l->tracks.find(p); t && t->keys.empty()) {
                t->staticValue = p == TrackProperty::CameraPoiX ? l->camera.poi.x : p == TrackProperty::CameraPoiY ? l->camera.poi.y : l->camera.poi.z;
            }
        }
    }
    project_->mark_dirty();
    request_render();
    return true;
}

bool Engine::set_camera_poi(u64 layerId, f32 x, f32 y, f32 z, bool undo) noexcept {
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z)) return false;
    std::lock_guard<std::mutex> lock(modelMutex_);
    drain_commands_locked();
    Composition* comp = project_ ? current_composition() : nullptr;
    Layer* l = comp ? comp->layer(LayerId::unpack(layerId)) : nullptr;
    if (!l || l->locked || l->kind != LayerKind::Camera) return false;
    if (undo) history_.before_mutation(*comp, project_->timeline().current(), "ponto de interesse");
    bump_model_revision_locked();
    const FrameIndex local = l->local_time(playback_.current());
    const f32 v[3] = {std::clamp(x, -1.0e6f, 1.0e6f), std::clamp(y, -1.0e6f, 1.0e6f), std::clamp(z, -1.0e6f, 1.0e6f)};
    const TrackProperty props[3] = {TrackProperty::CameraPoiX, TrackProperty::CameraPoiY, TrackProperty::CameraPoiZ};
    bool keyed = false;
    for (const TrackProperty p : props) if (const Track* t = l->tracks.find(p); t && !t->keys.empty()) keyed = true;
    if (keyed) {
        // Animado: a edição vira (ou troca) o key no cabeçote, nos três eixos.
        for (u32 i = 0; i < 3; ++i) {
            Track& t = l->tracks.get_or_create(props[i], kInvalidIndex, 0);
            if (const u32 at = t.find_exact(local); at != kInvalidIndex) t.keys[at].value = v[i];
            else (void)t.set(local, v[i], Interpolation::Linear);
        }
    } else {
        l->camera.poi = Vec3{v[0], v[1], v[2]};
        for (u32 i = 0; i < 3; ++i) if (Track* t = l->tracks.find(props[i])) t->staticValue = v[i];
    }
    project_->mark_dirty();
    request_render();
    return true;
}

bool Engine::query_camera_poi(u64 layerId, f32* out4) noexcept {
    if (!out4) return false;
    std::lock_guard<std::mutex> lock(modelMutex_);
    Composition* comp = project_ ? current_composition() : nullptr;
    const Layer* l = comp ? comp->layer(LayerId::unpack(layerId)) : nullptr;
    if (!l || l->kind != LayerKind::Camera) return false;
    const FrameIndex local = l->local_time(playback_.current());
    out4[0] = l->camera.twoNode ? 1.0f : 0.0f;
    out4[1] = track_value(*l, TrackProperty::CameraPoiX, local, l->camera.poi.x);
    out4[2] = track_value(*l, TrackProperty::CameraPoiY, local, l->camera.poi.y);
    out4[3] = track_value(*l, TrackProperty::CameraPoiZ, local, l->camera.poi.z);
    return true;
}

} // namespace aurea
