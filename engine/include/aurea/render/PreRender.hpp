#pragma once
#include "aurea/core/Types.hpp"

namespace aurea {
/// A private composition cache. Progress is independent of user exports.
struct PreRenderProgress {
    bool running = false;
    bool complete = false;
    u64 framesDone = 0;
    u64 framesTotal = 0;
    u32 width = 0;
    u32 height = 0;
    i32 direction = 1;
    i32 error = 0;
    u64 diskBytes = 0;
    u64 residentBytes = 0;
};
} // namespace aurea
