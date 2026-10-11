#include "aurea/media/P010Upload.hpp"

#include <bit>
#include <limits>

namespace aurea::media {

Status plan_p010_half_upload(const P010Plane& source, P010HalfUploadPlan& out) noexcept {
    out = {};
    if (!source.data || !source.width || !source.height
        || (source.components != 1 && source.components != 2)) return Errc::InvalidArgument;
    const u64 rowBytes = static_cast<u64>(source.width) * source.components * sizeof(u16);
    if (rowBytes > std::numeric_limits<u32>::max() || source.strideBytes < rowBytes)
        return Errc::OutOfRange;
    const u64 sourceBytes = static_cast<u64>(source.height - 1) * source.strideBytes + rowBytes;
    const u64 samples = rowBytes / sizeof(u16) * source.height;
    if (sourceBytes > std::numeric_limits<usize>::max()
        || samples > std::numeric_limits<usize>::max() / sizeof(u16)
        || (source.capacityBytes && source.capacityBytes < sourceBytes)) return Errc::OutOfRange;
    out.rowBytes = static_cast<u32>(rowBytes);
    out.samples = static_cast<usize>(samples);
    out.sourceBytes = static_cast<usize>(sourceBytes);
    return OkStatus;
}

Status convert_p010_half_plane(const P010Plane& source, std::span<u16> destination) noexcept {
    P010HalfUploadPlan plan;
    const Status status = plan_p010_half_upload(source, plan);
    if (!status.ok()) return status;
    if (!destination.data() || destination.size() < plan.samples) return Errc::OutOfRange;
    const usize rowSamples = plan.rowBytes / sizeof(u16);
    for (u32 y = 0; y < source.height; ++y) {
        const u8* row = source.data + static_cast<usize>(y) * source.strideBytes;
        u16* out = destination.data() + static_cast<usize>(y) * rowSamples;
        for (usize x = 0; x < rowSamples; ++x) {
            // Byte loads respect unaligned planes and odd padded row strides.
            // The low six bits are padding, including when a decoder leaves
            // them nonzero; they are not six additional bits of video data.
            const u32 code = (static_cast<u32>(row[x * 2])
                | (static_cast<u32>(row[x * 2 + 1]) << 8)) >> 6;
            if (!code) { out[x] = 0; continue; }
            const u32 exponent = std::bit_width(code) - 1;
            const u32 mantissa = (code - (1u << exponent)) << (10 - exponent);
            out[x] = static_cast<u16>(((exponent + 5) << 10) | mantissa);
        }
    }
    return OkStatus;
}

} // namespace aurea::media
