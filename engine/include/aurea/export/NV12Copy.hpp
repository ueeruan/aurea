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

} // namespace aurea::export_detail
