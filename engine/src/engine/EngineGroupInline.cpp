// =============================================================================
//  Aurea / engine / EngineGroupInline.cpp
//
//  0.5.3 — pedido da comunidade: "Please let us expand group, seeing all
//  contents of it without ungrouping!!". A timeline (Android e iOS) abre o
//  grupo NO LUGAR: as camadas de dentro aparecem recuadas embaixo dele e dá
//  para escolher, mover e aparar sem entrar no grupo (open_precomp) e sem
//  desagrupar. A composição aberta não muda; cada edição entra na composição de
//  dentro com o modelo travado (a prévia nunca vê a troca) e vira um passo de
//  desfazer DELA (o histórico guarda a composição pelo id).
//
//  Tempo: o grupo mostra a composição de dentro a partir de `offset`, começando
//  em `start` — de dentro para fora, `fora = dentro + start − offset`. Com curva
//  de tempo, velocidade ≠ 1, reverso ou outro fps a conta não é 1:1: as linhas
//  aparecem (no lugar aproximado) mas não são editáveis no lugar.
// =============================================================================
#include "aurea/Engine.hpp"
#include "aurea/bridge/BridgePods.hpp"
#include "aurea/core/Time.hpp"
#include "aurea/project/Project.hpp"
#include "aurea/timeline/Composition.hpp"
#include "aurea/timeline/Layer.hpp"
#include "aurea/timeline/PrecompControls.hpp"
#include "aurea/timeline/Timeline.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace aurea {

std::vector<u64> Engine::route_to_layer(u64 compId, u64 layerId) noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    if (!project_) return {};
    auto& timeline = project_->timeline();
    const auto target = CompositionId::unpack(compId);
    const auto layer = LayerId::unpack(layerId);
    const auto* dest = timeline.composition(target);
    if (!dest || !dest->layer(layer)) return {};
    struct Step { CompositionId comp; std::vector<u64> groups; };
    std::vector<Step> pending{{timeline.current(), {}}};
    std::vector<CompositionId> seen{timeline.current()};
    for (usize i = 0; i < pending.size(); ++i) {
        const Step step = pending[i];
        if (step.comp == target) {
            auto path = step.groups;
            path.push_back(layerId);
            return path;
        }
        const auto* comp = timeline.composition(step.comp);
        if (!comp || step.groups.size() >= 8) continue;
        comp->layers().for_each([&](LayerId id, const Layer& row) {
            if (row.kind != LayerKind::Composition || !row.nested.composition.valid() ||
                std::find(seen.begin(), seen.end(), row.nested.composition) != seen.end()) return;
            seen.push_back(row.nested.composition);
            auto route = step.groups;
            route.push_back(id.pack());
            pending.push_back({row.nested.composition, std::move(route)});
        });
    }
    return {};
}

std::vector<Engine::PrecompControlRow> Engine::precomp_controls(u64 groupId) noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    const Composition* inner = group_inner_locked(groupId, nullptr, nullptr);
    if (!inner) return {};
    const auto& edits = current_composition()->layer(LayerId::unpack(groupId))->nested.overrides;
    std::vector<PrecompControlRow> rows;
    const auto& order = inner->order();
    for (u32 index = 0; index < order.size(); ++index) {
        const LayerId id = order.at(index);
        const Layer* child = inner->layer(id);
        if (!child) continue;
        const u8 available = available_precomp_controls(*child);
        for (auto kind : {PrecompControl::Text, PrecompControl::Color, PrecompControl::Opacity}) {
            const u8 bit = precomp_control_bit(kind);
            if (!(available & bit) && !(child->precompExposed & bit)) continue;
            const auto it = std::find_if(edits.begin(), edits.end(), [&](const PrecompControlValue& edit) {
                return edit.target == id && edit.kind == kind;
            });
            PrecompControlRow row;
            row.value.target = id;
            row.value.kind = kind;
            row.name = child->name;
            row.exposed = (child->precompExposed & bit) != 0;
            row.available = (available & bit) != 0;
            row.overridden = it != edits.end();
            if (row.overridden) row.value = *it;
            else if (kind == PrecompControl::Text) row.value.text = child->text.content;
            else if (kind == PrecompControl::Color) row.value.rgba = child->shape.fillColor;
            else row.value.rgba.x = child->transform.opacity;
            rows.push_back(std::move(row));
        }
    }
    for (const auto& edit : edits) {
        if (inner->layer(edit.target)) continue;
        rows.push_back(PrecompControlRow{edit, {}, true, true, false});
    }
    return rows;
}

bool Engine::edit_precomp_control(u64 groupId, u64 targetId, u32 type, u32 operation,
                                  const std::string& text, Vec4 rgba) noexcept {
    if (type > static_cast<u32>(PrecompControl::Opacity) || operation > 3) return false;
    std::lock_guard<std::mutex> lock(modelMutex_);
    drain_commands_locked();
    Composition* inner = group_inner_locked(groupId, nullptr, nullptr);
    if (!inner) return false;
    Composition* outer = current_composition();
    Layer* group = outer->layer(LayerId::unpack(groupId));
    if (group->locked) return false;
    const LayerId target = LayerId::unpack(targetId);
    Layer* child = inner->layer(target);
    const auto kind = static_cast<PrecompControl>(type);
    const u8 bit = precomp_control_bit(kind);
    if (operation <= 1) {
        if (!child || child->locked || (operation == 0 && !(available_precomp_controls(*child) & bit))) return false;
        const u8 mask = operation == 0 ? (child->precompExposed | bit) : (child->precompExposed & ~bit);
        if (mask == child->precompExposed) return true;
        history_.before_mutation(*inner, inner->id(), "controle da composição");
        child->precompExposed = mask;
        inner->touch();
    } else {
        auto& edits = group->nested.overrides;
        auto it = std::find_if(edits.begin(), edits.end(), [&](const PrecompControlValue& edit) {
            return edit.target == target && edit.kind == kind;
        });
        if (operation == 3) {
            if (it == edits.end()) return true;
            history_.before_mutation(*outer, outer->id(), "controle da instância");
            edits.erase(it);
        } else {
            if (!child || !(child->precompExposed & available_precomp_controls(*child) & bit) ||
                (it == edits.end() && edits.size() >= kMaxPrecompControls)) return false;
            if (kind == PrecompControl::Text && text.size() > kMaxPrecompText) return false;
            for (f32* component : {&rgba.x, &rgba.y, &rgba.z, &rgba.w}) {
                if (!std::isfinite(*component)) return false;
                *component = std::clamp(*component, 0.f, 1.f);
            }
            const PrecompControlValue edit{target, kind, kind == PrecompControl::Text ? text : std::string{}, rgba};
            if (it != edits.end() && it->text == edit.text && it->rgba == edit.rgba) return true;
            history_.before_mutation(*outer, outer->id(), "controle da instância");
            if (it == edits.end()) edits.push_back(edit); else *it = edit;
        }
    }
    outer->touch();
    bump_model_revision_locked();
    project_->mark_dirty();
    request_render();
    return true;
}

bool Engine::set_inline_edit_target(u64 groupLayerId, u64 childLayerId) noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    std::lock_guard<std::mutex> submitLock(commandSubmitMutex_);
    // Drain before changing inspector focus. Published commands retain their
    // own scope, including when the finger selects another row immediately.
    drain_commands_locked();
    inlineEditGroup_.store(0, std::memory_order_release);
    inlineEditLayer_.store(0, std::memory_order_release);
    inlineEditOuter_.store(0, std::memory_order_release);
    inlineEditInner_.store(0, std::memory_order_release);
    if (!groupLayerId && !childLayerId) return true;
    Composition* inner = group_inner_locked(groupLayerId, nullptr, nullptr);
    const Composition* outer = current_composition();
    const Layer* group = outer ? outer->layer(LayerId::unpack(groupLayerId)) : nullptr;
    const Layer* child = inner ? inner->layer(LayerId::unpack(childLayerId)) : nullptr;
    if (!group || !child) return false;
    inlineEditOuter_.store(project_->timeline().current().pack(), std::memory_order_relaxed);
    inlineEditLayer_.store(childLayerId, std::memory_order_relaxed);
    inlineEditInner_.store(group->nested.composition.pack(), std::memory_order_relaxed);
    inlineEditGroup_.store(groupLayerId, std::memory_order_release);
    return true;
}

Engine::InlineEditScope::InlineEditScope(Engine& engine, u64 layerId, bool mutation) noexcept : engine_(engine), mutation_(mutation) {
    if (engine.inlineSampleActive_ || engine.inlineAccessRejected_) return;
    if (layerId && layerId == engine.inlineEditLayer_.load(std::memory_order_relaxed))
        enter(engine.inlineEditOuter_.load(std::memory_order_relaxed), engine.inlineEditGroup_.load(std::memory_order_acquire),
            engine.inlineEditInner_.load(std::memory_order_relaxed));
}
Engine::InlineEditScope::InlineEditScope(Engine& engine, u64 outer, u64 group, bool mutation, u64 expectedInner) noexcept
    : engine_(engine), mutation_(mutation) { enter(outer, group, expectedInner); }

void Engine::InlineEditScope::enter(u64 outerId, u64 groupId, u64 expectedInner) noexcept {
    if (!groupId || !outerId || !engine_.project_) return;
    auto& timeline = engine_.project_->timeline();
    outer_ = timeline.current(); outerTime_ = engine_.playback_.current(); previousTime_ = engine_.inlineSampleTime_;
    previousActive_ = engine_.inlineSampleActive_;
    previousRejected_ = engine_.inlineAccessRejected_;
    restore_ = true;
    const auto reject = [&] { engine_.inlineAccessRejected_ = true; };
    if (timeline.current().pack() != outerId) { reject(); return; }
    const Composition* outer = timeline.composition(timeline.current());
    const Layer* group = outer ? outer->layer(LayerId::unpack(groupId)) : nullptr;
    Composition* inner = engine_.group_inner_locked(groupId, nullptr, nullptr);
    if (!inner || !group || (expectedInner && group->nested.composition.pack() != expectedInner)) { reject(); return; }
    const f64 source = group->source_frame(engine_.playback_.current()) * inner->fps() / outer->fps();
    if (!std::isfinite(source) || std::fabs(source) >= f64(i64{1} << 40)) { reject(); return; }
    engine_.inlineSampleTime_ = FrameIndex{static_cast<i64>(std::floor(source + 1e-6))};
    engine_.inlineSampleActive_ = true;
    engine_.inlineAccessRejected_ = false;
    timeLayer_ = group;
    timeRatio_ = inner->fps() / outer->fps();
    if (mutation_) previousSelection_ = engine_.selection_;
    timeline.set_current(group->nested.composition);
    active_ = true;
}

Engine::InlineEditScope::~InlineEditScope() {
    if (!restore_ || !engine_.project_) return;
    auto& timeline = engine_.project_->timeline();
    timeline.set_current(outer_);
    engine_.inlineSampleTime_ = previousTime_;
    engine_.inlineSampleActive_ = previousActive_;
    engine_.inlineAccessRejected_ = previousRejected_;
    if (mutation_ && active_) {
        engine_.selection_ = std::move(previousSelection_);
        engine_.configure_playback_locked(timeline.composition(outer_));
        if (engine_.playback_.current() != outerTime_) engine_.playback_.seek(outerTime_, monotonic_ns());
        timeline.set_playhead(engine_.playback_.current());
    }
}

i64 Engine::InlineEditScope::map_frame(i64 outerFrame) const noexcept {
    if (!active_ || !timeLayer_) return outerFrame;
    const f64 value = timeLayer_->source_frame(FrameIndex{outerFrame}) * timeRatio_;
    return std::isfinite(value) ? static_cast<i64>(std::floor(std::clamp(value, -f64(i64{1} << 40), f64(i64{1} << 40)) + 1e-6)) : 0;
}

FrameIndex Engine::editing_frame_locked() const noexcept {
    return inlineSampleActive_ ? inlineSampleTime_ : playback_.current();
}

Composition* Engine::group_inner_locked(u64 groupLayerId, i64* delta, bool* linear) noexcept {
    if (!project_ || groupLayerId == 0) return nullptr;
    Timeline& tl = project_->timeline();
    Composition* outer = tl.composition(tl.current());
    const Layer* g = outer ? outer->layer(LayerId::unpack(groupLayerId)) : nullptr;
    if (!g || g->kind != LayerKind::Composition) return nullptr;
    Composition* inner = tl.composition(g->nested.composition);
    if (!inner || inner == outer) return nullptr;
    if (delta) *delta = g->start.value - g->offset.value;
    if (linear) {
        const bool remap = g->timeRemapEnabled && !g->timeRemap.keys.empty();
        *linear = !remap && g->speed_track() == nullptr && g->speed == 1.0f && !g->reversed
            && std::fabs(inner->fps() - outer->fps()) < 1e-6;
    }
    return inner;
}

u32 Engine::query_group_layers(u64 groupLayerId, bridge::LayerRow* out, u32 capacity,
                               char* outNameBlob, u32 nameBlobCapacity) noexcept {
    if (!out || capacity == 0) return 0;
    std::lock_guard<std::mutex> lock(modelMutex_);
    i64 delta = 0;
    bool linear = false;
    const Composition* comp = group_inner_locked(groupLayerId, &delta, &linear);
    if (!comp) return 0;
    constexpr i64 lo = -(i64{1} << 31) + 1, hi = (i64{1} << 31) - 1;
    const auto clamp32 = [&](i64 v) { return static_cast<i32>(std::clamp(v, lo, hi)); };
    u32 written = 0, nameCursor = 0;
    const u32 n = comp->order().size();
    for (u32 i = 0; i < n && written < capacity; ++i) {
        const LayerId id = comp->order().at(n - 1 - i);
        const Layer* l = comp->layer(id);
        if (!l) continue;
        bridge::LayerRow row;
        row.id = id.pack();
        row.kind = static_cast<u32>(l->kind);
        row.zIndex = i;
        row.startFrame = clamp32(l->start.value + delta);
        row.endFrame = clamp32(l->end.value + delta);
        // Keyframes e miniaturas: `t_fora = local + start_fora − offset` — o
        // offset da camada continua o dela (é o tempo LOCAL dela).
        row.offsetFrames = static_cast<i32>(l->offset.value);
        row.trackId = l->trackId;
        row.opacity = l->transform.opacity;
        row.effectCount = static_cast<u32>(l->effects.size());
        row.maskCount = static_cast<u32>(l->masks.size());
        u32 keyCount = static_cast<u32>(l->timeRemap.keys.size());
        for (u32 t = 0; t < l->tracks.size(); ++t) keyCount += static_cast<u32>(l->tracks.at(t).keys.size());
        row.keyframeCount = keyCount;
        row.blendMode = static_cast<u32>(l->blendMode);
        u32 flags = 0;
        if (l->visible) flags |= bridge::kLayerRowFlagVisible;
        if (l->locked)  flags |= bridge::kLayerRowFlagLocked;
        if (l->solo)    flags |= bridge::kLayerRowFlagSolo;
        if (l->animated()) flags |= bridge::kLayerRowFlagAnimated;
        if (l->threeD)  flags |= bridge::kLayerRowFlagThreeD;
        if (l->adjustment) flags |= bridge::kLayerRowFlagAdjustment;
        if (l->guide)   flags |= bridge::kLayerRowFlagGuide;
        if (l->shy)     flags |= bridge::kLayerRowFlagShy;
        flags |= (static_cast<u32>(l->label) << bridge::kLayerRowLabelShift) & bridge::kLayerRowLabelMask;
        row.flags = flags;
        row.parentIndex = kInvalidIndex;
        row.reserved0 = linear ? kGroupRowEditable : 0u;
        if (outNameBlob && l->name.size() <= nameBlobCapacity - nameCursor) {
            std::memcpy(outNameBlob + nameCursor, l->name.data(), l->name.size());
            row.nameOffset = nameCursor;
            row.nameLength = static_cast<u32>(l->name.size());
            nameCursor += static_cast<u32>(l->name.size());
        } else if (outNameBlob) {
            row.reserved0 |= bridge::kLayerRowNameBufferFull;
        }
        out[written++] = row;
    }
    return written;
}

Status Engine::set_group_layer_ranges(u64 groupLayerId, const u64* layerIds, const i64* starts,
                                      const i64* ends, u32 count) noexcept {
    constexpr i64 limit = i64{1} << 31;
    if (!layerIds || !starts || !ends || count == 0 || count > 4096)
        return Status{Errc::InvalidArgument, "faixas invalidas"};
    std::lock_guard<std::mutex> lock(modelMutex_);
    drain_commands_locked();
    if (!project_) return Errc::InvalidState;
    i64 delta = 0;
    bool linear = false;
    Composition* inner = group_inner_locked(groupLayerId, &delta, &linear);
    if (!inner) return Status{Errc::NotFound, "nao e um grupo da composicao aberta"};
    if (!linear) return Status{Errc::NotSupported, "o tempo do grupo nao anda 1:1 (curva, velocidade ou reverso)"};
    for (u32 i = 0; i < count; ++i) {
        const i64 s = starts[i] - delta, e = ends[i] - delta;
        if (e <= s || s < 0 || e >= limit) return Status{Errc::OutOfRange, "fora do tempo do grupo"};
    }
    Timeline& tl = project_->timeline();
    const CompositionId outerId = tl.current();
    const Layer* g = tl.composition(outerId)->layer(LayerId::unpack(groupLayerId));
    const CompositionId innerId = g->nested.composition;
    const FrameIndex head = playback_.current();
    history_.begin_group("mover dentro do grupo");
    tl.set_current(innerId);
    u32 applied = 0;
    Status last = OkStatus;
    for (u32 i = 0; i < count; ++i) {
        const LayerId id = LayerId::unpack(layerIds[i]);
        const Layer* l = inner->layer(id);
        if (!l) { last = Errc::NotFound; continue; }
        if (l->locked) { last = Status{Errc::InvalidState, "camada travada"}; continue; }
        if (l->start.value == starts[i] - delta && l->end.value == ends[i] - delta) { ++applied; continue; }
        Command c;
        c.type = CommandType::LayerSetTimeRange;
        c.layer_range.layer = id;
        c.layer_range.start = FrameIndex{starts[i] - delta};
        c.layer_range.end = FrameIndex{ends[i] - delta};
        const Status st = apply_command_internal(c, nullptr, true);
        if (st.ok()) ++applied;
        else last = st;
    }
    // De volta à composição aberta: a reprodução (duração, área) e o cabeçote
    // são os dela, mesmo que a de dentro tenha crescido.
    tl.set_current(outerId);
    history_.end_group();
    configure_playback_locked(tl.composition(outerId));
    if (playback_.current() != head) playback_.seek(head, monotonic_ns());
    if (applied) {
        bump_model_revision_locked();
        project_->mark_dirty();
    }
    request_render();
    return applied ? OkStatus : last;
}

bool Engine::edit_group_clip_time(u64 groupLayerId, u64 layerId, u32 operation, i64 frame) noexcept {
    constexpr i64 limit = i64{1} << 31;
    // Só as alças e o mover (rolar/deslizar pedem vizinhos de dentro; a UI não oferece).
    if (operation != 0 && operation != 1 && operation != 6) return false;
    if (frame <= -limit || frame >= limit) return false;
    std::lock_guard<std::mutex> lock(modelMutex_);
    drain_commands_locked();
    if (!project_) return false;
    i64 delta = 0;
    bool linear = false;
    if (!group_inner_locked(groupLayerId, &delta, &linear) || !linear) return false;
    Timeline& tl = project_->timeline();
    const CompositionId outerId = tl.current();
    const CompositionId innerId = tl.composition(outerId)->layer(LayerId::unpack(groupLayerId))->nested.composition;
    const FrameIndex head = playback_.current();
    tl.set_current(innerId);
    const bool ok = edit_clip_time_locked(layerId, operation, frame - delta, 0, 0, false, false, false);
    tl.set_current(outerId);
    configure_playback_locked(tl.composition(outerId));
    if (playback_.current() != head) playback_.seek(head, monotonic_ns());
    request_render();
    return ok;
}

}  // namespace aurea
