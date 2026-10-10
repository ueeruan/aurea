#include "TestFramework.hpp"
#include "aurea/media/P010Upload.hpp"

#include <array>
#include <algorithm>
#include <bit>
#include <limits>
#include <vector>

using namespace aurea;
using namespace aurea::media;

namespace {
f32 decode_half(u16 bits) {
    if (!bits) return 0;
    return std::ldexp(1.f + (bits & 1023u) / 1024.f, static_cast<int>(bits >> 10) - 15);
}
void put_code(u8* out, u32 code, u32 padding = 0) {
    const u32 word = code * 64 + padding;
    out[0] = static_cast<u8>(word); out[1] = static_cast<u8>(word >> 8);
}
}

AUREA_TEST(P010Upload, All1024CodesAreExactHalfValuesWithoutEightBitQuantization) {
    std::vector<u8> bytes(1024 * 2);
    for (u32 code = 0; code < 1024; ++code) put_code(bytes.data() + code * 2, code);
    std::array<u16, 1024> converted{};
    const P010Plane source{bytes.data(), 1024, 1, 1, 2048, bytes.size()};
    AUREA_CHECK(convert_p010_half_plane(source, converted).ok());
    for (u32 code = 0; code < 1024; ++code) {
        const f32 value = decode_half(converted[code]);
        AUREA_CHECK_EQ(value, code / 1024.f);
        AUREA_CHECK_NEAR(value * kP010HalfCodeScale, code / 1023.f, 1e-7f);
        if (code) AUREA_CHECK(converted[code] > converted[code - 1]);
    }
    // Black/white and neutral chroma are still the ten-bit codes consumed by
    // the color shader (limited Y=64..940, C=64..960, neutral C=512).
    for (u32 code : {0u, 64u, 512u, 940u, 960u, 1023u})
        AUREA_CHECK_NEAR(decode_half(converted[code]) * kP010HalfCodeScale * 1023.f, code, .0001f);
}

AUREA_TEST(P010Upload, OddPaddedUnalignedRowsKeepUvOrderAndIgnoreOnlySixPaddingBits) {
    for (u32 components : {1u, 2u}) {
        constexpr u32 width = 3, height = 3;
        const u32 pitch = width * components * 2 + 3;
        // Allocate exactly the last used byte: trailing decoder padding is
        // neither required nor read. The extra leading byte unaligns the plane.
        const usize extent = (height - 1) * pitch + width * components * 2;
        std::vector<u8> bytes(extent + 1, 0xDD);
        auto* base = bytes.data() + 1;
        for (u32 y = 0; y < height; ++y) for (u32 x = 0; x < width * components; ++x)
            put_code(base + y * pitch + x * 2, 1 + y * 113 + x * 29, (x + y * 17) & 63);
        const P010Plane source{base, width, height, components, pitch, extent};
        P010HalfUploadPlan plan;
        AUREA_CHECK(plan_p010_half_upload(source, plan).ok());
        AUREA_CHECK_EQ(plan.rowBytes, width * components * 2);
        AUREA_CHECK_EQ(plan.sourceBytes, extent);
        std::vector<u16> result(plan.samples + 2, 0xFFFF);
        AUREA_CHECK(convert_p010_half_plane(source, {result.data() + 1, plan.samples}).ok());
        AUREA_CHECK_EQ(result.front(), 0xFFFFu); AUREA_CHECK_EQ(result.back(), 0xFFFFu);
        for (u32 y = 0; y < height; ++y) for (u32 x = 0; x < width * components; ++x)
            AUREA_CHECK_EQ(decode_half(result[1 + y * width * components + x]), (1 + y * 113 + x * 29) / 1024.f);
        // One byte less than the final sample is an error before any write.
        auto shortSource = source; --shortSource.capacityBytes;
        std::fill(result.begin(), result.end(), 0xFFFF);
        AUREA_CHECK(!convert_p010_half_plane(shortSource, result).ok());
        for (u16 sample : result) AUREA_CHECK_EQ(sample, 0xFFFFu);
    }
}

AUREA_TEST(P010Upload, InvalidPlanesAndShortDestinationsAreRejectedBeforeWriting) {
    std::array<u8, 16> bytes{};
    std::array<u16, 8> result; result.fill(0xFFFF);
    const P010Plane valid{bytes.data(), 2, 2, 2, 8, bytes.size()};
    for (u32 which = 0; which < 8; ++which) {
        auto source = valid;
        if (which == 0) source.data = nullptr;
        if (which == 1) source.width = 0;
        if (which == 2) source.height = 0;
        if (which == 3) source.components = 0;
        if (which == 4) source.components = 3;
        if (which == 5) source.strideBytes = 7;
        if (which == 6) source.capacityBytes = 15;
        if (which == 7) source.width = std::numeric_limits<u32>::max();
        P010HalfUploadPlan plan{123, 456, 789};
        AUREA_CHECK(!plan_p010_half_upload(source, plan).ok());
        AUREA_CHECK_EQ(plan.rowBytes, 0u); AUREA_CHECK_EQ(plan.samples, 0u); AUREA_CHECK_EQ(plan.sourceBytes, 0u);
        AUREA_CHECK(!convert_p010_half_plane(source, result).ok());
    }
    AUREA_CHECK(!convert_p010_half_plane(valid, {result.data(), result.size() - 1}).ok());
    AUREA_CHECK(!convert_p010_half_plane(valid, {}).ok());
    for (u16 sample : result) AUREA_CHECK_EQ(sample, 0xFFFFu);
    // The decoder interface permits unknown extents; all row/overflow checks
    // still apply, while the decoder owns the obligation to retain its planes.
    auto unknownExtent = valid; unknownExtent.capacityBytes = 0;
    AUREA_CHECK(convert_p010_half_plane(unknownExtent, result).ok());
}
