#pragma once

#include "aurea/text/Captions.hpp"

namespace aurea::text {
/// Regroup existing tokens within their time range and keep individual looks.
[[nodiscard]] bool reflow_caption_track(std::vector<CaptionSegment>& segments, const CaptionOptions& options, f64 fps);
}
