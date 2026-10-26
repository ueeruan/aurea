// =============================================================================
//  Aurea / engine / EngineOmegaAppearance.cpp
//
//  Omega 0.3.2, área 05 (aparência): as regras de edição dos painéis Borda e
//  sombra, Cor e preenchimento e do ímã de Mover/Escala. A UI (Android e iOS)
//  só chama; a conta mora aqui, igual para as duas plataformas.
//
//  P0-4 · decorações: efeitos que já existem (Contorno da silhueta = borda,
//         Sombra projetada = sombra) com papel fixo, sempre no fim da pilha.
//  P1-4 · preenchimento da forma: cor animável e degradê (timeline/ShapeFill.hpp).
//  P1-2 · alvos do ímã (bordas e centros das outras camadas e da composição).
// =============================================================================
#include "aurea/Engine.hpp"
#include "aurea/bridge/BridgePods.hpp"
#include "aurea/core/SnapMath.hpp"
#include "aurea/effects/EffectRegistry.hpp"
#include "aurea/timeline/Composition.hpp"
#include "aurea/timeline/Layer.hpp"
#include "aurea/timeline/ShapeFill.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

namespace aurea {

namespace {

// Índices dos parâmetros dos efeitos reaproveitados (append-only nos efeitos:
// MatteEffects.cpp StrokeOutline e FinishingEffects.cpp DropShadow).
constexpr u32 kOutlineWidth = 0, kOutlineColor = 1, kOutlineSoftness = 2, kOutlinePosition = 3, kOutlineOpacity = 4;
constexpr f32 kOutlineInside = 2.0f;   ///< enum "Posição": 0 Fora, 1 Centro, 2 Dentro
constexpr u32 kShadowColor = 0, kShadowOpacity = 1, kShadowDirection = 2, kShadowDistance = 3, kShadowSoftness = 4;

/// Primeira posição do grupo do papel e uma depois da última (a pilha está
/// sempre ordenada por papel: comuns, traço, bordas, sombra).
void role_span(const Layer& l, u8 role, u32& begin, u32& end) noexcept {
    begin = 0;
    end = 0;
    for (u32 i = 0; i < l.effects.size(); ++i) {
        if (l.effects[i].role < role) begin = i + 1;
        if (l.effects[i].role <= role) end = i + 1;
    }
    if (end < begin) end = begin;
}

/// Grava o parâmetro escalar `param` (componente 0) da instância: keyframe no
/// tempo local quando a trilha dele já tem chave, senão o valor parado.
void put_effect_scalar(Layer& l, EffectInstance& e, u32 param, f32 value, FrameIndex local) noexcept {
    if (param >= e.params.size()) return;
    if (Track* tr = l.tracks.find(TrackProperty::EffectParam, e.id, param_track_key(param, 0)); tr && !tr->keys.empty()) {
        const u32 k = tr->find_exact(local);
        if (k != kInvalidIndex) tr->keys[k].value = value;
        else (void)tr->set(local, value, Interpolation::Bezier);
        return;
    }
    e.params[param].constant.v[0] = value;
}

bool is_sdf_shape(const Layer* l) noexcept {
    return l && l->kind == LayerKind::Shape && l->shape.shapeType != kShapeVector;
}

/// Grava um componente do preenchimento (ver `put_effect_scalar`).
void put_fill_component(Layer& l, u32 param, f32 value, FrameIndex local) noexcept {
    value = shape_fill::clamp_component(param, value);
    if (Track* tr = l.tracks.find(TrackProperty::ShapeParam, 0, param); tr && !tr->keys.empty()) {
        const u32 k = tr->find_exact(local);
        if (k != kInvalidIndex) tr->keys[k].value = value;
        else (void)tr->set(local, value, Interpolation::Bezier);
        return;
    }
    if (f32* field = shape_fill::static_field(l.shape, param)) *field = value;
}

} // namespace

// =============================================================================
// Decorações (Borda e sombra)
// =============================================================================
u32 Engine::decoration_add(u64 layerId, u32 role) noexcept {
    if (role != kEffectRoleBorder && role != kEffectRoleShadow) return kInvalidIndex;
    std::lock_guard<std::mutex> lock(modelMutex_);
    drain_commands_locked();
    Composition* comp = project_ ? current_composition() : nullptr;
    Layer* l = comp ? comp->layer(LayerId::unpack(layerId)) : nullptr;
    if (!l || l->locked) return kInvalidIndex;
    const EffectTypeId type = effect_type_id(role == kEffectRoleBorder ? effect_keys::kStrokeOutline : effect_keys::kDropShadow);
    const ParameterRegistry* params = effectRegistry_.params(type);
    if (!params) return kInvalidIndex;
    if (role == kEffectRoleShadow) {
        // Uma sombra por camada: a que existe volta ligada.
        for (EffectInstance& e : l->effects) {
            if (e.role != kEffectRoleShadow) continue;
            if (!e.enabled) {
                history_.before_mutation(*comp, project_->timeline().current(), "ligar sombra");
                e.enabled = true;
                bump_model_revision_locked();
                project_->mark_dirty();
                request_render();
            }
            return e.id;
        }
    }
    if (l->effects.size() >= kMaxEffectCount) return kInvalidIndex;
    history_.before_mutation(*comp, project_->timeline().current(),
                             role == kEffectRoleBorder ? "adicionar borda" : "adicionar sombra");
    EffectInstance e;
    e.id = l->alloc_effect_id();
    e.type = type;
    e.role = static_cast<u8>(role);
    e.expanded = false;
    initialize_instance(e, *params);
    auto set = [&](u32 index, ParamValue v) { if (index < e.params.size()) e.params[index].constant = v; };
    if (role == kEffectRoleBorder) {
        // A borda nova da referência: dentro, branca, 6 px, sem suavidade.
        set(kOutlineWidth, ParamValue::scalar(6.0f));
        set(kOutlineColor, ParamValue::color(1.0f, 1.0f, 1.0f, 1.0f));
        set(kOutlineSoftness, ParamValue::scalar(0.0f));
        set(kOutlinePosition, ParamValue::scalar(kOutlineInside));
        set(kOutlineOpacity, ParamValue::scalar(100.0f));
    } else {
        // Sombra: preta, deslocamento (5, 5) px → 45° a 7,07 px; suavidade 8 px.
        set(kShadowColor, ParamValue::color(0.0f, 0.0f, 0.0f, 1.0f));
        set(kShadowOpacity, ParamValue::scalar(60.0f));
        set(kShadowDirection, ParamValue::scalar(45.0f));
        set(kShadowDistance, ParamValue::scalar(std::sqrt(50.0f)));
        set(kShadowSoftness, ParamValue::scalar(8.0f));
    }
    u32 begin = 0, end = 0;
    role_span(*l, static_cast<u8>(role), begin, end);
    const u32 id = e.id;
    l->modelEffects = true;
    l->effects.insert(l->effects.begin() + end, std::move(e));
    bump_model_revision_locked();
    project_->mark_dirty();
    request_render();
    return id;
}

bool Engine::decoration_move(u64 layerId, u32 effectId, u32 slot) noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    drain_commands_locked();
    Composition* comp = project_ ? current_composition() : nullptr;
    Layer* l = comp ? comp->layer(LayerId::unpack(layerId)) : nullptr;
    if (!l || l->locked) return false;
    u32 from = kInvalidIndex;
    for (u32 i = 0; i < l->effects.size(); ++i) if (l->effects[i].id == effectId) from = i;
    if (from == kInvalidIndex || l->effects[from].role == kEffectRoleNone) return false;
    u32 begin = 0, end = 0;
    role_span(*l, l->effects[from].role, begin, end);
    const u32 to = std::min(begin + slot, end - 1);
    if (to == from) return false;
    history_.before_mutation(*comp, project_->timeline().current(), "reordenar borda");
    EffectInstance moving = std::move(l->effects[from]);
    l->effects.erase(l->effects.begin() + from);
    l->effects.insert(l->effects.begin() + to, std::move(moving));
    bump_model_revision_locked();
    project_->mark_dirty();
    request_render();
    return true;
}

bool Engine::decoration_set_shadow_offset(u64 layerId, u32 effectId, f32 dx, f32 dy) noexcept {
    if (!std::isfinite(dx) || !std::isfinite(dy)) return false;
    std::lock_guard<std::mutex> lock(modelMutex_);
    drain_commands_locked();
    Composition* comp = project_ ? current_composition() : nullptr;
    Layer* l = comp ? comp->layer(LayerId::unpack(layerId)) : nullptr;
    if (!l || l->locked) return false;
    EffectInstance* e = nullptr;
    for (EffectInstance& item : l->effects) if (item.id == effectId) e = &item;
    if (!e || e->type != effect_type_id(effect_keys::kDropShadow)) return false;
    const FrameIndex local = l->local_time(playback_.current());
    const f32 distance = std::min(std::hypot(dx, dy), 5000.0f);
    history_.before_mutation(*comp, project_->timeline().current(), "posição da sombra");
    // Sem deslocamento a direção fica a de antes (girar para 0° mudaria o
    // keyframe de direção sem efeito visível).
    if (distance > 1e-4f) put_effect_scalar(*l, *e, kShadowDirection, std::atan2(dy, dx) * kRad2Deg, local);
    put_effect_scalar(*l, *e, kShadowDistance, distance, local);
    bump_model_revision_locked();
    project_->mark_dirty();
    request_render();
    return true;
}

bool Engine::decoration_shadow_offset(u64 layerId, u32 effectId, f32& dx, f32& dy) noexcept {
    dx = dy = 0.0f;
    std::lock_guard<std::mutex> lock(modelMutex_);
    drain_commands_locked();
    Composition* comp = project_ ? current_composition() : nullptr;
    const Layer* l = comp ? comp->layer(LayerId::unpack(layerId)) : nullptr;
    if (!l) return false;
    const EffectInstance* e = nullptr;
    for (const EffectInstance& item : l->effects) if (item.id == effectId) e = &item;
    if (!e || e->type != effect_type_id(effect_keys::kDropShadow)) return false;
    const ParameterRegistry* params = effectRegistry_.params(e->type);
    if (!params || params->count() <= kShadowDistance) return false;
    const FrameIndex local = l->local_time(playback_.current());
    const f32 dir = evaluate_param(l->tracks, *e, kShadowDirection, params->at(kShadowDirection), local).v[0] * kDeg2Rad;
    const f32 dist = evaluate_param(l->tracks, *e, kShadowDistance, params->at(kShadowDistance), local).v[0];
    if (!std::isfinite(dir) || !std::isfinite(dist)) return false;
    dx = std::cos(dir) * dist;
    dy = std::sin(dir) * dist;
    return true;
}

// =============================================================================
// Cor e preenchimento (forma)
// =============================================================================
u32 Engine::query_shape_fill(u64 layerId, f32* out, u32 capacity) noexcept {
    if (!out || capacity < kShapeFillFloats) return 0;
    std::lock_guard<std::mutex> lock(modelMutex_);
    drain_commands_locked();
    Composition* comp = project_ ? current_composition() : nullptr;
    const Layer* l = comp ? comp->layer(LayerId::unpack(layerId)) : nullptr;
    if (!l) return 0;
    std::fill(out, out + kShapeFillFloats, 0.0f);
    const FrameIndex local = l->local_time(playback_.current());
    const shape_fill::Eval e = shape_fill::evaluate(*l, static_cast<f64>(local.value));
    out[0] = e.kind;
    out[1] = e.type;
    out[2] = e.filled ? 1.0f : 0.0f;
    const f32 colors[12] = {e.color.x, e.color.y, e.color.z, e.color.w, e.color0.x, e.color0.y, e.color0.z, e.color0.w,
                            e.color1.x, e.color1.y, e.color1.z, e.color1.w};
    std::copy(colors, colors + 12, out + 3);
    out[15] = e.start.x; out[16] = e.start.y;
    out[17] = e.end.x;   out[18] = e.end.y;
    out[19] = static_cast<f32>(e.animated);
    out[20] = static_cast<f32>(e.keyHere);
    out[21] = is_sdf_shape(l) ? 1.0f : 0.0f;
    out[22] = l->shape.bounds.w;
    out[23] = l->shape.bounds.h;
    return kShapeFillFloats;
}

bool Engine::set_shape_fill_mode(u64 layerId, u32 mode) noexcept {
    if (mode > 2) return false;
    std::lock_guard<std::mutex> lock(modelMutex_);
    drain_commands_locked();
    Composition* comp = project_ ? current_composition() : nullptr;
    Layer* l = comp ? comp->layer(LayerId::unpack(layerId)) : nullptr;
    if (!is_sdf_shape(l) || l->locked) return false;
    ShapeData& s = l->shape;
    const bool filled = mode != 0;
    const u8 kind = mode == 2 ? 1 : (mode == 1 ? 0 : s.fillKind);
    if (s.filled == filled && s.fillKind == kind) return true;
    history_.before_mutation(*comp, project_->timeline().current(), "tipo do preenchimento");
    if (mode == 2 && s.fillKind != 1) {
        // Degradê novo parte da cor atual: início = a cor; fim = preto ou
        // branco (o que contrasta), com o mesmo alfa.
        const FrameIndex local = l->local_time(playback_.current());
        const Vec4 c = shape_fill::evaluate(*l, static_cast<f64>(local.value)).color;
        const bool light = 0.2126f * c.x + 0.7152f * c.y + 0.0722f * c.z > 0.5f;
        s.gradColor0 = c;
        s.gradColor1 = light ? Vec4{0.0f, 0.0f, 0.0f, c.w} : Vec4{1.0f, 1.0f, 1.0f, c.w};
    }
    s.filled = filled;
    s.fillKind = kind;
    bump_model_revision_locked();
    project_->mark_dirty();
    request_render();
    return true;
}

bool Engine::set_shape_gradient_type(u64 layerId, u32 type) noexcept {
    if (type > 2) return false;
    std::lock_guard<std::mutex> lock(modelMutex_);
    drain_commands_locked();
    Composition* comp = project_ ? current_composition() : nullptr;
    Layer* l = comp ? comp->layer(LayerId::unpack(layerId)) : nullptr;
    if (!is_sdf_shape(l) || l->locked) return false;
    if (l->shape.gradType == type) return true;
    history_.before_mutation(*comp, project_->timeline().current(), "tipo do degradê");
    l->shape.gradType = static_cast<u8>(type);
    if (type == 1 || type == 2) {
        // Radial e angular partem do CENTRO (a ponta inicial é o centro); a
        // linear volta da esquerda para a direita. Só quando as pontas ainda
        // são as de fábrica de outro tipo (a pessoa não as moveu).
        const bool linearDefault = l->shape.gradStart.x == 0.0f && l->shape.gradStart.y == 0.5f
                                && l->shape.gradEnd.x == 1.0f && l->shape.gradEnd.y == 0.5f;
        if (linearDefault) { l->shape.gradStart = Vec2{0.5f, 0.5f}; l->shape.gradEnd = Vec2{1.0f, 0.5f}; }
    } else {
        const bool centerDefault = l->shape.gradStart.x == 0.5f && l->shape.gradStart.y == 0.5f
                                && l->shape.gradEnd.x == 1.0f && l->shape.gradEnd.y == 0.5f;
        if (centerDefault) { l->shape.gradStart = Vec2{0.0f, 0.5f}; l->shape.gradEnd = Vec2{1.0f, 0.5f}; }
    }
    bump_model_revision_locked();
    project_->mark_dirty();
    request_render();
    return true;
}

bool Engine::set_shape_fill_value(u64 layerId, u32 group, const f32* values, u32 count) noexcept {
    if (!values || group >= shape_fill::kGroupCount || count != shape_fill::group_size(group)) return false;
    for (u32 i = 0; i < count; ++i) if (!std::isfinite(values[i])) return false;
    std::lock_guard<std::mutex> lock(modelMutex_);
    drain_commands_locked();
    Composition* comp = project_ ? current_composition() : nullptr;
    Layer* l = comp ? comp->layer(LayerId::unpack(layerId)) : nullptr;
    if (!is_sdf_shape(l) || l->locked) return false;
    history_.before_mutation(*comp, project_->timeline().current(), "cor do preenchimento");
    const FrameIndex local = l->local_time(playback_.current());
    const u32 first = shape_fill::group_first(group);
    for (u32 i = 0; i < count; ++i) put_fill_component(*l, first + i, values[i], local);
    bump_model_revision_locked();
    project_->mark_dirty();
    request_render();
    return true;
}

bool Engine::toggle_shape_fill_key(u64 layerId, u32 group) noexcept {
    if (group >= shape_fill::kGroupCount) return false;
    std::lock_guard<std::mutex> lock(modelMutex_);
    drain_commands_locked();
    Composition* comp = project_ ? current_composition() : nullptr;
    Layer* l = comp ? comp->layer(LayerId::unpack(layerId)) : nullptr;
    if (!is_sdf_shape(l) || l->locked) return false;
    const FrameIndex local = l->local_time(playback_.current());
    const u32 first = shape_fill::group_first(group), n = shape_fill::group_size(group);
    bool keyed = false;
    for (u32 p = first; p < first + n; ++p) {
        const Track* tr = l->tracks.find(TrackProperty::ShapeParam, 0, p);
        keyed |= tr && tr->find_exact(local) != kInvalidIndex;
    }
    history_.before_mutation(*comp, project_->timeline().current(), keyed ? "remover keyframe" : "adicionar keyframe");
    for (u32 p = first; p < first + n; ++p) {
        Track& tr = l->tracks.get_or_create(TrackProperty::ShapeParam, 0, p);
        const f32 now = tr.keys.empty() ? shape_fill::static_component(l->shape, p) : tr.sample(local);
        if (keyed) {
            // A última chave que sai deixa o valor dela parado na forma (não
            // volta ao valor antigo).
            if (tr.keys.size() == 1 && tr.find_exact(local) != kInvalidIndex)
                if (f32* field = shape_fill::static_field(l->shape, p)) *field = shape_fill::clamp_component(p, now);
            (void)tr.remove(local);
        } else {
            (void)tr.set(local, shape_fill::clamp_component(p, now), Interpolation::Bezier);
        }
    }
    bump_model_revision_locked();
    project_->mark_dirty();
    request_render();
    return true;
}

// =============================================================================
// Ímã (alvos)
// =============================================================================
u32 Engine::query_snap_targets(u64 layerId, u32 axis, f32* values, f32* guides, u32 capacity) noexcept {
    if (!values || capacity == 0 || axis > 3) return 0;
    std::vector<u64> others;
    f32 compW = 0.0f, compH = 0.0f;
    {
        std::lock_guard<std::mutex> lock(modelMutex_);
        drain_commands_locked();
        Composition* comp = project_ ? current_composition() : nullptr;
        if (!comp || !comp->layer(LayerId::unpack(layerId))) return 0;
        const FrameIndex now = playback_.current();
        compW = static_cast<f32>(comp->width());
        compH = static_cast<f32>(comp->height());
        const OrderedIds<LayerId>& order = comp->order();
        for (u32 i = 0; i < order.size(); ++i) {
            const LayerId id = order.at(i);
            if (id.pack() == layerId) continue;
            const Layer* o = comp->layer(id);
            if (!o || !o->visible || o->adjustment || !o->contains_time(now)) continue;
            if (o->kind == LayerKind::Audio || o->kind == LayerKind::Camera || o->kind == LayerKind::Light
                || o->kind == LayerKind::Null) continue;
            others.push_back(id.pack());
        }
    }
    // Caixa (px da composição) de uma camada pelos cantos do palco — o MESMO
    // cálculo do renderer (query_layer_detail). Em perspectiva não vale.
    struct Box { f32 x0, y0, x1, y1; };
    auto box_of = [this](u64 id, Box& b, bridge::LayerDetailPOD* keep = nullptr) {
        bridge::LayerDetailPOD d;
        if (!query_layer_detail(id, d)) return false;
        if (!(d.geomFlags & bridge::kGeomCornersValid) || (d.geomFlags & bridge::kGeomPerspective)) return false;
        b = Box{d.corners[0], d.corners[1], d.corners[0], d.corners[1]};
        for (int i = 1; i < 4; ++i) {
            b.x0 = std::min(b.x0, d.corners[i * 2]); b.x1 = std::max(b.x1, d.corners[i * 2]);
            b.y0 = std::min(b.y0, d.corners[i * 2 + 1]); b.y1 = std::max(b.y1, d.corners[i * 2 + 1]);
        }
        if (keep) *keep = d;
        return std::isfinite(b.x0) && std::isfinite(b.x1) && std::isfinite(b.y0) && std::isfinite(b.y1);
    };
    std::vector<f32> v, g;
    if (axis >= 2) {
        // Marcas das réguas de Escala: o tamanho das outras camadas e da composição.
        v.push_back(axis == 2 ? compW : compH);
        for (u64 id : others) {
            Box b{};
            if (box_of(id, b)) v.push_back(axis == 2 ? b.x1 - b.x0 : b.y1 - b.y0);
        }
        g = v;
    } else {
        bridge::LayerDetailPOD self;
        Box me{};
        if (!box_of(layerId, me, &self)) return 0;
        // Pai girado/inclinado: a posição não anda num eixo só — sem ímã.
        const f32 a = self.parentAffine[0], b = self.parentAffine[1], c = self.parentAffine[2], d = self.parentAffine[3];
        if (std::fabs(b) > 1e-4f || std::fabs(c) > 1e-4f) return 0;
        const f32 gain = axis == 0 ? a : d;   // px da composição por unidade da posição
        if (!(std::fabs(gain) > 1e-6f)) return 0;
        const f32 pos = self.position[axis];
        const f32 lo = axis == 0 ? me.x0 : me.y0, hi = axis == 0 ? me.x1 : me.y1;
        const f32 edges[3] = {lo, (lo + hi) * 0.5f, hi};
        std::vector<f32> lines = {0.0f, (axis == 0 ? compW : compH) * 0.5f, axis == 0 ? compW : compH};
        for (u64 id : others) {
            Box o{};
            if (!box_of(id, o)) continue;
            const f32 olo = axis == 0 ? o.x0 : o.y0, ohi = axis == 0 ? o.x1 : o.y1;
            lines.push_back(olo);
            lines.push_back((olo + ohi) * 0.5f);
            lines.push_back(ohi);
        }
        for (f32 line : lines)
            for (f32 edge : edges) {
                v.push_back(pos + (line - edge) / gain);
                g.push_back(line);
            }
    }
    const u32 n = snap::sort_unique(v.data(), g.data(), static_cast<u32>(v.size()));
    const u32 written = std::min(n, capacity);
    std::copy(v.begin(), v.begin() + written, values);
    if (guides) std::copy(g.begin(), g.begin() + written, guides);
    return written;
}

} // namespace aurea
