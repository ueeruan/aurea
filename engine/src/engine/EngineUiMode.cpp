// =============================================================================
//  INTERFACE DO PROJETO (0.2.5): Composição (padrão) ou Edição.
//
//  A Edição é a interface de montagem de vídeo: uma TRILHA PRINCIPAL magnética
//  embaixo (os trechos encostados, aparar e apagar fecham o buraco) e as outras
//  linhas por cima (camada, texto, áudio). As regras de montagem já moram no
//  motor (linha magnética, ripple, reordenar); aqui fica só o que a troca de
//  interface decide: guardar a escolha no projeto e preparar a trilha.
// =============================================================================
#include "aurea/Engine.hpp"

#include <algorithm>

namespace aurea {

namespace {

/// Trecho que pode ser a trilha principal: tem imagem em movimento ou parada.
bool is_main_track_kind(LayerKind kind) noexcept {
    return kind == LayerKind::Video || kind == LayerKind::Image || kind == LayerKind::Composition;
}

/// O trecho de mídia mais fundo na pilha (índice 0 da ordem = fundo).
const Layer* deepest_media(const Composition& comp, LayerId* outId) noexcept {
    for (u32 i = 0; i < comp.order().size(); ++i) {
        const LayerId id = comp.order().at(i);
        const Layer* l = comp.layer(id);
        if (l && is_main_track_kind(l->kind)) {
            if (outId) *outId = id;
            return l;
        }
    }
    return nullptr;
}

} // namespace

bool Engine::set_project_ui_mode(u32 mode) noexcept {
    if (mode > 1) return false;
    std::lock_guard<std::mutex> lock(modelMutex_);
    if (!project_) return false;
    EditorSettings& ed = project_->editor_settings();
    const u8 previous = ed.uiMode;
    bool changed = ed.uiMode != mode;
    ed.uiMode = static_cast<u8>(mode);
    if (mode == 0 && previous == 1) {
        // 0.3.6: voltar à Composição devolve o ímã como estava ANTES da Edição.
        // A Edição liga o ímã da composição e a trilha principal magnética só
        // para ela; deixá-los ligados prendia a Composição às regras da Edição
        // (vídeo preso na trilha, aparar o começo sem mover, mídia nova
        // encostada no fim). O que a pessoa já tinha ligado antes fica.
        const u8 before = ed.magnetBeforeEdit;
        ed.magnetBeforeEdit = 0;
        Composition* comp = current_composition();
        if (comp && (before & EditMagnetBefore::Recorded)) {
            const bool restoreComp = !(before & EditMagnetBefore::CompMagnet) && comp->edit_mode();
            bool anyLine = false;
            if (!(before & EditMagnetBefore::AnyLine))
                comp->layers().for_each([&](LayerId, const Layer& l) { if (l.magneticTrack && !l.locked) anyLine = true; });
            if (restoreComp || anyLine) {
                history_.before_mutation(*comp, project_->timeline().current(), "modo composicao");
                bump_model_revision_locked();
                if (restoreComp) comp->set_edit_mode(false);
                if (anyLine) {
                    comp->layers().for_each([&](LayerId id, const Layer&) {
                        if (Layer* l = comp->layer(id); l && !l->locked) l->magneticTrack = false;
                    });
                }
                comp->touch();
            }
        }
    }
    if (mode == 1) {
        Composition* comp = current_composition();
        if (comp) {
            // A linha que vira trilha principal: só quando NENHUMA linha da
            // composição é magnética (projeto vindo da Composição). Quem já
            // escolheu linhas magnéticas fica com a escolha dele.
            bool anyMagnetic = false;
            comp->layers().for_each([&](LayerId, const Layer& l) { if (l.magneticTrack) anyMagnetic = true; });
            // 0.3.6: o ímã de antes da Edição, para a volta à Composição.
            if (previous != 1) {
                ed.magnetBeforeEdit = static_cast<u8>(EditMagnetBefore::Recorded
                    | (comp->edit_mode() ? EditMagnetBefore::CompMagnet : 0)
                    | (anyMagnetic ? EditMagnetBefore::AnyLine : 0));
            }
            LayerId anchor{};
            const Layer* main = anyMagnetic ? nullptr : deepest_media(*comp, &anchor);
            if (!comp->edit_mode() || main) {
                history_.before_mutation(*comp, project_->timeline().current(), "modo edicao");
                bump_model_revision_locked();
                comp->set_edit_mode(true);
                if (main) {
                    const u32 track = main->trackId;
                    // Linha 0 (projeto antigo): a linha é a própria camada.
                    comp->layers().for_each([&](LayerId id, const Layer& l) {
                        if ((track != 0 && l.trackId == track) || id == anchor)
                            if (Layer* edit = comp->layer(id); edit && !edit->locked) edit->magneticTrack = true;
                    });
                }
                comp->touch();
                changed = true;
            }
        }
    }
    if (changed) project_->mark_dirty();
    return true;
}

u32 Engine::project_ui_mode() noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    return project_ ? project_->editor_settings().uiMode : 0u;
}

u64 Engine::main_track_anchor() noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    const Composition* comp = project_ ? current_composition() : nullptr;
    LayerId anchor{};
    return comp && deepest_media(*comp, &anchor) ? anchor.pack() : 0u;
}

namespace {

/// O índice, na ordem SEM `self`, do trecho mais alto da linha `track` (−1 = nenhum).
i32 line_top_without(const Composition& comp, u32 track, LayerId anchor, LayerId self) noexcept {
    i32 top = -1, index = 0;
    for (u32 i = 0; i < comp.order().size(); ++i) {
        const LayerId id = comp.order().at(i);
        if (id == self) continue;
        const Layer* l = comp.layer(id);
        if (l && (track == 0 ? id == anchor : l->trackId == track)) top = index;
        ++index;
    }
    return top;
}

} // namespace

bool Engine::place_on_magnetic_line_locked(Composition& comp, LayerId id, i64 length) noexcept {
    Layer* layer = comp.layer(id);
    if (!comp.edit_mode() || !layer || !layer->magneticTrack || layer->trackId == 0 || length <= 0) return false;
    // O fim da linha SEM o trecho novo. Linha vazia: o primeiro corte começa
    // no zero (a trilha principal não tem buraco no início) com a duração dele.
    i64 tail = 0;
    comp.layers().for_each([&](LayerId other, const Layer& l) {
        if (other != id && l.trackId == layer->trackId) tail = std::max(tail, l.end.value);
    });
    layer->start = FrameIndex{tail};
    layer->end = FrameIndex{tail + length};
    if (layer->end.value > comp.duration().value) {
        comp.set_duration(layer->end);
        configure_playback_locked(&comp);
    }
    return true;
}

void Engine::place_on_audio_line_locked(Composition& comp, LayerId id) noexcept {
    Layer* layer = comp.layer(id);
    if (!layer) return;
    const i64 start = layer->start.value, end = layer->end.value;
    // Linhas que só têm áudio (e quem ocupa o trecho [start, end) nelas).
    std::vector<u32> audioTracks, busy;
    u32 maxTrack = 0;
    comp.layers().for_each([&](LayerId other, const Layer& l) {
        maxTrack = std::max(maxTrack, l.trackId);
        if (other == id || l.trackId == 0) return;
        if (l.kind != LayerKind::Audio) { busy.push_back(l.trackId); return; }
        if (std::find(audioTracks.begin(), audioTracks.end(), l.trackId) == audioTracks.end()) audioTracks.push_back(l.trackId);
        if (l.start.value < end && l.end.value > start) busy.push_back(l.trackId);
    });
    std::sort(audioTracks.begin(), audioTracks.end());
    u32 track = 0;
    for (const u32 t : audioTracks)
        if (std::find(busy.begin(), busy.end(), t) == busy.end()) { track = t; break; }
    layer->trackId = track ? track : maxTrack + 1;
    layer->magneticTrack = false;
}

bool Engine::set_cut_transition(u64 outgoing, u64 incoming, u32 type, u32 frames) noexcept {
    if (type > 5 || outgoing == incoming) return false;
    std::lock_guard<std::mutex> lock(modelMutex_);
    Composition* comp = project_ ? current_composition() : nullptr;
    Layer* a = comp ? comp->layer(LayerId::unpack(outgoing)) : nullptr;
    Layer* b = comp ? comp->layer(LayerId::unpack(incoming)) : nullptr;
    if (!a || !b || a->locked || b->locked) return false;
    history_.before_mutation(*comp, project_->timeline().current(), "transicao no corte");
    bump_model_revision_locked();
    a = comp->layer(LayerId::unpack(outgoing));
    b = comp->layer(LayerId::unpack(incoming));
    if (!a || !b) return false;
    const auto half = [](const Layer& l) { return static_cast<u32>(std::max<i64>(1, (l.end.value - l.start.value) / 2)); };
    const u32 fa = std::clamp<u32>(frames, 1u, half(*a));
    const u32 fb = std::clamp<u32>(frames, 1u, half(*b));
    a->transitionOut = static_cast<u8>(type); a->transitionOutFrames = type ? fa : 0;
    b->transitionIn = static_cast<u8>(type); b->transitionInFrames = type ? fb : 0;
    project_->mark_dirty();
    request_render();
    return true;
}

u32 Engine::cut_transition(u64 incoming) noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    const Composition* comp = project_ ? current_composition() : nullptr;
    const Layer* b = comp ? comp->layer(LayerId::unpack(incoming)) : nullptr;
    return b ? b->transitionIn : 0u;
}

bool Engine::move_clip_to_overlay(u64 layerId, i64 atFrame) noexcept {
    constexpr i64 limit = i64{1} << 31;
    if (atFrame >= limit) return false;
    std::lock_guard<std::mutex> lock(modelMutex_);
    drain_commands_locked();
    Composition* comp = project_ ? current_composition() : nullptr;
    if (!comp) return false;
    const LayerId id = LayerId::unpack(layerId);
    Layer* layer = comp->layer(id);
    if (!layer || layer->locked) return false;
    LayerId anchorId{};
    const Layer* anchor = deepest_media(*comp, &anchorId);
    // Fica logo ACIMA do trecho mais alto da trilha principal — também quando
    // ele mesmo é o primeiro corte dela (a referência vira a linha dele, sem ele).
    // Linha sem outro corte (ou projeto antigo, linha 0): fica onde está.
    const bool self = !anchor || anchorId == id;
    const u32 refTrack = self ? layer->trackId : anchor->trackId;
    u32 insertAt = static_cast<u32>(std::max(comp->order().index_of(id), 0));
    if (refTrack != 0) {
        const i32 top = line_top_without(*comp, refTrack, self ? id : anchorId, id);
        if (top >= 0) insertAt = static_cast<u32>(top + 1);
    }

    history_.before_mutation(*comp, project_->timeline().current(), "para camada");
    bump_model_revision_locked();
    layer = comp->layer(id);
    if (!layer) return false;
    const u32 oldTrack = layer->trackId;
    const bool wasMagnetic = layer->magneticTrack;
    const i64 start = layer->start.value, end = layer->end.value;
    layer->trackId = comp->new_track_id();
    layer->magneticTrack = false;
    if (atFrame >= 0) {
        layer->start = FrameIndex{atFrame};
        layer->end = FrameIndex{atFrame + (end - start)};
    }
    (void)comp->order().move_to(id, insertAt);
    comp->rebuild_draw_order();
    // A trilha de onde ele saiu fica encostada (como no ripple).
    if (wasMagnetic && oldTrack != 0) comp->close_gaps_in_track(FrameIndex{start}, FrameIndex{end}, oldTrack);
    i64 duration = comp->duration().value;
    comp->layers().for_each([&](LayerId, const Layer& item) { duration = std::max(duration, item.end.value); });
    if (duration != comp->duration().value) { comp->set_duration(FrameIndex{duration}); configure_playback_locked(comp); }
    comp->touch();
    project_->mark_dirty();
    request_render();
    return true;
}

bool Engine::move_clip_to_main_track(u64 layerId) noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    drain_commands_locked();
    Composition* comp = project_ ? current_composition() : nullptr;
    if (!comp) return false;
    const LayerId id = LayerId::unpack(layerId);
    Layer* layer = comp->layer(id);
    if (!layer || layer->locked || !is_main_track_kind(layer->kind)) return false;
    LayerId anchorId{};
    const Layer* anchor = deepest_media(*comp, &anchorId);
    if (!anchor || anchorId == id) return false;   // ele já é a trilha principal
    if (anchor->trackId != 0 && anchor->trackId == layer->trackId) return false;   // já mora nela
    const i32 top = line_top_without(*comp, anchor->trackId, anchorId, id);
    if (top < 0) return false;

    history_.before_mutation(*comp, project_->timeline().current(), "para trilha principal");
    bump_model_revision_locked();
    // Projeto antigo (linha 0): a trilha principal ganha um número antes de receber o corte.
    Layer* main = comp->layer(anchorId);
    if (!main) return false;
    if (main->trackId == 0) main->trackId = comp->new_track_id();
    main->magneticTrack = true;
    const u32 track = main->trackId;
    i64 tail = 0;
    comp->layers().for_each([&](LayerId other, const Layer& l) {
        if (other != id && l.trackId == track) tail = std::max(tail, l.end.value);
    });
    layer = comp->layer(id);
    if (!layer) return false;
    const i64 length = layer->end.value - layer->start.value;
    const u32 oldTrack = layer->trackId;
    layer->trackId = track;
    layer->magneticTrack = true;
    layer->start = FrameIndex{tail};
    layer->end = FrameIndex{tail + length};
    (void)comp->order().move_to(id, static_cast<u32>(top));
    comp->rebuild_draw_order();
    (void)oldTrack;
    i64 duration = comp->duration().value;
    comp->layers().for_each([&](LayerId, const Layer& item) { duration = std::max(duration, item.end.value); });
    if (duration != comp->duration().value) { comp->set_duration(FrameIndex{duration}); configure_playback_locked(comp); }
    comp->touch();
    project_->mark_dirty();
    request_render();
    return true;
}

} // namespace aurea
