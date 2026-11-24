#pragma once

#include "aurea/media/VideoSource.hpp"

namespace aurea {

/// Software decoder for local, seekable video. Returns null for media that
/// must be opened by the platform (provider URIs or unsupported pixel formats).
[[nodiscard]] std::unique_ptr<VideoDecoderBackend> open_portable_video_source(const char* path) noexcept;

} // namespace aurea
