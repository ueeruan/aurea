// =============================================================================
//  Aurea / command / History.cpp
// =============================================================================
#include "aurea/command/History.hpp"

#include "aurea/timeline/Timeline.hpp"

namespace aurea {

void History::clear() noexcept {
    entries_.clear();
    cursor_ = 0;
    groupDepth_ = 0;
    groupCaptured_ = false;
    groupLabel_.clear();
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
    entries_.erase(entries_.begin() + static_cast<std::ptrdiff_t>(cursor_), entries_.end());

    Entry e;
    e.label = groupDepth_ > 0 && !groupLabel_.empty() ? groupLabel_ : std::string(label ? label : "");
    e.comp = id;
    e.before = comp.clone();
    entries_.push_back(std::move(e));
    if (entries_.size() > kMaxEntries) entries_.erase(entries_.begin());
    cursor_ = entries_.size();
}

bool History::undo(Timeline& timeline) {
    if (cursor_ == 0) return false;
    Entry& e = entries_[cursor_ - 1];
    Composition* current = timeline.composition(e.comp);
    if (!current || !e.before) return false;
    e.after = current->clone();
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
