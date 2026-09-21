#include "aurea/timeline/Timeline.hpp"
#include "aurea/core/Log.hpp"

namespace aurea {

CompositionId Timeline::create_composition(std::string name,
                                           u32 width, u32 height, f64 fps) {
    if (compositions_.count() >= 256) {
        AUREA_LOG_ERROR("limite de composicoes atingido");
        return CompositionId{};
    }
    Composition comp(std::move(name));
    comp.set_size(width, height);
    comp.set_fps(fps);
    comp.set_duration(FrameIndex{static_cast<i64>(fps * 10.0)});   // 10 s iniciais

    const CompositionId id = compositions_.create(std::move(comp));
    if (Composition* c = compositions_.get(id)) c->set_id(id);

    if (!root_.valid()) root_ = id;
    if (!current_.valid()) {
        current_ = id;
        clock_.set_fps(fps);
    }
    return id;
}

bool Timeline::remove_composition(CompositionId id) noexcept {
    if (!compositions_.contains(id)) return false;
    if (id == root_) return false;   // a raiz não pode ser removida

    // Layers de outras composições que referenciam esta viram órfãs. Não as
    // removemos implicitamente: o chamador decide, e a UI avisa o usuário.
    (void)compositions_.destroy(id);   // existência conferida acima
    if (current_ == id) current_ = root_;
    return true;
}

bool Timeline::set_current(CompositionId id) noexcept {
    const Composition* c = compositions_.get(id);
    if (!c) return false;
    current_ = id;
    clock_.set_fps(c->fps());
    dirty_ = true;
    return true;
}

FrameIndex Timeline::total_duration() const {
    // Só as composições que NÃO são alvo de aninhamento contam: uma pre-comp de
    // 3 s usada dentro de uma de 30 s não estica a timeline para 33 s.
    i64 maxEnd = 0;
    compositions_.for_each([&maxEnd](CompositionId, const Composition& c) {
        if (c.duration().value > maxEnd) maxEnd = c.duration().value;
    });
    return FrameIndex{maxEnd};
}

FrameIndex Timeline::next_snap_point(FrameIndex from, CompositionId comp) const {
    const Composition* c = compositions_.get(comp);
    if (!c) return from;

    i64 best = std::numeric_limits<i64>::max();
    c->layers().for_each([&](LayerId, const Layer& l) {
        // Pontos de ímã: as duas bordas da layer e os keyframes das tracks com
        // animação. O playhead encosta neles — é o que faz alinhar corte com
        // corte sem precisar de zoom alto.
        if (l.start.value > from.value && l.start.value < best) best = l.start.value;
        if (l.end.value > from.value && l.end.value < best) best = l.end.value;
        for (u32 t = 0; t < l.tracks.size(); ++t) {
            const Track& tr = l.tracks.at(t);
            for (const auto& k : tr.keys) {
                if (k.time.value > from.value && k.time.value < best) best = k.time.value;
            }
        }
    });

    if (best == std::numeric_limits<i64>::max()) return from;
    return FrameIndex{best};
}

FrameIndex Timeline::prev_snap_point(FrameIndex from, CompositionId comp) const {
    const Composition* c = compositions_.get(comp);
    if (!c) return from;

    i64 best = std::numeric_limits<i64>::min();
    c->layers().for_each([&](LayerId, const Layer& l) {
        if (l.start.value < from.value && l.start.value > best) best = l.start.value;
        if (l.end.value < from.value && l.end.value > best) best = l.end.value;
        for (u32 t = 0; t < l.tracks.size(); ++t) {
            const Track& tr = l.tracks.at(t);
            for (const auto& k : tr.keys) {
                if (k.time.value < from.value && k.time.value > best) best = k.time.value;
            }
        }
    });

    if (best == std::numeric_limits<i64>::min()) return from;
    return FrameIndex{best};
}

Status Timeline::resolve_nested_time(CompositionId parent, LayerId nestedLayer,
                                     FrameIndex parentTime,
                                     CompositionId& outComp,
                                     FrameIndex& outTime) const {
    const Composition* p = compositions_.get(parent);
    if (!p) return Errc::NotFound;

    const Layer* l = p->layer(nestedLayer);
    if (!l) return Errc::NotFound;
    if (l->kind != LayerKind::Composition) return Errc::InvalidArgument;

    const CompositionId inner = l->nested.composition;
    const Composition* c = compositions_.get(inner);
    if (!c) return Errc::MediaSourceMissing;

    // O tempo dentro da pre-comp é o tempo da layer, menos o início dela, mais
    // o offset de conteúdo — e então multiplicado pelo time remap, se houver.
    FrameIndex local = l->local_time(parentTime);

    if (l->timeRemapEnabled) {
        const f32 remapped = l->timeRemap.sample(local);
        local = FrameIndex{static_cast<i64>(remapped)};
    }

    // Fora da duração da pre-comp o conteúdo congela no último frame em vez de
    // sumir. Congelar é o que o usuário vê ao estender a layer além do
    // material — e sumir seria um bug visível.
    if (local.value < 0) local = FrameIndex{0};
    if (local.value >= c->duration().value) {
        local = FrameIndex{c->duration().value > 0 ? c->duration().value - 1 : 0};
    }

    outComp = inner;
    outTime = local;
    return OkStatus;
}

} // namespace aurea
