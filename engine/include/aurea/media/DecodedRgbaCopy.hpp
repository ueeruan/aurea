#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <new>
#include <vector>

namespace aurea::media {

// A native RGBA image can have padded rows and omit padding after its final
// row. Copy only visible pixels into owned storage, after validating the full
// source range. No mapped platform address escapes to the renderer.
inline bool decoded_rgba8_bounds_valid(uint32_t width, uint32_t height, const uint8_t* data,
        size_t rowStride, size_t length) noexcept {
    if (!data || !width || !height || width > 16384 || height > 16384) return false;
    const size_t rowBytes = size_t(width) * 4;
    return rowStride >= rowBytes && length >= rowBytes &&
        size_t(height - 1) <= (length - rowBytes) / rowStride;
}

inline bool copy_decoded_rgba8(uint32_t width, uint32_t height, const uint8_t* data,
        size_t rowStride, size_t length, std::vector<uint8_t>& output) {
    if (!decoded_rgba8_bounds_valid(width, height, data, rowStride, length)) return false;
    const size_t rowBytes = size_t(width) * 4;
    output.resize(rowBytes * height);
    for (uint32_t row = 0; row < height; ++row)
        std::memcpy(output.data() + size_t(row) * rowBytes, data + size_t(row) * rowStride, rowBytes);
    return true;
}

// Native frames use this overload: Android builds without exceptions, and a
// failed fallback allocation must return false from prepare_cpu_planes().
inline bool copy_decoded_rgba8(uint32_t width, uint32_t height, const uint8_t* data,
        size_t rowStride, size_t length, std::unique_ptr<uint8_t[]>& output) noexcept {
    if (!decoded_rgba8_bounds_valid(width, height, data, rowStride, length)) return false;
    const size_t rowBytes = size_t(width) * 4;
    std::unique_ptr<uint8_t[]> pixels{new (std::nothrow) uint8_t[rowBytes * height]};
    if (!pixels) return false;
    for (uint32_t row = 0; row < height; ++row)
        std::memcpy(pixels.get() + size_t(row) * rowBytes, data + size_t(row) * rowStride, rowBytes);
    output.swap(pixels);
    return true;
}

} // namespace aurea::media
