#pragma once
#include "aurea/export/CodecOutputPacket.hpp"
#include <media/NdkMediaMuxer.h>

namespace aurea::android {
inline bool normalize_codec_output_packet(const uint8_t* data, size_t& capacity,
        AMediaCodecBufferInfo& info) noexcept {
    return aurea::normalize_codec_output_packet(data, info.size, info.offset, capacity);
}

// NDK's muxer applies info.offset itself. Passing data + offset here would
// skip that prefix twice. Codec-owned packets must be normalized above first;
// this helper also accepts owned buffers whose prefix and bounds are real.
inline media_status_t write_muxer_packet(AMediaMuxer* muxer, size_t track,
        const uint8_t* data, size_t capacity, const AMediaCodecBufferInfo& info) noexcept {
    if (!data || info.offset < 0 || info.size < 0 || static_cast<size_t>(info.offset) > capacity ||
        static_cast<size_t>(info.size) > capacity - static_cast<size_t>(info.offset)) return AMEDIA_ERROR_MALFORMED;
    return AMediaMuxer_writeSampleData(muxer, track, data, &info);
}
} // namespace aurea::android
