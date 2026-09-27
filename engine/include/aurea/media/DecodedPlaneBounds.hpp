#pragma once

#include <cstdint>
#include <cstring>
#include <vector>

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

// Copy only visible samples, never final-row padding or vendor-specific layouts.
// Validate every plane before reading any of them. Output owns its storage.
inline bool copy_decoded_yuv420(uint32_t width, uint32_t height,
        const uint8_t* const data[3], const int row[3], const int pixel[3],
        const int length[3], std::vector<uint8_t>& output) {
    if (!width || !height || width > 16384 || height > 16384) return false;
    const uint32_t cw = (width + 1) / 2, ch = (height + 1) / 2;
    for (int i = 0; i < 3; ++i) {
        if (!data[i] || (pixel[i] != 1 && (i == 0 || pixel[i] != 2)) ||
            !decoded_plane_fits(i ? cw : width, i ? ch : height, row[i], pixel[i], length[i])) return false;
    }
    const size_t yBytes = size_t(width) * height, cBytes = size_t(cw) * ch;
    output.resize(yBytes + 2 * cBytes);
    for (int i = 0; i < 3; ++i) {
        const uint32_t w = i ? cw : width, h = i ? ch : height;
        uint8_t* dst = output.data() + (i ? yBytes + size_t(i - 1) * cBytes : 0);
        for (uint32_t y = 0; y < h; ++y) {
            const uint8_t* src = data[i] + size_t(y) * row[i];
            if (pixel[i] == 1) std::memcpy(dst + size_t(y) * w, src, w);
            else for (uint32_t x = 0; x < w; ++x) dst[size_t(y) * w + x] = src[size_t(x) * 2];
        }
    }
    return true;
}

} // namespace aurea::media
