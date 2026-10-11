#pragma once
#include "aurea/core/Types.hpp"
#include <algorithm>

namespace aurea {
// Called only after the corresponding GPU fence is terminal. A sustained
// lighter workload can return oversized upload rings to their initial floor.
inline u64 upload_ring_shrink_target(u64 capacity, u64 used, u64 floor, u32& quiet) noexcept {
    if (capacity <= floor || used > capacity / 4) { quiet = 0; return 0; }
    if (++quiet < 32) return 0;
    quiet = 0;
    return std::max<u64>(floor, (used * 2 + 255u) & ~255ull);
}
}
