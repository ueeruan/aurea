#include "TestFramework.hpp"
#include "aurea/export/NV12Copy.hpp"
#include <algorithm>
#include <array>
#include <vector>

using namespace aurea;

namespace {
bool exact_plane(u32 width, u32 rows, usize sourceStride, usize destinationStride, u32 plane) {
    constexpr usize guard = 31;
    constexpr u8 sourcePad = 0xA9, destinationPad = 0xB7;
    std::vector<u8> source(guard + sourceStride * rows + guard, sourcePad);
    std::vector<u8> destination(guard + destinationStride * rows + guard, destinationPad);
    const auto expected = [plane](u32 row, u32 column) {
        return static_cast<u8>((row * 13u + column * 37u + plane * 71u) & 0xFFu);
    };
    for (u32 row = 0; row < rows; ++row)
        for (u32 column = 0; column < width; ++column)
            source[guard + row * sourceStride + column] = expected(row, column);
    const auto original = source;
    export_detail::copy_nv12_plane(destination.data() + guard, destinationStride,
                                  source.data() + guard, sourceStride, width, rows);
    if (source != original) return false;
    for (usize i = 0; i < guard; ++i)
        if (destination[i] != destinationPad || destination[destination.size() - i - 1] != destinationPad)
            return false;
    for (u32 row = 0; row < rows; ++row) {
        for (u32 column = 0; column < width; ++column)
            if (destination[guard + row * destinationStride + column] != expected(row, column)) return false;
        for (usize column = width; column < destinationStride; ++column)
            if (destination[guard + row * destinationStride + column] != destinationPad) return false;
    }
    return true;
}

bool exact_packed_frame(u32 width, u32 height, usize yStride, usize uvStride, usize misalignment) {
    constexpr usize guard = 64;
    constexpr u8 sourcePad = 0xA9, destinationPad = 0xB7;
    const usize prefix = guard + misalignment;
    const usize yBytes = static_cast<usize>(width) * height;
    const usize uvBytes = static_cast<usize>(width) * (height / 2);
    std::vector<u8> y(prefix + yStride * height + guard, sourcePad);
    std::vector<u8> uv(prefix + uvStride * (height / 2) + guard, sourcePad);
    std::vector<u8> destination(prefix + yBytes + uvBytes + guard, destinationPad);
    for (u32 row = 0; row < height; ++row)
        for (u32 x = 0; x < width; ++x)
            y[prefix + row * yStride + x] = static_cast<u8>((row * 13u + x * 37u) & 0xFFu);
    for (u32 row = 0; row < height / 2; ++row)
        for (u32 x = 0; x < width; ++x)
            uv[prefix + row * uvStride + x] = static_cast<u8>((row * 23u + x * 17u + 71u) & 0xFFu);
    const auto originalY = y, originalUV = uv;
    export_detail::copy_nv12_to_packed(destination.data() + prefix,
        y.data() + prefix, yStride, uv.data() + prefix, uvStride, width, height);
    if (y != originalY || uv != originalUV) return false;
    for (usize i = 0; i < prefix; ++i)
        if (destination[i] != destinationPad) return false;
    for (usize i = prefix + yBytes + uvBytes; i < destination.size(); ++i)
        if (destination[i] != destinationPad) return false;
    for (u32 row = 0; row < height; ++row)
        if (std::memcmp(destination.data() + prefix + static_cast<usize>(row) * width,
                        y.data() + prefix + row * yStride, width)) return false;
    for (u32 row = 0; row < height / 2; ++row)
        if (std::memcmp(destination.data() + prefix + yBytes + static_cast<usize>(row) * width,
                        uv.data() + prefix + row * uvStride, width)) return false;
    return true;
}
}

AUREA_TEST(ExportPixelCopy, PackedAndPaddedNV12PreserveEveryByteAndGuard) {
    struct Picture { u32 width, height; };
    for (const auto picture : {Picture{2, 2}, Picture{854, 482}, Picture{1920, 1080}}) {
        const usize aligned = (picture.width + 255u) & ~255u;
        const std::array<usize, 3> strides{picture.width, picture.width + 10u, aligned};
        for (const usize sourceStride : strides)
            for (const usize destinationStride : strides) {
                AUREA_CHECK(exact_plane(picture.width, picture.height, sourceStride, destinationStride, 0));
                AUREA_CHECK(exact_plane(picture.width, picture.height / 2, sourceStride, destinationStride, 1));
            }
    }
}

AUREA_TEST(ExportPixelCopy, EmptyPlanesDoNotReadOrWriteNullMemory) {
    export_detail::copy_nv12_plane(nullptr, 0, nullptr, 0, 0, 1080);
    export_detail::copy_nv12_plane(nullptr, 0, nullptr, 0, 1920, 0);
    std::array<u8, 8> bytes{1, 2, 3, 4, 5, 6, 7, 8};
    const auto original = bytes;
    export_detail::copy_nv12_plane(bytes.data(), 0, nullptr, 0, 0, 1);
    export_detail::copy_nv12_plane(bytes.data(), 1, nullptr, 1, 1, 0);
    AUREA_CHECK(bytes == original);
}

AUREA_TEST(ExportPixelCopy, AdaptivePackedFramesPreservePixelsStridesAlignmentAndGuards) {
    struct Picture { u32 width, height; };
    for (const auto picture : {Picture{2, 2}, Picture{1280, 720}, Picture{1920, 1080}, Picture{3840, 2160}}) {
        const usize width = picture.width;
        for (const auto strides : {std::array<usize, 2>{width, width},
                                  std::array<usize, 2>{width + 64, width},
                                  std::array<usize, 2>{width, width + 32},
                                  std::array<usize, 2>{width + 64, width + 32}})
            for (const usize offset : {usize{0}, usize{1}, usize{7}})
                AUREA_CHECK(exact_packed_frame(picture.width, picture.height, strides[0], strides[1], offset));
    }
}

AUREA_TEST(ExportPixelCopy, EmptyAdaptiveFramesDoNotReadOrWriteNullMemory) {
    export_detail::copy_nv12_to_packed(nullptr, nullptr, 0, nullptr, 0, 0, 1080);
    export_detail::copy_nv12_to_packed(nullptr, nullptr, 0, nullptr, 0, 1920, 0);
    std::array<u8, 8> bytes{1, 2, 3, 4, 5, 6, 7, 8};
    const auto original = bytes;
    export_detail::copy_nv12_to_packed(bytes.data(), nullptr, 0, nullptr, 0, 0, 1);
    export_detail::copy_nv12_to_packed(bytes.data(), nullptr, 0, nullptr, 0, 1, 0);
    AUREA_CHECK(bytes == original);
}
