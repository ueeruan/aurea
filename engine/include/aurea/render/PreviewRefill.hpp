#pragma once
#include "aurea/core/Types.hpp"

namespace aurea {
// A seek may temporarily have no decoded frame. Hold the complete picture,
// but let a missing/broken asset time out instead of freezing all other edits.
class PreviewRefill {
    u64 since_ = 0;
public:
    static constexpr u64 kDecodeTransitionNs = 750'000'000ull;

    bool hold(bool missing, bool canHold, u64 nowNs, u64 limitNs = 250'000'000ull) noexcept {
        if (!missing || !canHold) { since_ = 0; return false; }
        if (!since_) since_ = nowNs;
        return nowNs - since_ < limitNs;
    }
};
}
