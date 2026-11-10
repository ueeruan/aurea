// =============================================================================
//  FAIXAS DA EDIÇÃO: o fluxo de montagem por faixas (V1, V2…, A1, A2…).
//
//  Incluído no fim de Engine.cpp, porque usa as regras de tempo de clipe de lá
//  (trim_clip_copy, clip_source_limit). As interfaces só escolhem a operação e
//  o quadro; quem decide até onde cada borda pode ir é este arquivo.
//
//  O áudio ligado não é uma segunda camada: é a mesma camada de vídeo vista na
//  faixa A. Ele anda junto sem nenhum vínculo a manter, e o aparelho não abre
//  a mesma mídia duas vezes. "Desvincular" (extract_audio) cria o clipe real.
// =============================================================================

namespace {

/// Clipes de uma faixa, em ordem de tempo.
std::vector<LayerId> clips_on_track(const Composition& comp, u32 track) {
    std::vector<LayerId> ids;
    comp.layers().for_each([&](LayerId id, const Layer& l) { if (l.trackId == track) ids.push_back(id); });
    std::sort(ids.begin(), ids.end(), [&](LayerId a, LayerId b) { return comp.layer(a)->start.value < comp.layer(b)->start.value; });
    return ids;
}

bool has_linked_audio(const Project& project, const Layer& l) noexcept {
    if (l.kind != LayerKind::Video || l.audioUnlinked) return false;
    const Asset* asset = project.asset(l.source);
    return asset && asset->has_audio();
}

/// A faixa que mostra o áudio ligado de `l` (0 = não há faixa de áudio).
u32 linked_audio_track(const Composition& comp, const Layer& l) noexcept {
    if (const EditTrack* t = comp.edit_track(l.audioTrackId); t && t->audio) return t->id;
    for (const EditTrack& t : comp.edit_tracks()) if (t.audio) return t.id;
    return 0;
}

/// Até onde `wanted` pode ir, vindo de `valid`, sem pedir quadro que a mídia não tem.
i64 clamp_to_source(const Project& project, const Composition& comp, const Layer& l, i64 wanted, i64 valid) noexcept {
    if (!has_clip_source(l)) return wanted;
    const f64 limit = clip_source_limit(project, comp, l);
    if (limit < 0) return wanted;
    const auto inside = [&](i64 f) {
        const f64 s = l.source_frame(FrameIndex{f});
        return std::isfinite(s) && s >= -0.001 && s <= limit + 0.001;
    };
    if (inside(wanted)) return wanted;
    i64 good = valid, bad = wanted;
    while (std::abs(bad - good) > 1) {
        const i64 mid = good + (bad - good) / 2;
        if (inside(mid)) good = mid; else bad = mid;
    }
    return good;
}

/// Divide `id` em `at`; os dois pedaços ficam na mesma faixa. Devolve o segundo.
LayerId split_clip_at(const Project& project, Composition& comp, LayerId id, i64 at) {
    Layer* l = comp.layer(id);
    if (!l || at <= l->start.value || at >= l->end.value) return LayerId{};
    const FrameIndex originalStart = l->start, originalEnd = l->end, originalOffset = l->offset;
    const u32 track = l->trackId;
    const bool magnetic = l->magneticTrack;
    const LayerId second = comp.duplicate_layer(id, FrameIndex{at});
    if (!second.valid()) return LayerId{};
    l = comp.layer(id);
    const f64 limit = clip_source_limit(project, comp, *l);
    trim_clip_copy(*l, originalStart.value, at, limit);
    if (Layer* s = comp.layer(second)) {
        s->start = originalStart; s->end = originalEnd; s->offset = originalOffset;
        trim_clip_copy(*s, at, originalEnd.value, limit);
        s->trackId = track;
        s->magneticTrack = magnetic;
        s->name = l->name;   // os dois pedaços continuam sendo o mesmo clipe
    }
    return second;
}

/// O que há em [from, to) na faixa impede um corte? (clipe travado)
bool range_has_locked_clip(const Composition& comp, u32 track, i64 from, i64 to, LayerId except) noexcept {
    bool found = false;
    comp.layers().for_each([&](LayerId id, const Layer& l) {
        if (id != except && l.trackId == track && l.locked && l.start.value < to && l.end.value > from) found = true;
    });
    return found;
}

/// Abre o espaço [from, to) na faixa: apaga, apara ou divide o que estiver nele.
template <typename Drop>
void carve_track(const Project& project, Composition& comp, u32 track, i64 from, i64 to, LayerId except, Drop&& drop) {
    for (const LayerId id : clips_on_track(comp, track)) {
        if (id == except) continue;
        Layer* l = comp.layer(id);
        if (!l || l->end.value <= from || l->start.value >= to) continue;
        const i64 s = l->start.value, e = l->end.value;
        const f64 limit = clip_source_limit(project, comp, *l);
        if (s >= from && e <= to) {
            drop(id);
        } else if (s < from && e > to) {
            const LayerId tail = split_clip_at(project, comp, id, to);
            if (Layer* head = comp.layer(id)) trim_clip_copy(*head, s, from, limit);
            (void)tail;
        } else if (s < from) {
            trim_clip_copy(*l, s, from, limit);
        } else {
            trim_clip_copy(*l, to, e, limit);
        }
    }
}

/// Ripple com as outras faixas sincronizadas. Na faixa dona, tudo o que começa
/// em `from` ou depois anda `delta`. Nas outras destravadas, puxar só vai até
/// encostar no clipe anterior: nada passa por cima de nada.
void ripple_tracks(Composition& comp, i64 from, i64 delta, u32 own, LayerId except) noexcept {
    if (delta == 0) return;
    for (const EditTrack& t : comp.edit_tracks()) {
        if (t.locked && t.id != own) continue;
        i64 applied = delta;
        if (t.id != own) {
            i64 before = 0, first = -1;
            bool lockedClip = false;
            comp.layers().for_each([&](LayerId id, const Layer& l) {
                if (l.trackId != t.id || id == except) return;
                if (l.start.value < from) { before = std::max(before, l.end.value); return; }
                if (l.locked) lockedClip = true;
                if (first < 0 || l.start.value < first) first = l.start.value;
            });
            if (first < 0 || lockedClip) continue;
            if (delta < 0) applied = std::min<i64>(0, std::max(delta, before - first));
            if (applied == 0) continue;
        }
        comp.shift_track(FrameIndex{from}, applied, t.id, except);
    }
}

/// Áudio real por cima do áudio ligado de um vídeo: as duas ondas tocariam juntas sem aparecer.
bool hits_linked_audio(const Project& project, const Composition& comp, u32 track, i64 from, i64 to, LayerId except) noexcept {
    bool hit = false;
    comp.layers().for_each([&](LayerId id, const Layer& l) {
        if (id != except && l.start.value < to && l.end.value > from && has_linked_audio(project, l) && linked_audio_track(comp, l) == track) hit = true;
    });
    return hit;
}

} // namespace

void Engine::fit_sequence_locked(Composition& comp) noexcept {
    (void)comp.arrange_edit_tracks();
    i64 last = 0;
    comp.layers().for_each([&](LayerId, const Layer& l) { last = std::max(last, l.end.value); });
    // A sequência termina no último clipe, como o que o Premiere exporta.
    if (last > 0 && last != comp.duration().value) {
        comp.set_duration(FrameIndex{last});
        configure_playback_locked(&comp);
    }
    comp.touch();
    project_->mark_dirty();
    request_render();
}

bool Engine::edit_track_clip(u32 operation, u64 layerId, i64 frame, u32 track) noexcept {
    constexpr i64 limit = i64{1} << 31;
    const auto op = static_cast<TrackEditOp>(operation);
    if (operation > static_cast<u32>(TrackEditOp::MoveLinkedAudio) || frame <= -limit || frame >= limit) return false;
    bool slipByCurve = false;
    {
        std::lock_guard<std::mutex> lock(modelMutex_);
        drain_commands_locked();
        Composition* comp = project_ ? current_composition() : nullptr;
        if (!comp) return false;
        (void)comp->arrange_edit_tracks();
        const CompositionId compId = project_->timeline().current();
        const auto drop = [&](LayerId id) {
            media_.close_layer(id);
            comp->remove_layer(id);
            selection_.erase(std::remove(selection_.begin(), selection_.end(), id.pack()), selection_.end());
        };
        const auto begin = [&](const char* label) {
            history_.before_mutation(*comp, compId, label);
            bump_model_revision_locked();
        };

        if (op == TrackEditOp::RazorAll) {
            std::vector<LayerId> hit;
            comp->layers().for_each([&](LayerId id, const Layer& l) {
                const EditTrack* t = comp->edit_track(l.trackId);
                if (t && !t->locked && t->target && !l.locked && frame > l.start.value && frame < l.end.value) hit.push_back(id);
            });
            if (hit.empty()) return false;
            begin("cortar todas as faixas");
            ClipChainEdit chains(comp);
            for (const LayerId id : hit) (void)split_clip_at(*project_, *comp, id, frame);
            fit_sequence_locked(*comp);
            return true;
        }

        const LayerId id = LayerId::unpack(layerId);
        Layer* l = comp->layer(id);
        const EditTrack* own = l ? comp->edit_track(l->trackId) : nullptr;
        if (!l || !own || l->locked || own->locked) return false;
        const u32 ownTrack = own->id;
        const i64 start = l->start.value, end = l->end.value, length = end - start;
        if (length <= 0) return false;
        const f64 sourceLimit = clip_source_limit(*project_, *comp, *l);

        Layer* prev = nullptr; Layer* next = nullptr;
        for (const LayerId other : clips_on_track(*comp, ownTrack)) {
            if (other == id) continue;
            Layer* c = comp->layer(other);
            if (c->end.value <= start && (!prev || c->end.value > prev->end.value)) prev = c;
            if (c->start.value >= end && (!next || c->start.value < next->start.value)) next = c;
        }

        switch (op) {
            case TrackEditOp::Move:
            case TrackEditOp::MoveInsert: {
                const EditTrack* dest = track ? comp->edit_track(track) : own;
                if (!dest || dest->locked || dest->audio != (l->kind == LayerKind::Audio)) return false;
                const u32 destTrack = dest->id;
                i64 target = std::max<i64>(0, frame);
                if (op == TrackEditOp::Move) {
                    if (target == start && destTrack == ownTrack) return false;
                    if (range_has_locked_clip(*comp, destTrack, target, target + length, id)) return false;
                    if (dest->audio && hits_linked_audio(*project_, *comp, destTrack, target, target + length, id)) return false;
                    begin("mover clipe");
                    ClipChainEdit chains(comp);
                    carve_track(*project_, *comp, destTrack, target, target + length, id, drop);
                    if (Layer* moved = comp->layer(id)) {
                        moved->start = FrameIndex{target}; moved->end = FrameIndex{target + length}; moved->trackId = destTrack;
                    }
                    break;
                }
                bool blocked = false;
                comp->layers().for_each([&](LayerId other, const Layer& c) {
                    if (other != id && c.trackId == destTrack && c.locked && c.end.value > target) blocked = true;
                });
                if (blocked) return false;
                begin("inserir clipe");
                ClipChainEdit chains(comp);
                // Sai de onde estava fechando o buraco; o alvo anda junto com ele.
                ripple_tracks(*comp, end, -length, ownTrack, id);
                if (target >= end) target -= length; else if (target > start) target = start;
                for (const LayerId other : clips_on_track(*comp, destTrack)) {
                    const Layer* c = comp->layer(other);
                    if (other != id && c && target > c->start.value && target < c->end.value) { (void)split_clip_at(*project_, *comp, other, target); break; }
                }
                ripple_tracks(*comp, target, length, destTrack, id);
                if (Layer* moved = comp->layer(id)) {
                    moved->start = FrameIndex{target}; moved->end = FrameIndex{target + length}; moved->trackId = destTrack;
                }
                break;
            }
            case TrackEditOp::TrimIn: {
                const i64 low = prev ? prev->end.value : 0;
                const i64 target = clamp_to_source(*project_, *comp, *l, std::clamp<i64>(frame, low, end - 1), start);
                if (target == start) return false;
                begin("aparar inicio");
                trim_clip_copy(*l, target, end, sourceLimit);
                break;
            }
            case TrackEditOp::TrimOut: {
                const i64 high = next ? next->start.value : limit - 1;
                const i64 target = clamp_to_source(*project_, *comp, *l, std::clamp<i64>(frame, start + 1, high) - 1, end - 1) + 1;
                if (target == end) return false;
                begin("aparar fim");
                trim_clip_copy(*l, start, target, sourceLimit);
                break;
            }
            case TrackEditOp::RippleTrimIn: {
                const i64 target = clamp_to_source(*project_, *comp, *l, std::min<i64>(frame, end - 1), start);
                if (target == start) return false;
                begin("ripple do inicio");
                trim_clip_copy(*l, target, end, sourceLimit);
                // O clipe fica onde estava: quem anda é o que vem depois.
                const i64 delta = start - target;
                l->start.value += delta; l->end.value += delta;
                ripple_tracks(*comp, end, delta, ownTrack, id);
                break;
            }
            case TrackEditOp::RippleTrimOut: {
                const i64 target = clamp_to_source(*project_, *comp, *l, std::max<i64>(frame, start + 1) - 1, end - 1) + 1;
                if (target == end) return false;
                begin("ripple do fim");
                trim_clip_copy(*l, start, target, sourceLimit);
                ripple_tracks(*comp, end, target - end, ownTrack, id);
                break;
            }
            case TrackEditOp::Roll: {
                if (!next || next->start.value != end || next->locked) return false;
                Layer* right = next;
                const f64 rightLimit = clip_source_limit(*project_, *comp, *right);
                i64 cut = std::clamp<i64>(frame, start + 1, right->end.value - 1);
                cut = clamp_to_source(*project_, *comp, *l, cut - 1, end - 1) + 1;
                cut = clamp_to_source(*project_, *comp, *right, cut, end);
                if (cut == end) return false;
                begin("roll");
                trim_clip_copy(*l, start, cut, sourceLimit);
                trim_clip_copy(*right, cut, right->end.value, rightLimit);
                break;
            }
            case TrackEditOp::Slip: {
                if (!has_clip_source(*l) || frame == 0) return false;
                // Com keyframes, mudar o ponto de entrada arrastaria as chaves junto.
                if (l->timeRemapEnabled || l->speed_track() || l->tracks.has_animation()) { slipByCurve = true; break; }
                // Arrastar para a direita mostra quadros anteriores da mídia.
                i64 shift = -frame;
                if (sourceLimit >= 0) {
                    const f64 a = l->source_frame(FrameIndex{start}), b = l->source_frame(FrameIndex{end - 1});
                    const i64 lowest = -static_cast<i64>(std::floor(std::min(a, b) + 0.001));
                    const i64 highest = static_cast<i64>(std::floor(sourceLimit - std::max(a, b) + 0.001));
                    shift = std::clamp(shift, std::min<i64>(lowest, 0), std::max<i64>(highest, 0));
                }
                if (shift == 0) return false;
                begin("slip");
                l->offset.value += shift;
                break;
            }
            case TrackEditOp::Slide: {
                const bool leftTouch = prev && prev->end.value == start && !prev->locked;
                const bool rightTouch = next && next->start.value == end && !next->locked;
                i64 low = -start, high = limit;
                if (prev) low = leftTouch ? prev->start.value + 1 - start : prev->end.value - start;
                if (next) high = rightTouch ? next->end.value - 1 - end : next->start.value - end;
                if (low > high) return false;
                i64 delta = std::clamp(frame, low, high);
                if (leftTouch) delta = clamp_to_source(*project_, *comp, *prev, start + delta - 1, start - 1) + 1 - start;
                if (rightTouch) delta = clamp_to_source(*project_, *comp, *next, end + delta, end) - end;
                if (delta == 0) return false;
                begin("slide");
                if (leftTouch) trim_clip_copy(*prev, prev->start.value, start + delta, clip_source_limit(*project_, *comp, *prev));
                if (rightTouch) trim_clip_copy(*next, end + delta, next->end.value, clip_source_limit(*project_, *comp, *next));
                l->start.value += delta; l->end.value += delta;
                break;
            }
            case TrackEditOp::Razor: {
                if (frame <= start || frame >= end) return false;
                begin("cortar clipe");
                ClipChainEdit chains(comp);
                if (!split_clip_at(*project_, *comp, id, frame).valid()) return false;
                break;
            }
            case TrackEditOp::RippleDelete: {
                begin("excluir e fechar");
                ClipChainEdit chains(comp);
                drop(id);
                ripple_tracks(*comp, end, -length, ownTrack, id);
                break;
            }
            case TrackEditOp::Lift: {
                begin("excluir");
                ClipChainEdit chains(comp);
                drop(id);
                break;
            }
            case TrackEditOp::MoveLinkedAudio: {
                const EditTrack* lane = comp->edit_track(track);
                if (!has_linked_audio(*project_, *l) || !lane || !lane->audio || lane->locked || lane->id == linked_audio_track(*comp, *l)) return false;
                bool busy = range_has_locked_clip(*comp, lane->id, start, end, id);
                comp->layers().for_each([&](LayerId other, const Layer& c) {
                    if (other != id && c.trackId == lane->id && c.start.value < end && c.end.value > start) busy = true;
                });
                if (busy || hits_linked_audio(*project_, *comp, lane->id, start, end, id)) return false;
                begin("mover audio ligado");
                l->audioTrackId = lane->id;
                break;
            }
            default: return false;
        }
        if (!slipByCurve) { fit_sequence_locked(*comp); return true; }
    }
    // Clipe com keyframes: o caminho que preserva as chaves (curva de tempo).
    return edit_clip_time(layerId, 2, -frame);
}

std::vector<i64> Engine::query_edit_timeline() noexcept {
    std::vector<i64> out;
    std::lock_guard<std::mutex> lock(modelMutex_);
    drain_commands_locked();
    Composition* comp = project_ ? current_composition() : nullptr;
    if (!comp) return out;
    (void)comp->arrange_edit_tracks();
    std::vector<i64> linked;
    comp->layers().for_each([&](LayerId id, const Layer& l) {
        if (!has_linked_audio(*project_, l)) return;
        linked.push_back(static_cast<i64>(id.pack()));
        linked.push_back(static_cast<i64>(linked_audio_track(*comp, l)));
    });
    const auto& tracks = comp->edit_tracks();
    out.reserve(4 + tracks.size() * 2 + linked.size());
    out.push_back(static_cast<i64>(tracks.size()));
    out.push_back(static_cast<i64>(linked.size() / 2));
    out.push_back(comp->mark_in());
    out.push_back(comp->mark_out());
    for (const EditTrack& t : tracks) {
        out.push_back(static_cast<i64>(t.id));
        out.push_back((t.audio ? 1 : 0) | (t.locked ? 2 : 0) | (t.muted ? 4 : 0) | (t.solo ? 8 : 0) | (t.target ? 16 : 0));
    }
    out.insert(out.end(), linked.begin(), linked.end());
    return out;
}

bool Engine::set_edit_track_flag(u32 track, u32 flag, bool on) noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    drain_commands_locked();
    Composition* comp = project_ ? current_composition() : nullptr;
    EditTrack* t = comp ? comp->edit_track(track) : nullptr;
    if (!t || flag > 3) return false;
    bool& field = flag == 0 ? t->locked : flag == 1 ? t->muted : flag == 2 ? t->solo : t->target;
    if (field == on) return true;
    history_.before_mutation(*comp, project_->timeline().current(), "estado da faixa");
    bump_model_revision_locked();
    field = on;
    comp->apply_edit_track_state(true);
    comp->touch();
    project_->mark_dirty();
    request_render();
    return true;
}

u32 Engine::add_edit_track(bool audio) noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    drain_commands_locked();
    Composition* comp = project_ ? current_composition() : nullptr;
    if (!comp) return 0;
    history_.before_mutation(*comp, project_->timeline().current(), "nova faixa");
    bump_model_revision_locked();
    const u32 id = comp->add_edit_track(audio);
    comp->touch();
    project_->mark_dirty();
    return id;
}

bool Engine::remove_edit_track(u32 track) noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    drain_commands_locked();
    Composition* comp = project_ ? current_composition() : nullptr;
    const EditTrack* t = comp ? comp->edit_track(track) : nullptr;
    if (!t) return false;
    bool used = false;
    comp->layers().for_each([&](LayerId, const Layer& l) {
        if (l.trackId == track || (has_linked_audio(*project_, l) && linked_audio_track(*comp, l) == track)) used = true;
    });
    if (used) return false;
    u32 sameKind = 0;
    for (const EditTrack& other : comp->edit_tracks()) sameKind += other.audio == t->audio;
    if (sameKind <= 1) return false;
    history_.before_mutation(*comp, project_->timeline().current(), "remover faixa");
    bump_model_revision_locked();
    comp->remove_edit_track(track);
    comp->touch();
    project_->mark_dirty();
    return true;
}

bool Engine::set_edit_mark(u32 which, i64 frame) noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    drain_commands_locked();
    Composition* comp = project_ ? current_composition() : nullptr;
    if (!comp || which > 1) return false;
    if (frame < -1) frame = -1;
    history_.before_mutation(*comp, project_->timeline().current(), "marcar entrada e saida");
    bump_model_revision_locked();
    i64 in = which == 0 ? frame : comp->mark_in(), out = which == 1 ? frame : comp->mark_out();
    // Marcas cruzadas: a que acabou de mudar vale e a outra sai.
    if (in >= 0 && out >= 0 && in >= out) { if (which == 0) out = -1; else in = -1; }
    comp->set_marks(in, out);
    comp->touch();
    project_->mark_dirty();
    return true;
}

bool Engine::edit_marked_range(bool extract) noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    drain_commands_locked();
    Composition* comp = project_ ? current_composition() : nullptr;
    if (!comp) return false;
    (void)comp->arrange_edit_tracks();
    const i64 from = comp->mark_in(), to = comp->mark_out();
    if (from < 0 || to <= from) return false;
    std::vector<u32> tracks;
    u32 anchor = 0;
    for (const EditTrack& t : comp->edit_tracks()) {
        if (t.locked || !t.target) continue;
        if (range_has_locked_clip(*comp, t.id, from, to, LayerId{})) return false;
        tracks.push_back(t.id);
        if (!anchor) anchor = t.id;
    }
    if (tracks.empty()) return false;
    const auto drop = [&](LayerId id) {
        media_.close_layer(id);
        comp->remove_layer(id);
        selection_.erase(std::remove(selection_.begin(), selection_.end(), id.pack()), selection_.end());
    };
    history_.before_mutation(*comp, project_->timeline().current(), extract ? "extrair trecho" : "levantar trecho");
    bump_model_revision_locked();
    {
        ClipChainEdit chains(comp);
        for (const u32 track : tracks) carve_track(*project_, *comp, track, from, to, LayerId{}, drop);
        if (extract) {
            for (const u32 track : tracks) comp->shift_track(FrameIndex{to}, from - to, track, LayerId{});
            comp->set_marks(-1, -1);
        }
    }
    fit_sequence_locked(*comp);
    return true;
}
