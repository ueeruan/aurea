#pragma once

#include <cstddef>
#include <cstdint>

namespace aurea {

// AMediaCodec_getOutputBuffer returns a pointer to the sample, already adjusted
// for the codec's internal offset. Through API 35, BufferInfo.offset and the
// reported output capacity are not valid packet bounds. BufferInfo.size is the
// sample length on every supported API; after API 35, offset is always zero.
// Normalize only codec-owned output. Owned buffers passed to a muxer may still
// have a real prefix and must retain their ordinary offset/bounds validation.
// https://developer.android.com/ndk/reference/group/media#amediacodec_getoutputbuffer
inline bool normalize_codec_output_packet(const uint8_t* data, int32_t size,
        int32_t& offset, size_t& capacity) noexcept {
    offset = 0;
    capacity = 0;
    if (size < 0 || (size > 0 && !data)) return false;
    capacity = static_cast<size_t>(size);
    return true;
}

} // namespace aurea
