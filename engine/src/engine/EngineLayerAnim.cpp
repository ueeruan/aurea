// =============================================================================
//  Aurea / engine / EngineLayerAnim.cpp
//
//  API dos animadores de CAMADA (entrada/saída/wiggle em qualquer camada), do
//  comprimento do desfoque de movimento por camada e do escopo de grupo:
//  pôr/tirar camadas de um grupo, câmera que atravessa o grupo e as camadas
//  que um ajuste afeta. O padrão de toda mutação é o do motor: modelMutex_,
//  history_.before_mutation, modelRevision_, mark_dirty, request_render.
// =============================================================================
#include "aurea/Engine.hpp"

#include "aurea/timeline/LayerAnimator.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <string>

namespace aurea {
namespace {

constexpr u32 kMaxLayerAnimators = 32;

/// Progresso padrão: 0 → 100 % em 1 s a partir do começo da camada (ou até o
/// fim dela, se for mais curta).
void seed_progress(Layer& l, u32 index, f64 fps) {
    Track& tr = l.tracks.get_or_create(TrackProperty::LayerAnimParam, index, layeranim::kProgress);
    tr.clear();
    const i64 span = std::max<i64>(1, l.end.value - l.start.value - 1);
    const i64 dur = std::min<i64>(span, std::max<i64>(1, static_cast<i64>(std::lround(fps > 0.0 ? fps : 30.0))));
    const FrameIndex a = l.local_time(l.start);
    (void)tr.set(a, 0.0f, Interpolation::Linear);
    (void)tr.set(FrameIndex{a.value + dur}, 100.0f, Interpolation::Linear);
}

/// Entrada ↔ saída: os keyframes do progresso espelhados no trecho da camada
/// (o quadro local t vai para começo + fim − t). A curva do animador (`ease`)
/// continua valendo: na saída o progresso desce de 100 a 0.
void mirror_progress(Layer& l, u32 index) {
    Track* tr = l.tracks.find(TrackProperty::LayerAnimParam, index, layeranim::kProgress);
    if (!tr || tr->keys.empty()) return;
    const i64 a = l.local_time(l.start).value;
    const i64 b = l.local_time(FrameIndex{std::max(l.start.value, l.end.value - 1)}).value;
    std::vector<std::pair<i64, f32>> ks;
    ks.reserve(tr->keys.size());
    for (const Keyframe& k : tr->keys) ks.emplace_back(a + b - k.time.value, k.value);
    tr->clear();
    for (const auto& [t, v] : ks) (void)tr->set(FrameIndex{t}, v, Interpolation::Linear);
}

bool time_altered(const Layer& g) noexcept {
    return g.timeRemapEnabled || g.speed != 1.0f || g.reversed || g.speed_track() != nullptr;
}

} // namespace

// =============================================================================
// Animadores de camada
// =============================================================================

u32 Engine::query_layer_animators(u64 layerId, f32* out, u32 capacity) noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    Composition* comp = project_ ? current_composition() : nullptr;
    const Layer* l = comp ? comp->layer(LayerId::unpack(layerId)) : nullptr;
    if (!l) return 0;
    const FrameIndex local = l->local_time(playback_.current());
    const u32 n = static_cast<u32>(l->layerAnimators.size());
    for (u32 i = 0; i < n && out && (i + 1) * kLayerAnimFloats <= capacity; ++i) {
        const LayerAnimator& a = l->layerAnimators[i];
        f32* v = out + i * kLayerAnimFloats;
        std::fill(v, v + kLayerAnimFloats, 0.0f);
        v[0] = a.enabled ? 1.0f : 0.0f;
        v[1] = static_cast<f32>(a.unit);
        v[2] = a.exit ? 1.0f : 0.0f;
        v[3] = static_cast<f32>(a.ease);
        v[4] = a.scaleSeparated ? 1.0f : 0.0f;
        v[5] = static_cast<f32>(a.wiggleSeed);
        u32 animated = 0, keyed = 0;
        for (u32 p = 0; p < layeranim::kParamCount; ++p) {
            f32 value = layeranim::param_static(a, p);
            if (const Track* tr = l->tracks.find(TrackProperty::LayerAnimParam, i, p); tr && tr->driven()) {
                value = tr->value_or(local, value);
                if (!tr->keys.empty()) {
                    animated |= 1u << p;
                    if (tr->find_exact(local) != kInvalidIndex) keyed |= 1u << p;
                }
            }
            v[6 + p] = value;
        }
        v[24] = static_cast<f32>(animated);
        v[25] = static_cast<f32>(keyed);
        v[26] = l->kind == LayerKind::Text ? 1.0f : 0.0f;
    }
    return n;
}

i32 Engine::add_layer_animator(u64 layerId) noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    Composition* comp = project_ ? current_composition() : nullptr;
    Layer* l = comp ? comp->layer(LayerId::unpack(layerId)) : nullptr;
    if (!l || l->layerAnimators.size() >= kMaxLayerAnimators || l->kind == LayerKind::Camera || l->kind == LayerKind::Light)
        return -1;
    history_.before_mutation(*comp, project_->timeline().current(), "novo animador");
    modelRevision_.fetch_add(1, std::memory_order_acq_rel);
    const u32 index = static_cast<u32>(l->layerAnimators.size());
    // Trilhas órfãs com este índice (não deveriam existir) saem antes.
    l->tracks.remove_if([&](const Track& t) { return t.property == TrackProperty::LayerAnimParam && t.effectIndex >= index; });
    LayerAnimator a;
    a.name = "Animador " + std::to_string(index + 1);
    a.unit = l->kind == LayerKind::Text ? 1 : 0;   // texto: letra a letra, como no app antigo
    l->layerAnimators.push_back(a);
    seed_progress(*l, index, comp->fps());
    project_->mark_dirty();
    request_render();
    return static_cast<i32>(index);
}

bool Engine::remove_layer_animator(u64 layerId, u32 index) noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    Composition* comp = project_ ? current_composition() : nullptr;
    Layer* l = comp ? comp->layer(LayerId::unpack(layerId)) : nullptr;
    if (!l || index >= l->layerAnimators.size()) return false;
    history_.before_mutation(*comp, project_->timeline().current(), "remover animador");
    modelRevision_.fetch_add(1, std::memory_order_acq_rel);
    l->layerAnimators.erase(l->layerAnimators.begin() + index);
    l->tracks.remove_if([&](const Track& t) { return t.property == TrackProperty::LayerAnimParam && t.effectIndex == index; });
    for (u32 i = 0; i < l->tracks.size(); ++i) {
        Track& t = l->tracks.at(i);
        if (t.property == TrackProperty::LayerAnimParam && t.effectIndex > index && t.effectIndex != kInvalidIndex) --t.effectIndex;
    }
    project_->mark_dirty();
    request_render();
    return true;
}

bool Engine::set_layer_animator(u64 layerId, u32 index, const f32* v) noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    Composition* comp = project_ ? current_composition() : nullptr;
    Layer* l = comp ? comp->layer(LayerId::unpack(layerId)) : nullptr;
    if (!l || !v || index >= l->layerAnimators.size()) return false;
    for (u32 i = 0; i < 6; ++i) if (!std::isfinite(v[i])) return false;
    history_.before_mutation(*comp, project_->timeline().current(), "animador");
    modelRevision_.fetch_add(1, std::memory_order_acq_rel);
    LayerAnimator& a = l->layerAnimators[index];
    a.enabled = v[0] > 0.5f;
    a.unit = static_cast<u8>(std::clamp(v[1], 0.0f, 3.0f));
    const bool exit = v[2] > 0.5f;
    if (exit != a.exit) mirror_progress(*l, index);
    a.exit = exit;
    a.ease = static_cast<u8>(std::clamp(v[3], 0.0f, 3.0f));
    a.scaleSeparated = v[4] > 0.5f;
    a.wiggleSeed = static_cast<u32>(std::clamp(v[5], 0.0f, 1.0e6f));
    project_->mark_dirty();
    request_render();
    return true;
}

bool Engine::set_layer_anim_param(u64 layerId, u32 index, u32 param, f32 value) noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    Composition* comp = project_ ? current_composition() : nullptr;
    Layer* l = comp ? comp->layer(LayerId::unpack(layerId)) : nullptr;
    if (!l || index >= l->layerAnimators.size() || !std::isfinite(value)) return false;
    f32* r = layeranim::param_ref(l->layerAnimators[index], param);
    if (!r) return false;
    history_.before_mutation(*comp, project_->timeline().current(), "valor do animador");
    modelRevision_.fetch_add(1, std::memory_order_acq_rel);
    value = layeranim::clamp_param(param, value);
    Track* tr = l->tracks.find(TrackProperty::LayerAnimParam, index, param);
    if (tr && !tr->keys.empty()) {
        // Animado: o valor vira keyframe no playhead (como nas outras propriedades).
        const FrameIndex local = l->local_time(playback_.current());
        const u32 k = tr->find_exact(local);
        if (k != kInvalidIndex) tr->keys[k].value = value;
        else (void)tr->set(local, value, param == layeranim::kProgress ? Interpolation::Linear : Interpolation::Bezier);
    } else {
        *r = value;
    }
    project_->mark_dirty();
    request_render();
    return true;
}

bool Engine::toggle_layer_anim_key(u64 layerId, u32 index, u32 param) noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    Composition* comp = project_ ? current_composition() : nullptr;
    Layer* l = comp ? comp->layer(LayerId::unpack(layerId)) : nullptr;
    if (!l || index >= l->layerAnimators.size()) return false;
    f32* r = layeranim::param_ref(l->layerAnimators[index], param);
    if (!r) return false;
    history_.before_mutation(*comp, project_->timeline().current(), "keyframe do animador");
    modelRevision_.fetch_add(1, std::memory_order_acq_rel);
    const FrameIndex local = l->local_time(playback_.current());
    Track& tr = l->tracks.get_or_create(TrackProperty::LayerAnimParam, index, param);
    const u32 k = tr.find_exact(local);
    if (k != kInvalidIndex) {
        // Tirar o último keyframe devolve o valor parado (o do instante).
        if (tr.keys.size() == 1) *r = tr.keys[0].value;
        (void)tr.remove(local);
    } else {
        (void)tr.set(local, tr.keys.empty() ? *r : tr.sample_keys(local),
                     param == layeranim::kProgress ? Interpolation::Linear : Interpolation::Bezier);
    }
    project_->mark_dirty();
    request_render();
    return true;
}

u32 Engine::copy_layer_animators(u64 layerId) noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    Composition* comp = project_ ? current_composition() : nullptr;
    const Layer* l = comp ? comp->layer(LayerId::unpack(layerId)) : nullptr;
    if (!l || l->layerAnimators.empty()) return 0;
    clipboard_.layerAnimators = l->layerAnimators;
    clipboard_.layerAnimTracks.clear();
    // Tempos relativos ao começo da camada: colar noutra camada começa no começo dela.
    const i64 local0 = l->local_time(l->start).value;
    for (u32 i = 0; i < l->tracks.size(); ++i) {
        const Track& t = l->tracks.at(i);
        if (t.property != TrackProperty::LayerAnimParam || t.effectIndex >= l->layerAnimators.size()) continue;
        Track c = t;
        for (Keyframe& k : c.keys) k.time = FrameIndex{k.time.value - local0};
        c.lastIndex = 0;
        clipboard_.layerAnimTracks.push_back(std::move(c));
    }
    return static_cast<u32>(clipboard_.layerAnimators.size());
}

u32 Engine::layer_animator_clipboard() noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    return static_cast<u32>(clipboard_.layerAnimators.size());
}

u32 Engine::paste_layer_animators(const u64* ids, u32 count) noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    Composition* comp = project_ ? current_composition() : nullptr;
    if (!comp || !ids || count == 0 || clipboard_.layerAnimators.empty()) return 0;
    bool captured = false;
    u32 pasted = 0;
    for (u32 n = 0; n < count; ++n) {
        Layer* l = comp->layer(LayerId::unpack(ids[n]));
        if (!l || l->kind == LayerKind::Camera || l->kind == LayerKind::Light) continue;
        const u32 base = static_cast<u32>(l->layerAnimators.size());
        if (base + clipboard_.layerAnimators.size() > kMaxLayerAnimators) continue;
        if (!captured) {
            history_.before_mutation(*comp, project_->timeline().current(), "colar animadores");
            modelRevision_.fetch_add(1, std::memory_order_acq_rel);
            captured = true;
        }
        const i64 local0 = l->local_time(l->start).value;
        for (LayerAnimator a : clipboard_.layerAnimators) {
            // Letra/palavra/linha só existem no texto; noutra camada vira a camada inteira.
            if (l->kind != LayerKind::Text) a.unit = 0;
            l->layerAnimators.push_back(std::move(a));
        }
        l->tracks.remove_if([&](const Track& t) { return t.property == TrackProperty::LayerAnimParam && t.effectIndex >= base; });
        for (const Track& src : clipboard_.layerAnimTracks) {
            Track t = src;
            t.effectIndex += base;
            for (Keyframe& k : t.keys) k.time = FrameIndex{k.time.value + local0};
            l->tracks.add(std::move(t));
        }
        ++pasted;
    }
    if (pasted) {
        project_->mark_dirty();
        request_render();
    }
    return pasted;
}

// =============================================================================
// Desfoque de movimento por camada
// =============================================================================

bool Engine::set_layer_motion_blur_length(u64 layerId, f32 factor) noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    Composition* comp = project_ ? current_composition() : nullptr;
    Layer* l = comp ? comp->layer(LayerId::unpack(layerId)) : nullptr;
    if (!l || !std::isfinite(factor)) return false;
    factor = std::clamp(factor, 0.0f, 4.0f);
    if (l->transform.motionBlurAmount == factor) return true;
    history_.before_mutation(*comp, project_->timeline().current(), "comprimento do desfoque");
    modelRevision_.fetch_add(1, std::memory_order_acq_rel);
    l->transform.motionBlurAmount = factor;
    project_->mark_dirty();
    request_render();
    return true;
}

f32 Engine::query_layer_motion_blur_length(u64 layerId) noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    Composition* comp = project_ ? current_composition() : nullptr;
    const Layer* l = comp ? comp->layer(LayerId::unpack(layerId)) : nullptr;
    return l ? l->transform.motionBlurAmount : 1.0f;
}

// =============================================================================
// Escopo: ajuste e grupo
// =============================================================================

bool Engine::set_adjustment_scope(u64 layerId, u32 scope) noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    Composition* comp = project_ ? current_composition() : nullptr;
    Layer* l = comp ? comp->layer(LayerId::unpack(layerId)) : nullptr;
    if (!l || scope > 2) return false;
    if (l->adjustmentScope == scope) return true;
    history_.before_mutation(*comp, project_->timeline().current(), "camadas afetadas");
    modelRevision_.fetch_add(1, std::memory_order_acq_rel);
    l->adjustmentScope = static_cast<u8>(scope);
    project_->mark_dirty();
    request_render();
    return true;
}

u32 Engine::query_adjustment_scope(u64 layerId) noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    Composition* comp = project_ ? current_composition() : nullptr;
    const Layer* l = comp ? comp->layer(LayerId::unpack(layerId)) : nullptr;
    return l ? l->adjustmentScope : 0u;
}

bool Engine::set_adjustment_target(u64 layerId, u64 targetId, bool on) noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    Composition* comp = project_ ? current_composition() : nullptr;
    const LayerId id = LayerId::unpack(layerId), target = LayerId::unpack(targetId);
    Layer* l = comp ? comp->layer(id) : nullptr;
    if (!l || id == target || !comp->layer(target)) return false;
    auto& list = l->adjustmentTargets;
    const auto it = std::find(list.begin(), list.end(), target);
    if ((it != list.end()) == on && l->adjustmentScope == 2) return true;
    if (on && it == list.end() && list.size() >= 256) return false;
    history_.before_mutation(*comp, project_->timeline().current(), "camadas afetadas");
    modelRevision_.fetch_add(1, std::memory_order_acq_rel);
    if (on && it == list.end()) list.push_back(target);
    if (!on && it != list.end()) list.erase(it);
    l->adjustmentScope = 2;
    project_->mark_dirty();
    request_render();
    return true;
}

u32 Engine::query_adjustment_targets(u64 layerId, u64* out, u32 capacity) noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    Composition* comp = project_ ? current_composition() : nullptr;
    const Layer* l = comp ? comp->layer(LayerId::unpack(layerId)) : nullptr;
    if (!l) return 0;
    u32 n = 0;
    for (LayerId t : l->adjustmentTargets) {
        if (!comp->layer(t)) continue;   // apagada: some da lista mostrada
        if (out && n < capacity) out[n] = t.pack();
        ++n;
    }
    return out ? std::min(n, capacity) : n;
}

// =============================================================================
// Presets de efeito (Effect::presets)
// =============================================================================

std::vector<std::pair<std::string, std::string>> Engine::effect_presets(u32 typeId) noexcept {
    std::vector<std::pair<std::string, std::string>> out;
    const Effect* fx = effectRegistry_.find(EffectTypeId{typeId});
    if (!fx) return out;
    for (const EffectPreset& p : fx->presets()) out.emplace_back(p.id ? p.id : "", p.name ? p.name : "");
    return out;
}

bool Engine::apply_effect_preset(u64 layerId, u32 effectId, u32 preset) noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    Composition* comp = project_ ? current_composition() : nullptr;
    Layer* l = comp ? comp->layer(LayerId::unpack(layerId)) : nullptr;
    EffectInstance* e = l ? l->find_effect(EffectId{effectId, 0}) : nullptr;
    const Effect* fx = e ? effectRegistry_.find(e->type) : nullptr;
    const ParameterRegistry* specs = e ? effectRegistry_.params(e->type) : nullptr;
    if (!fx || !specs) return false;
    const std::span<const EffectPreset> all = fx->presets();
    if (preset >= all.size()) return false;
    history_.before_mutation(*comp, project_->timeline().current(), "preset do efeito");
    modelRevision_.fetch_add(1, std::memory_order_acq_rel);
    for (const EffectPresetValue& v : all[preset].values) {
        if (v.param >= e->params.size() || v.param >= specs->count() || !std::isfinite(v.value)) continue;
        const ParamSpec& spec = specs->at(v.param);
        ParamValue& dst = e->params[v.param].constant;
        switch (spec.type) {
            case ParamType::Bool: dst = ParamValue::boolean(v.value >= 0.5f); break;
            case ParamType::Int: case ParamType::Enum: dst.v[0] = std::round(v.value); break;
            default: dst.v[0] = v.value; break;
        }
    }
    project_->mark_dirty();
    request_render();
    return true;
}

bool Engine::set_group_camera_pass_through(u64 layerId, bool on) noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    Composition* comp = project_ ? current_composition() : nullptr;
    Layer* l = comp ? comp->layer(LayerId::unpack(layerId)) : nullptr;
    if (!l || l->kind != LayerKind::Composition) return false;
    if (l->nested.cameraPassThrough == on) return true;
    history_.before_mutation(*comp, project_->timeline().current(), "câmera no grupo");
    modelRevision_.fetch_add(1, std::memory_order_acq_rel);
    l->nested.cameraPassThrough = on;
    project_->mark_dirty();
    request_render();
    return true;
}

i32 Engine::query_group_camera_pass_through(u64 layerId) noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    Composition* comp = project_ ? current_composition() : nullptr;
    const Layer* l = comp ? comp->layer(LayerId::unpack(layerId)) : nullptr;
    if (!l || l->kind != LayerKind::Composition) return -1;
    return l->nested.cameraPassThrough ? 1 : 0;
}

Result<u32> Engine::add_layers_to_group(const u64* ids, u32 count, u64 groupLayerId, std::string* why) noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    auto refuse = [&](Errc code, const char* reason) {
        if (why) *why = reason;
        return Status{code, reason};
    };
    Composition* comp = project_ ? current_composition() : nullptr;
    if (!comp || !ids || count == 0) return refuse(Errc::InvalidArgument, "nada para agrupar");
    Timeline& tl = project_->timeline();
    const CompositionId parentId = tl.current();
    const LayerId gid = LayerId::unpack(groupLayerId);
    const Layer* G = comp->layer(gid);
    if (!G || G->kind != LayerKind::Composition) return refuse(Errc::InvalidArgument, "nao e um grupo");
    const CompositionId childId = G->nested.composition;
    const Composition* C = tl.composition(childId);
    if (!C) return refuse(Errc::NotFound, "composicao do grupo ausente");
    if (time_altered(*G)) return refuse(Errc::NotSupported, "o tempo do grupo foi alterado");
    if (C->fps() != comp->fps()) return refuse(Errc::NotSupported, "o grupo tem outra taxa de quadros");
    // Na ordem vertical (de baixo para cima), sem o próprio grupo.
    std::vector<LayerId> moving;
    for (u32 i = 0; i < comp->order().size(); ++i) {
        const LayerId id = comp->order().at(i);
        if (id == gid) continue;
        for (u32 k = 0; k < count; ++k) {
            if (ids[k] != id.pack()) continue;
            moving.push_back(id);
            break;
        }
    }
    if (moving.empty()) return refuse(Errc::NotFound, "camadas nao encontradas");
    auto moves = [&](LayerId id) { return std::find(moving.begin(), moving.end(), id) != moving.end(); };
    for (LayerId id : moving) {
        const Layer* src = comp->layer(id);
        if (!src) continue;
        if (src->kind == LayerKind::Composition && src->nested.composition == childId)
            return refuse(Errc::NotSupported, "um grupo nao entra nele mesmo");
        if (src->parent.valid() && src->parent != gid && !moves(src->parent))
            return refuse(Errc::NotSupported, "a camada tem pai fora do grupo");
    }
    for (u32 i = 0; i < comp->order().size(); ++i) {
        const Layer* x = comp->layer(comp->order().at(i));
        if (x && !moves(comp->order().at(i)) && x->parent.valid() && moves(x->parent))
            return refuse(Errc::NotSupported, "outra camada tem esta como pai");
    }

    history_.before_mutation(*comp, parentId, "adicionar ao grupo");
    modelRevision_.fetch_add(1, std::memory_order_acq_rel);
    // Quadro c de dentro aparece no quadro c + shift de fora.
    const i64 shift = G->start.value - G->offset.value;
    // O grupo passa a mostrar uma CÓPIA da composição com as camadas novas: o
    // desfazer (que guarda só a composição de fora) volta a apontar para a
    // original, intacta.
    const std::string childName = C->name();
    const CompositionId nid = tl.create_composition(childName, C->width(), C->height(), C->fps());
    Composition* NC = tl.composition(nid);
    comp = tl.composition(parentId);   // a criação pode ter realocado
    C = tl.composition(childId);
    if (!NC || !comp || !C) return refuse(Errc::OutOfMemory, "composicao nao criada");
    NC->restore_from(*C);
    NC->set_name(childName);
    std::vector<std::pair<LayerId, LayerId>> map;
    i64 lo = std::numeric_limits<i64>::max(), hi = std::numeric_limits<i64>::min();
    for (LayerId id : moving) {
        const Layer* src = comp->layer(id);
        if (!src) continue;
        const LayerId inner = NC->add_layer(src->kind, src->name);
        Layer* dst = NC->layer(inner);
        if (!dst) continue;
        *dst = *src;
        dst->start = FrameIndex{src->start.value - shift};
        dst->end = FrameIndex{src->end.value - shift};
        if (dst->matteSource.valid() && !moves(dst->matteSource)) { dst->matteSource = LayerId{}; dst->matteMode = MatteMode::None; }
        lo = std::min(lo, src->start.value);
        hi = std::max(hi, src->end.value);
        map.emplace_back(id, inner);
    }
    for (auto& [oldId, inner] : map) {
        Layer* dst = NC->layer(inner);
        if (!dst) continue;
        LayerId np{};
        for (auto& [o, nn] : map) if (o == dst->parent) np = nn;
        dst->parent = np;   // pai = o grupo (ou nenhum): já é o espaço de dentro
        if (dst->matteSource.valid()) {
            LayerId nm{};
            for (auto& [o, nn] : map) if (o == dst->matteSource) nm = nn;
            dst->matteSource = nm;
            if (!nm.valid()) dst->matteMode = MatteMode::None;
        }
    }
    if (hi > shift && hi - shift > NC->duration().value) NC->set_duration(FrameIndex{hi - shift});
    NC->rebuild_draw_order();
    for (LayerId id : moving) {
        media_.close_layer(id);
        comp->remove_layer(id);
    }
    if (Layer* g = comp->layer(gid)) {
        g->nested.composition = nid;
        // O grupo cobre o trecho das que entraram (o tempo de dentro não muda).
        if (hi > g->end.value) g->end = FrameIndex{hi};
        if (lo < g->start.value && lo - shift >= 0) {
            g->offset = FrameIndex{g->offset.value - (g->start.value - lo)};
            g->start = FrameIndex{lo};
        }
    }
    comp->rebuild_draw_order();
    selection_.assign(1, gid.pack());
    project_->mark_dirty();
    request_render();
    return static_cast<u32>(map.size());
}

Result<u64> Engine::remove_layer_from_group(u64 layerId, std::string* why) noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    auto refuse = [&](Errc code, const char* reason) {
        if (why) *why = reason;
        return Status{code, reason};
    };
    if (!project_ || compStack_.empty()) return refuse(Errc::InvalidArgument, "nao esta dentro de um grupo");
    Timeline& tl = project_->timeline();
    const CompositionId childId = tl.current();
    const CompositionId parentId = compStack_.back();
    const Composition* C = tl.composition(childId);
    Composition* P = tl.composition(parentId);
    const LayerId lid = LayerId::unpack(layerId);
    const Layer* L = C ? C->layer(lid) : nullptr;
    if (!L || !P) return refuse(Errc::NotFound, "camada nao encontrada");
    LayerId gid{};
    for (u32 i = 0; i < P->order().size() && !gid.valid(); ++i) {
        const Layer* x = P->layer(P->order().at(i));
        if (x && x->kind == LayerKind::Composition && x->nested.composition == childId) gid = P->order().at(i);
    }
    const Layer* G = gid.valid() ? P->layer(gid) : nullptr;
    if (!G) return refuse(Errc::NotFound, "grupo nao encontrado");
    if (time_altered(*G)) return refuse(Errc::NotSupported, "o tempo do grupo foi alterado");
    if (C->fps() != P->fps()) return refuse(Errc::NotSupported, "o grupo tem outra taxa de quadros");
    if (L->parent.valid()) return refuse(Errc::NotSupported, "a camada tem pai dentro do grupo");
    if (L->matteSource.valid()) return refuse(Errc::NotSupported, "a camada usa uma matte do grupo");
    for (u32 i = 0; i < C->order().size(); ++i) {
        const Layer* x = C->layer(C->order().at(i));
        if (x && C->order().at(i) != lid && (x->parent == lid || x->matteSource == lid))
            return refuse(Errc::NotSupported, "outra camada do grupo depende desta");
    }

    history_.before_mutation(*P, parentId, "tirar do grupo");
    modelRevision_.fetch_add(1, std::memory_order_acq_rel);
    const i64 shift = G->start.value - G->offset.value;
    // O grupo transformado leva a camada junto como pai (a tela não muda).
    bool identity = !G->tracks.has_animation() && !G->parent.valid();
    if (identity) {
        const Mat4 m = layer_world_matrix(*P, *G, G->start);
        const Mat4 I = Mat4::identity();
        for (int c = 0; c < 4 && identity; ++c)
            identity = std::fabs(m.col[c].x - I.col[c].x) < 1e-4f && std::fabs(m.col[c].y - I.col[c].y) < 1e-4f
                    && std::fabs(m.col[c].z - I.col[c].z) < 1e-4f && std::fabs(m.col[c].w - I.col[c].w) < 1e-4f;
    }
    const std::string childName = C->name();
    const CompositionId nid = tl.create_composition(childName, C->width(), C->height(), C->fps());
    Composition* NC = tl.composition(nid);
    P = tl.composition(parentId);
    C = tl.composition(childId);
    if (!NC || !P || !C) return refuse(Errc::OutOfMemory, "composicao nao criada");
    NC->restore_from(*C);
    NC->set_name(childName);
    const Layer moved = *C->layer(lid);
    NC->remove_layer(lid);
    NC->rebuild_draw_order();
    const LayerId out = P->add_layer(moved.kind, moved.name);
    Layer* dst = P->layer(out);
    if (!dst) return refuse(Errc::OutOfMemory, "camada nao criada");
    *dst = moved;
    dst->start = FrameIndex{moved.start.value + shift};
    dst->end = FrameIndex{moved.end.value + shift};
    dst->parent = identity ? LayerId{} : gid;
    u32 at = 0;
    for (u32 i = 0; i < P->order().size(); ++i) if (P->order().at(i) == gid) at = i + 1;
    if (Layer* g = P->layer(gid)) g->nested.composition = nid;
    (void)P->reorder_layer(out, at);
    P->rebuild_draw_order();
    // A timeline volta para fora, com a camada escolhida no lugar novo.
    const i64 outer = playback_.current().value + shift;
    compStack_.pop_back();
    tl.set_current(parentId);
    playback_.configure(P->fps(), P->duration());
    playback_.seek(FrameIndex{std::clamp<i64>(outer, 0, std::max<i64>(0, P->duration().value - 1))}, monotonic_ns());
    selection_.assign(1, out.pack());
    project_->mark_dirty();
    request_render();
    return out.pack();
}

} // namespace aurea
