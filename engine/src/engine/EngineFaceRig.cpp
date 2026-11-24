// =============================================================================
//  Aurea / engine / EngineFaceRig.cpp
//
//  API do RIG 2D DE ROSTO no palco (aurea/effects/FaceRig.hpp). Como o
//  Fantoche, a UI fala em FRAÇÃO da camada (u, v na caixa dela) e o motor
//  guarda as âncoras nos parâmetros ocultos do efeito. Pôr o efeito numa
//  imagem já acha os pontos (no mesmo passo de desfazer); "Detectar de novo"
//  refaz; "Ajustar pontos" arrasta cada alça (o gesto inteiro = um passo).
// =============================================================================
#include "aurea/Engine.hpp"

#include "aurea/effects/EffectRegistry.hpp"
#include "aurea/effects/FaceRig.hpp"

#include <algorithm>
#include <cmath>

namespace aurea {
namespace {

using namespace face_rig;

EffectInstance* face_rig_effect(Layer* l, u32 effectId) noexcept {
    if (!l) return nullptr;
    for (EffectInstance& e : l->effects)
        if (e.id == effectId && e.type == effect_type_id(kFaceRigKey) && e.params.size() >= kParamCount) return &e;
    return nullptr;
}

Vec2 get(const EffectInstance& e, u32 p) noexcept { return Vec2{e.params[p].constant.v[0], e.params[p].constant.v[1]}; }
void put(EffectInstance& e, u32 p, Vec2 v) noexcept {
    e.params[p].constant.v[0] = v.x;
    e.params[p].constant.v[1] = v.y;
}

Anchors read_anchors(const EffectInstance& e) noexcept {
    Anchors a;
    a.face = get(e, kFaceCenter);
    a.faceR = get(e, kFaceSize);
    a.eye[0] = get(e, kEyeLeft);
    a.eyeR[0] = get(e, kEyeLeftSize);
    a.eye[1] = get(e, kEyeRight);
    a.eyeR[1] = get(e, kEyeRightSize);
    a.mouth = get(e, kMouth);
    a.mouthR = get(e, kMouthSize);
    a.neck = get(e, kNeck);
    a.hairTop = get(e, kHairTop);
    return a;
}

void write_anchors(EffectInstance& e, const Anchors& a, f32 placed) noexcept {
    put(e, kFaceCenter, a.face);
    put(e, kFaceSize, a.faceR);
    put(e, kEyeLeft, a.eye[0]);
    put(e, kEyeLeftSize, a.eyeR[0]);
    put(e, kEyeRight, a.eye[1]);
    put(e, kEyeRightSize, a.eyeR[1]);
    put(e, kMouth, a.mouth);
    put(e, kMouthSize, a.mouthR);
    put(e, kNeck, a.neck);
    put(e, kHairTop, a.hairTop);
    e.params[kPlaced].constant.v[0] = placed;
}

/// Alças (fração): centros e cantos (centro + tamanho).
void handles_of(const Anchors& a, Vec2* h) noexcept {
    h[kHandleFace] = a.face;
    h[kHandleFaceEdge] = a.face + a.faceR;
    h[kHandleEyeLeft] = a.eye[0];
    h[kHandleEyeLeftEdge] = a.eye[0] + a.eyeR[0];
    h[kHandleEyeRight] = a.eye[1];
    h[kHandleEyeRightEdge] = a.eye[1] + a.eyeR[1];
    h[kHandleMouth] = a.mouth;
    h[kHandleMouthEdge] = a.mouth + a.mouthR;
    h[kHandleNeck] = a.neck;
    h[kHandleHairTop] = a.hairTop;
}

/// Uma alça arrastada para `p`: o centro leva o canto junto; o canto muda o tamanho.
void move_handle(Anchors& a, u32 handle, Vec2 p) noexcept {
    auto size = [](Vec2 c, Vec2 q, f32 minimum) {
        return Vec2{std::max(std::fabs(q.x - c.x), minimum), std::max(std::fabs(q.y - c.y), minimum)};
    };
    switch (handle) {
        case kHandleFace: a.face = p; break;
        case kHandleFaceEdge: a.faceR = size(a.face, p, 0.02f); break;
        case kHandleEyeLeft: a.eye[0] = p; break;
        case kHandleEyeLeftEdge: a.eyeR[0] = size(a.eye[0], p, 0.002f); break;
        case kHandleEyeRight: a.eye[1] = p; break;
        case kHandleEyeRightEdge: a.eyeR[1] = size(a.eye[1], p, 0.002f); break;
        case kHandleMouth: a.mouth = p; break;
        case kHandleMouthEdge: a.mouthR = size(a.mouth, p, 0.002f); break;
        case kHandleNeck: a.neck = p; break;
        case kHandleHairTop: a.hairTop = p; break;
        default: break;
    }
}

} // namespace

void Engine::face_rig_seed_locked(const Layer& l, EffectInstance& e) noexcept {
    if (e.type != effect_type_id(kFaceRigKey) || e.params.size() < kParamCount || l.kind != LayerKind::Image) return;
    const auto it = images_.find(l.source.pack());
    if (it == images_.end() || !it->second) return;
    const ImagePixels& px = *it->second;
    if (!px.width || !px.height || px.rgba.size() < static_cast<usize>(px.width) * px.height * 4) return;
    Anchors a;
    const u32 quality = auto_place(px.rgba.data(), px.width, px.height, a);
    write_anchors(e, a, static_cast<f32>(quality));
}

u32 Engine::query_face_rig(u64 layerId, u32 effectId, f32* out, u32 capacity) noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    drain_commands_locked();
    InlineEditScope inlineScope(*this, layerId);
    Composition* comp = project_ ? current_composition() : nullptr;
    Layer* l = comp ? comp->layer(LayerId::unpack(layerId)) : nullptr;
    const EffectInstance* e = face_rig_effect(l, effectId);
    if (!e) return 0;
    if (!out || capacity < kFaceRigQueryFloats) return kFaceRigQueryFloats;
    Vec2 h[kHandleCount];
    handles_of(read_anchors(*e), h);
    for (u32 i = 0; i < kHandleCount; ++i) { out[i * 2] = h[i].x; out[i * 2 + 1] = h[i].y; }
    out[kFaceRigHandles * 2] = e->params[kPlaced].constant.v[0];
    return kFaceRigQueryFloats;
}

i32 Engine::face_rig_auto_place(u64 layerId, u32 effectId) noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    drain_commands_locked();
    InlineEditScope inlineScope(*this, layerId, true);
    Composition* comp = project_ ? current_composition() : nullptr;
    Layer* l = comp ? comp->layer(LayerId::unpack(layerId)) : nullptr;
    EffectInstance* e = face_rig_effect(l, effectId);
    if (!e || l->locked) return -1;
    Anchors a = proportional_anchors(0.2f, 0.12f, 0.8f, 0.78f);
    u32 quality = 0;
    if (l->kind == LayerKind::Image) {
        const auto it = images_.find(l->source.pack());
        if (it != images_.end() && it->second) {
            const ImagePixels& px = *it->second;
            if (px.width && px.height && px.rgba.size() >= static_cast<usize>(px.width) * px.height * 4)
                quality = auto_place(px.rgba.data(), px.width, px.height, a);
        }
    }
    history_.before_mutation(*comp, project_->timeline().current(), "pontos do rosto");
    bump_model_revision_locked();
    write_anchors(*e, a, static_cast<f32>(quality));
    project_->mark_dirty();
    request_render();
    return static_cast<i32>(quality);
}

bool Engine::face_rig_move_handle(u64 layerId, u32 effectId, u32 handle, f32 u, f32 v, bool continuing) noexcept {
    if (handle >= kHandleCount || !std::isfinite(u) || !std::isfinite(v)) return false;
    std::lock_guard<std::mutex> lock(modelMutex_);
    drain_commands_locked();
    InlineEditScope inlineScope(*this, layerId, true);
    Composition* comp = project_ ? current_composition() : nullptr;
    Layer* l = comp ? comp->layer(LayerId::unpack(layerId)) : nullptr;
    EffectInstance* e = face_rig_effect(l, effectId);
    if (!e || l->locked) return false;
    Anchors a = read_anchors(*e);
    move_handle(a, handle, Vec2{clampf(u, -1.0f, 2.0f), clampf(v, -1.0f, 2.0f)});
    if (!continuing) history_.before_mutation(*comp, project_->timeline().current(), "mover ponto do rosto");
    bump_model_revision_locked();
    write_anchors(*e, a, 3.0f);
    project_->mark_dirty();
    request_render();
    return true;
}

bool Engine::face_rig_set_handles(u64 layerId, u32 effectId, const f32* uv, u32 count) noexcept {
    if (!uv || count < kFaceRigHandles * 2) return false;
    for (u32 i = 0; i < kFaceRigHandles * 2; ++i)
        if (!std::isfinite(uv[i])) return false;
    std::lock_guard<std::mutex> lock(modelMutex_);
    drain_commands_locked();
    InlineEditScope inlineScope(*this, layerId, true);
    Composition* comp = project_ ? current_composition() : nullptr;
    Layer* l = comp ? comp->layer(LayerId::unpack(layerId)) : nullptr;
    EffectInstance* e = face_rig_effect(l, effectId);
    if (!e || l->locked) return false;
    Anchors a = read_anchors(*e);
    // Centros primeiro: os cantos medem a partir deles.
    for (u32 pass = 0; pass < 2; ++pass)
        for (u32 i = 0; i < kHandleCount; ++i) {
            const bool edge = i == kHandleFaceEdge || i == kHandleEyeLeftEdge || i == kHandleEyeRightEdge || i == kHandleMouthEdge;
            if (edge == (pass == 0)) continue;
            move_handle(a, i, Vec2{clampf(uv[i * 2], -1.0f, 2.0f), clampf(uv[i * 2 + 1], -1.0f, 2.0f)});
        }
    history_.before_mutation(*comp, project_->timeline().current(), "pontos do rosto");
    bump_model_revision_locked();
    write_anchors(*e, a, 3.0f);
    project_->mark_dirty();
    request_render();
    return true;
}

} // namespace aurea
