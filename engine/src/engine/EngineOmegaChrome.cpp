// =============================================================================
//  Aurea / engine / EngineOmegaChrome.cpp
//
//  Omega 0.3.2, área 02 (moldura do editor): as regras que a moldura da
//  referência pede e o motor ainda não tinha. Android e iOS só chamam; a conta
//  mora aqui, igual para as duas plataformas.
//
//  P0-2 · trilha de grupos: os nomes do caminho aberto, da principal à atual.
//  P0-4 · barra inferior da seleção múltipla: qual variante (cortes, tudo
//         depois do cabeçote, tudo antes).
//  P1-3 · colar estilo por parte (cor, borda e sombra, mesclagem, transformar,
//         texto, volume, efeitos, velocidade), num passo de desfazer.
//  P2-1 · modos de visão da prévia (só as escolhidas, escolhidas a 50 %,
//         pixels reais). Nunca entram no export (`finalQuality`).
//  P2-3 · setas do transporte: o começo, o último quadro e as pontas da camada
//         entram nos eventos (marcadores e keyframes já entravam).
// =============================================================================
#include "aurea/Engine.hpp"
#include "aurea/effects/EffectRegistry.hpp"
#include "aurea/scene3d/PartEffects.hpp"
#include "aurea/text/TextAnimator.hpp"
#include "aurea/text/TextTransform.hpp"
#include "aurea/timeline/Composition.hpp"
#include "aurea/timeline/Layer.hpp"

#include <algorithm>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

namespace aurea {

namespace {

/// Borda e sombra (decisão 05: efeitos existentes com papel de decoração).
bool is_decoration_effect(EffectTypeId type) noexcept {
    static constexpr EffectTypeId kTypes[] = {
        effect_type_id(effect_keys::kStroke), effect_type_id(effect_keys::kStrokeOutline),
        effect_type_id(effect_keys::kDropShadow), effect_type_id(effect_keys::kBorder),
    };
    return std::find(std::begin(kTypes), std::end(kTypes), type) != std::end(kTypes);
}

/// Efeitos que só servem a um tipo de camada (animadores de texto, layout do
/// texto/forma 3D, partes 3D): colam só entre camadas do mesmo tipo.
bool kind_bound_effect(EffectTypeId type) noexcept {
    static constexpr EffectTypeId kTypes[] = {
        effect_type_id(text::kAnimatorEffect), effect_type_id(text::kTransformEffect),
        effect_type_id(effect_keys::kText3DLayout), effect_type_id(effect_keys::kShape3DLayout),
    };
    return std::find(std::begin(kTypes), std::end(kTypes), type) != std::end(kTypes)
        || scene3d::is_part3d_effect(type);
}

bool has_audio(const Layer& l) noexcept {
    return l.kind == LayerKind::Audio || l.kind == LayerKind::Video;
}

bool has_speed(const Layer& l) noexcept {
    return l.kind == LayerKind::Video || l.kind == LayerKind::Audio || l.kind == LayerKind::Composition;
}

/// Keys em tempo LOCAL: levar de uma camada a outra mantém a distância do
/// início (o ponto de entrada `offset`), como o colar estilo de sempre.
void rebase_keys(Track& t, i64 delta) noexcept {
    if (delta == 0) return;
    for (Keyframe& k : t.keys) k.time.value += delta;
    t.lastIndex = 0;
}

/// Troca a trilha `p` (sem efeito) de `dst` pela de `src`, com os keyframes.
void replace_track(Layer& dst, const Layer& src, TrackProperty p) {
    dst.tracks.remove_if([p](const Track& t) { return t.property == p && t.effectIndex == kInvalidIndex; });
    if (const Track* t = src.tracks.find(p)) {
        Track c = *t;
        rebase_keys(c, dst.offset.value - src.offset.value);
        dst.tracks.add(std::move(c));
    }
}

/// Troca, em `dst`, os efeitos que `pick` aceita pelos de `src` que ele aceita
/// (com os keyframes deles, ids novos e a mesma ordem). Devolve se mudou algo.
template <class Pick>
bool replace_effects_where(Layer& dst, const Layer& src, Pick pick) {
    bool changed = false;
    std::vector<u32> removed;
    for (const EffectInstance& e : dst.effects) if (pick(e.type)) removed.push_back(e.id);
    if (!removed.empty()) {
        dst.effects.erase(std::remove_if(dst.effects.begin(), dst.effects.end(),
                                         [&](const EffectInstance& e) { return pick(e.type); }), dst.effects.end());
        dst.tracks.remove_if([&](const Track& t) {
            return t.property == TrackProperty::EffectParam
                && std::find(removed.begin(), removed.end(), t.effectIndex) != removed.end();
        });
        changed = true;
    }
    u32 next = dst.nextEffectId;
    for (const EffectInstance& e : dst.effects) if (e.id != kInvalidIndex) next = std::max(next, e.id + 1);
    const i64 delta = dst.offset.value - src.offset.value;
    for (const EffectInstance& e : src.effects) {
        if (!pick(e.type)) continue;
        if (kind_bound_effect(e.type) && dst.kind != src.kind) continue;
        EffectInstance copy = e;
        copy.id = next++;
        // Nenhuma trilha órfã com este id pode ficar na frente da colada.
        dst.tracks.remove_if([&](const Track& t) { return t.property == TrackProperty::EffectParam && t.effectIndex == copy.id; });
        for (u32 i = 0; i < src.tracks.size(); ++i) {
            const Track& t = src.tracks.at(i);
            if (t.property != TrackProperty::EffectParam || t.effectIndex != e.id) continue;
            Track c = t;
            c.effectIndex = copy.id;
            rebase_keys(c, delta);
            dst.tracks.add(std::move(c));
        }
        dst.effects.push_back(std::move(copy));
        changed = true;
    }
    dst.nextEffectId = std::max(dst.nextEffectId, next);
    return changed;
}

} // namespace

// --- P0-2 · trilha de grupos ----------------------------------------------------

std::string Engine::composition_trail() noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    if (!project_) return {};
    Timeline& tl = project_->timeline();
    std::string out;
    bool first = true;
    auto add = [&](CompositionId id) {
        const Composition* c = tl.composition(id);
        if (!first) out.push_back('\x1F');
        first = false;
        if (c) out += c->name();
    };
    for (const CompositionId id : compStack_) add(id);
    add(tl.current());
    return out;
}

// --- P2-3 · setas do transporte ---------------------------------------------------

bool Engine::query_transport_navigation(u64 layerId, i64 compositionFrame, i32 direction,
                                        i64& targetFrame) noexcept {
    targetFrame = compositionFrame;
    if (direction == 0) return false;
    // Marcadores e keyframes visíveis: a regra de sempre (trava o modelo sozinha).
    i64 base = compositionFrame;
    const bool hasBase = query_timeline_navigation(layerId, compositionFrame, direction, base);
    std::lock_guard<std::mutex> lock(modelMutex_);
    drain_commands_locked();
    const Composition* comp = project_ ? current_composition() : nullptr;
    if (!comp) return false;
    const i64 last = std::max<i64>(std::max<i64>(0, comp->duration().value - 1), navigation_end_locked(comp));
    bool found = hasBase && base >= 0 && base <= last;
    i64 best = found ? base : compositionFrame;
    auto consider = [&](i64 frame) {
        if (frame < 0 || frame > last) return;
        if (direction > 0 ? frame <= compositionFrame : frame >= compositionFrame) return;
        if (!found || (direction > 0 ? frame < best : frame > best)) {
            best = frame;
            found = true;
        }
    };
    consider(0);
    consider(std::max<i64>(0, comp->duration().value - 1));
    if (const Layer* l = layerId ? comp->layer(LayerId::unpack(layerId)) : nullptr) {
        consider(l->start.value);
        consider(l->end.value);
    }
    if (found) targetFrame = best;
    return found;
}

// --- P0-4 · barra inferior da seleção múltipla -----------------------------------

u32 Engine::query_multi_time_actions(const u64* ids, u32 count, i64 frame) noexcept {
    if (!ids || count == 0 || count > 4096) return 0;
    std::lock_guard<std::mutex> lock(modelMutex_);
    drain_commands_locked();
    const Composition* comp = project_ ? current_composition() : nullptr;
    if (!comp) return 0;
    i64 earliest = std::numeric_limits<i64>::max();
    i64 latest = std::numeric_limits<i64>::min();
    bool any = false;
    for (u32 i = 0; i < count; ++i) {
        const Layer* l = comp->layer(LayerId::unpack(ids[i]));
        if (!l) continue;
        any = true;
        earliest = std::min(earliest, l->start.value);
        latest = std::max(latest, l->end.value);
    }
    if (!any) return 0;
    // Todas começam no cabeçote ou depois: estender/levar o INÍCIO até ele.
    if (earliest >= frame) return 1;
    // Todas acabam no cabeçote ou antes: estender/levar o FIM até ele.
    if (latest <= frame) return 2;
    return 0;   // o cabeçote cruza as escolhidas: aparar início · dividir · aparar fim
}

// --- P1-3 · colar estilo por parte ----------------------------------------------

u32 Engine::copied_style_parts() noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    if (!clipboard_.hasStyle) return 0;
    const Layer& s = clipboard_.style;
    u32 parts = StyleBlendOpacity | StyleTransform;
    if (s.kind == LayerKind::Shape || s.kind == LayerKind::Text) parts |= StyleColorFill | StyleBorderShadow;
    if (s.kind == LayerKind::Text) parts |= StyleText;
    if (has_audio(s)) parts |= StyleVolume;
    if (has_speed(s)) parts |= StyleSpeed;
    for (const EffectInstance& e : s.effects) {
        if (is_decoration_effect(e.type)) parts |= StyleBorderShadow;
        else parts |= StyleEffects;
    }
    return parts;
}

u32 Engine::paste_style_parts(const u64* ids, u32 count, u32 parts) noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    drain_commands_locked();
    Composition* comp = project_ ? current_composition() : nullptr;
    if (!comp || !ids || count == 0 || count > 4096 || !clipboard_.hasStyle || parts == 0) return 0;
    std::vector<LayerId> targets;
    for (u32 i = 0; i < count; ++i) {
        const LayerId id = LayerId::unpack(ids[i]);
        const Layer* l = comp->layer(id);
        if (l && !l->locked && std::find(targets.begin(), targets.end(), id) == targets.end()) targets.push_back(id);
    }
    if (targets.empty()) return 0;
    const Layer& s = clipboard_.style;
    history_.before_mutation(*comp, project_->timeline().current(), "colar estilo");
    u32 done = 0;
    for (const LayerId id : targets) {
        Layer& d = *comp->layer(id);
        bool changed = false;
        if (parts & StyleColorFill) {
            if (d.kind == LayerKind::Shape && s.kind == LayerKind::Shape) {
                d.shape.fillColor = s.shape.fillColor;
                d.shape.filled = s.shape.filled;
                changed = true;
            } else if (d.kind == LayerKind::Text && s.kind == LayerKind::Text) {
                d.text.color = s.text.color;
                changed = true;
            }
        }
        if (parts & StyleBorderShadow) {
            if (d.kind == LayerKind::Shape && s.kind == LayerKind::Shape) {
                d.shape.strokeColor = s.shape.strokeColor;
                d.shape.strokeWidth = s.shape.strokeWidth;
                changed = true;
            } else if (d.kind == LayerKind::Text && s.kind == LayerKind::Text) {
                d.text.strokeWidth = s.text.strokeWidth;
                d.text.strokeColor = s.text.strokeColor;
                d.text.shadow = s.text.shadow;
                d.text.shadowColor = s.text.shadowColor;
                d.text.shadowOffset = s.text.shadowOffset;
                d.text.shadowBlur = s.text.shadowBlur;
                changed = true;
            }
            changed = replace_effects_where(d, s, [](EffectTypeId t) { return is_decoration_effect(t); }) || changed;
        }
        if (parts & StyleBlendOpacity) {
            d.blendMode = s.blendMode;
            d.transform.opacity = s.transform.opacity;
            replace_track(d, s, TrackProperty::Opacity);
            changed = true;
        }
        if (parts & StyleTransform) {
            // Mover e transformar: posição, escala, giro, âncora e inclinação
            // (a opacidade é da mesclagem; o desfoque de movimento fica).
            d.transform.position = s.transform.position;
            d.transform.scale = s.transform.scale;
            d.transform.rotation = s.transform.rotation;
            d.transform.anchor = s.transform.anchor;
            d.transform.skewX = s.transform.skewX;
            d.transform.skewY = s.transform.skewY;
            for (u32 p = 0; p <= static_cast<u32>(TrackProperty::SkewY); ++p) {
                if (p == static_cast<u32>(TrackProperty::Opacity)) continue;
                replace_track(d, s, static_cast<TrackProperty>(p));
            }
            changed = true;
        }
        if ((parts & StyleText) && d.kind == LayerKind::Text && s.kind == LayerKind::Text) {
            d.text.font = s.text.font;
            d.text.fontFamily = s.text.fontFamily;
            d.text.fontWeight = s.text.fontWeight;
            d.text.fontItalic = s.text.fontItalic;
            d.text.fontPath = s.text.fontPath;
            d.text.size = s.text.size;
            d.text.alignment = s.text.alignment;
            d.text.lineHeight = s.text.lineHeight;
            d.text.tracking = s.text.tracking;
            d.text.background = s.text.background;
            d.text.backgroundColor = s.text.backgroundColor;
            d.text.backgroundPadding = s.text.backgroundPadding;
            d.text.backgroundRadius = s.text.backgroundRadius;
            replace_track(d, s, TrackProperty::TextTracking);
            changed = true;
        }
        if ((parts & StyleVolume) && has_audio(d) && has_audio(s)) {
            d.gain = s.gain;
            d.pan = s.pan;
            d.fadeIn = s.fadeIn;
            d.fadeOut = s.fadeOut;
            replace_track(d, s, TrackProperty::AudioVolume);
            changed = true;
        }
        if (parts & StyleEffects)
            changed = replace_effects_where(d, s, [](EffectTypeId t) { return !is_decoration_effect(t); }) || changed;
        if ((parts & StyleSpeed) && has_speed(d) && has_speed(s)) {
            d.speed = s.speed;
            d.reversed = s.reversed;
            replace_track(d, s, TrackProperty::Speed);
            changed = true;
        }
        if (changed) ++done;
    }
    bump_model_revision_locked();
    project_->mark_dirty();
    request_render();
    return done;
}

// --- P2-1 · modos de visão da prévia ----------------------------------------------

void Engine::set_preview_view(u32 selectionView, const u64* ids, u32 count, bool pixelPreview) noexcept {
    const u32 mode = selectionView <= 2 ? selectionView : 0;
    std::vector<u64> next;
    if (mode != 0 && ids) next.assign(ids, ids + std::min<u32>(count, kOmegaViewMaxIds));
    std::sort(next.begin(), next.end());
    next.erase(std::unique(next.begin(), next.end()), next.end());
    std::lock_guard<std::mutex> lock(modelMutex_);
    if (mode == omegaViewMode_ && pixelPreview == omegaPixelPreview_ && next == omegaViewIds_) return;
    omegaViewMode_ = mode;
    omegaPixelPreview_ = pixelPreview;
    omegaViewIds_ = std::move(next);
    // O quadro guardado da prévia (e o pré-render) foi feito no modo anterior.
    bump_model_revision_locked();
    request_render();
}

u32 Engine::preview_view_mode() noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    return omegaViewMode_ | (omegaPixelPreview_ ? 0x100u : 0u) | (static_cast<u32>(omegaViewIds_.size()) << 16);
}

void Engine::apply_omega_view_locked(RenderSettings& rs) const noexcept {
    if (rs.finalQuality) return;
    if (omegaViewMode_ != 0 && !omegaViewIds_.empty()) {
        rs.viewSelection = static_cast<u8>(omegaViewMode_);
        rs.viewSelectionCount = static_cast<u32>(std::min<usize>(omegaViewIds_.size(), kOmegaViewMaxIds));
        std::memcpy(rs.viewSelectionIds, omegaViewIds_.data(), rs.viewSelectionCount * sizeof(u64));
    }
    if (omegaPixelPreview_) {
        // Pixels reais: a composição na resolução dela (sem a redução da prévia)
        // e a saída por vizinho mais próximo.
        rs.pixelPreview = true;
        rs.previewNumerator = 1;
        rs.previewDenominator = 1;
    }
}

} // namespace aurea
