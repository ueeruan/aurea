#pragma once

#include <cstdint>

namespace aurea::media {

// AImage planes need not include padding after the last row. Validate the
// final sample, without treating rowStride * height as readable memory.
inline bool decoded_plane_fits(uint32_t width, uint32_t height, int rowStride,
                               int pixelStride, int length) noexcept {
    if (!width || !height || rowStride <= 0 || pixelStride <= 0 || length <= 0) return false;
    const uint64_t rowBytes = uint64_t(width - 1) * uint64_t(pixelStride) + 1;
    if (rowBytes > uint64_t(rowStride) || rowBytes > uint64_t(length)) return false;
    return uint64_t(height - 1) <= (uint64_t(length) - rowBytes) / uint64_t(rowStride);
}

} // namespace aurea::media
