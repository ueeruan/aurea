#pragma once

#include "aurea/core/Types.hpp"
#include <cstring>

namespace aurea::export_detail {

// Non-overlapping Y/UV planes; both strides are at least the useful row width.
// Packed planes can be copied together, while padded rows retain their padding.
inline void copy_nv12_plane(u8* destination, usize destinationStride,
                            const u8* source, usize sourceStride,
                            u32 width, u32 rows) noexcept {
    if (!width || !rows) return;
    if (destinationStride == width && sourceStride == width) {
        std::memcpy(destination, source, static_cast<usize>(width) * rows);
        return;
    }
    for (u32 row = 0; row < rows; ++row)
        std::memcpy(destination + static_cast<usize>(row) * destinationStride,
                    source + static_cast<usize>(row) * sourceStride, width);
}

// Copy a complete frame into feeder-owned packed NV12 storage. Keep row copies
// for smaller frames and padded inputs. Host paired measurements favored bulk
// memcpy for 4K, while 1080p regressed. The measured admission threshold is
// 8 MiB. Device-specific throughput is recorded by the export benchmarks.
inline void copy_nv12_to_packed(u8* destination, const u8* y, usize yStride,
                               const u8* uv, usize uvStride, u32 width, u32 height) noexcept {
    if (!width || !height) return;
    const usize yBytes = static_cast<usize>(width) * height;
    const usize uvBytes = static_cast<usize>(width) * (height / 2);
    constexpr usize kBulkFrameBytes = 8ull << 20;
    if (yStride == width && uvStride == width && yBytes + uvBytes >= kBulkFrameBytes) {
        std::memcpy(destination, y, yBytes);
        std::memcpy(destination + yBytes, uv, uvBytes);
        return;
    }
    for (u32 row = 0; row < height; ++row)
        std::memcpy(destination + static_cast<usize>(row) * width,
                    y + static_cast<usize>(row) * yStride, width);
    destination += yBytes;
    for (u32 row = 0; row < height / 2; ++row)
        std::memcpy(destination + static_cast<usize>(row) * width,
                    uv + static_cast<usize>(row) * uvStride, width);
}

} // namespace aurea::export_detail
