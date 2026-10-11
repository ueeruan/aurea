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
