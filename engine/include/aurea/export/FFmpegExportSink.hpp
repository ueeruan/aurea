#pragma once

#include "aurea/export/ExportSink.hpp"

namespace aurea {

// Shared native exporter. The factory never substitutes a platform exporter.
// A build without the four FFmpeg libraries returns nullptr and reports
// unavailable; enabled builds still probe/open the requested encoders at runtime.
[[nodiscard]] bool ffmpeg_export_available() noexcept;
[[nodiscard]] std::unique_ptr<ExportSink> make_ffmpeg_export_sink(void* user) noexcept;

} // namespace aurea
