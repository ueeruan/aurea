#pragma once

#include "aurea/core/Result.hpp"
#include "aurea/core/Types.hpp"

#include <span>

namespace aurea::media {

/// One CPU P010 plane: little-endian words, ten significant high bits. A Y
/// texel has one component; a CbCr texel has two. Strides are always bytes.
struct P010Plane {
    const u8* data = nullptr;
    u32 width = 0, height = 0, components = 0, strideBytes = 0;
    /// Zero means the decoder has not published a byte extent. Otherwise the
    /// last used sample, without requiring trailing row padding, must fit.
    usize capacityBytes = 0;
};

struct P010HalfUploadPlan {
    u32 rowBytes = 0;
    usize samples = 0;
    usize sourceBytes = 0;
};

[[nodiscard]] Status plan_p010_half_upload(const P010Plane& source,
                                          P010HalfUploadPlan& out) noexcept;

/// Packs the plane into binary16(code / 1024). Every P010 code (0..1023) is
/// represented exactly; the existing color shader uses codeScale=1024/1023
/// to recover its usual code/maxCode input. No range/transfer conversion or
/// 8-bit quantization happens here. Source and destination must not overlap.
[[nodiscard]] Status convert_p010_half_plane(const P010Plane& source,
                                            std::span<u16> destination) noexcept;

inline constexpr f32 kP010HalfCodeScale = 1024.0f / 1023.0f;

} // namespace aurea::media
