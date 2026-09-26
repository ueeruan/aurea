#pragma once
#include "aurea/core/Result.hpp"
#include "aurea/text/Captions.hpp"
#include <atomic>
#include <functional>
namespace aurea { class VideoSourceFactory; }
namespace aurea::text {
// The decoder and model belong to this job, never to the preview render thread.
Result<std::vector<CaptionWord>> transcribe_local(
    VideoSourceFactory& factory, const std::string& source, const std::string& model,
    const std::string& language, std::atomic<bool>& cancelled,
    const std::function<void(int)>& progress, double start = 0, double end = 0) noexcept;
}
