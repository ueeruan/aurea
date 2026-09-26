// =============================================================================
//  Aurea / command / History.cpp
// =============================================================================
#include "aurea/command/History.hpp"

#include "aurea/timeline/Timeline.hpp"

namespace aurea {

namespace {

u64 vector_bytes(const VectorData& v) noexcept {
    u64 n = 0;
    for (const VectorGroup& g : v.groups) {
        n += sizeof(VectorGroup) + g.name.size();
        for (const VectorPath& p : g.paths) {
            n += sizeof(VectorPath) + p.path.v.size() * sizeof(BezierVertex);
            for (const PathKey& k : p.keys) n += sizeof(PathKey) + k.path.v.size() * sizeof(BezierVertex);
        }
    }
    return n;
}

u64 layer_bytes(const Layer& l) noexcept {
    u64 n = sizeof(Layer) + l.name.size();
    n += l.timeRemap.keys.size() * sizeof(Keyframe);
    for (u32 i = 0; i < l.tracks.size(); ++i) n += sizeof(Track) + l.tracks.at(i).keys.size() * sizeof(Keyframe);
    for (const EffectInstance& e : l.effects) {
        n += sizeof(EffectInstance) + e.params.size() * sizeof(ParamSlot);
        for (const CurveData& c : e.curves) {
            for (const auto& ch : c.channel) n += ch.size() * sizeof(CurveData::Point);
        }
        for (const GradientData& g : e.gradients) n += g.stops.size() * sizeof(GradientStop);
    }
    for (const Mask& m : l.masks) {
        n += sizeof(Mask) + m.name.size() + m.points.size() * sizeof(MaskPoint);
        for (const MaskPathKey& k : m.pathKeys) n += sizeof(MaskPathKey) + k.points.size() * sizeof(MaskPoint);
    }
    n += l.text.content.size() + l.text.fontFamily.size() + l.text.fontPath.size();
    for (const auto& segment : l.captions) {
        n += sizeof(text::CaptionSegment) + segment.text.size();
        for (const auto& word : segment.words) n += sizeof(text::CaptionToken) + word.text.size();
    }
    n += l.text.spans.size() * sizeof(TextSpan) + l.text.animators.size() * sizeof(TextAnimator);
    n += l.shape.path.size() * sizeof(Vec2) + vector_bytes(l.shape.vector);
    return n;
}

} // namespace

u64 History::estimate_bytes(const Composition& comp) noexcept {
    u64 n = sizeof(Composition) + comp.markers().size() * sizeof(Marker);
    comp.layers().for_each([&n](LayerId, const Layer& l) { n += layer_bytes(l); });
    return n;
}

void History::set_budget_bytes(u64 bytes) noexcept {
    budgetBytes_ = bytes;
    enforce_budget();
}

void History::clear() noexcept {
    entries_.clear();
    cursor_ = 0;
    bytes_ = 0;
    groupDepth_ = 0;
    groupCaptured_ = false;
    groupLabel_.clear();
}

void History::erase_range(usize first, usize last) noexcept {
    for (usize i = first; i < last; ++i) bytes_ -= entries_[i].beforeBytes + entries_[i].afterBytes;
    entries_.erase(entries_.begin() + static_cast<std::ptrdiff_t>(first),
                   entries_.begin() + static_cast<std::ptrdiff_t>(last));
}

void History::enforce_budget() noexcept {
    // Mais antigas primeiro; a ação mais recente fica sempre (desfazer a
    // última coisa que o usuário fez é o mínimo). Só sai o que já está
    // aplicado ([0, cursor)): tirar uma refazível da frente desalinharia o redo.
    usize drop = 0;
    u64 bytes = bytes_;
    while (drop < cursor_ && entries_.size() - drop > 1
           && (entries_.size() - drop > kMaxEntries || bytes > budgetBytes_)) {
        bytes -= entries_[drop].beforeBytes + entries_[drop].afterBytes;
        ++drop;
    }
    if (drop == 0) return;
    erase_range(0, drop);
    cursor_ = cursor_ > drop ? cursor_ - drop : 0;
}

void History::begin_group(const char* label) noexcept {
    if (groupDepth_++ == 0) {
        groupCaptured_ = false;
        groupLabel_ = label ? label : "";
    }
}

void History::end_group() noexcept {
    if (groupDepth_ == 0) return;
    if (--groupDepth_ == 0) groupCaptured_ = false;
}

void History::before_mutation(const Composition& comp, CompositionId id, const char* label) {
    if (groupDepth_ > 0) {
        if (groupCaptured_) return;
        groupCaptured_ = true;
    }
    // Ação nova depois de desfazer: o futuro alternativo é descartado.
    erase_range(cursor_, entries_.size());

    Entry e;
    e.label = groupDepth_ > 0 && !groupLabel_.empty() ? groupLabel_ : std::string(label ? label : "");
    e.comp = id;
    e.before = comp.clone();
    e.beforeBytes = estimate_bytes(comp);
    bytes_ += e.beforeBytes;
    entries_.push_back(std::move(e));
    cursor_ = entries_.size();
    enforce_budget();
}

bool History::undo(Timeline& timeline) {
    if (cursor_ == 0) return false;
    Entry& e = entries_[cursor_ - 1];
    Composition* current = timeline.composition(e.comp);
    if (!current || !e.before) return false;
    bytes_ -= e.afterBytes;
    e.after = current->clone();
    e.afterBytes = estimate_bytes(*current);
    bytes_ += e.afterBytes;
    current->restore_from(*e.before);
    --cursor_;
    groupDepth_ = 0;
    groupCaptured_ = false;
    return true;
}

bool History::redo(Timeline& timeline) {
    if (cursor_ >= entries_.size()) return false;
    Entry& e = entries_[cursor_];
    Composition* current = timeline.composition(e.comp);
    if (!current || !e.after) return false;
    current->restore_from(*e.after);
    ++cursor_;
    groupDepth_ = 0;
    groupCaptured_ = false;
    return true;
}

} // namespace aurea
