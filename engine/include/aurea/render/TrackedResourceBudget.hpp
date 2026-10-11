#pragma once

#include "aurea/core/Types.hpp"

namespace aurea {

// Reusing an existing physical texture does not allocate backing. A driver
// block may include unused storage above a soft envelope; that must still
// prohibit new allocations, without rejecting an unchanged existing frame.
[[nodiscard]] inline bool fits_existing_tracked_resource_budget(u64 budget, u64 poolBytes,
    u64 gpuUsedBytes, u64 knownCacheBytes) noexcept {
    if (!budget) return true;
    if (knownCacheBytes > budget) return false;
    const u64 available = budget - knownCacheBytes;
    return poolBytes <= available && gpuUsedBytes <= available;
}

// A soft admission envelope for resources whose backing storage is tracked.
// This is not RSS/PSS: the backend and cache counters do not cover the runtime,
// driver, application model or every native decoder allocation. Pool bytes are
// already included in backend residency; take the maximum instead of adding.
[[nodiscard]] inline bool fits_tracked_resource_budget(u64 budget, u64 poolBytes,
    u64 gpuUsedBytes, u64 gpuReservedBytes, u64 knownCacheBytes, u64 incomingBytes) noexcept {
    if (!budget) return true;
    if (knownCacheBytes > budget) return false;
    const u64 available = budget - knownCacheBytes;
    // Reserved backing can already contain free space for the new image.
    // Add the logical cost to pool/used, not to that physical reservation;
    // the caller rechecks actual reservation growth after allocation.
    if (poolBytes > available || gpuUsedBytes > available || gpuReservedBytes > available) return false;
    return incomingBytes <= available - poolBytes && incomingBytes <= available - gpuUsedBytes;
}

} // namespace aurea
