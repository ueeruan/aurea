#pragma once
#include "aurea/project/Asset.hpp"
#include "aurea/timeline/Layer.hpp"
#include "aurea/tracking/CameraTrackData.hpp"
#include <algorithm>
#include <cmath>

namespace aurea::tracking {
// Metadata identity, deliberately separate from Asset::contentHash: this is
// never used to deduplicate media. Unknown content uses the stored path and
// is conservatively invalidated when relinked. No file I/O on the UI thread.
inline u64 source_signature(const Asset* a) noexcept {
    if (!a) return 0;
    u64 hash = 1469598103934665603ull;
    auto mix = [&](const void* data, usize size) {
        const auto* bytes = static_cast<const u8*>(data);
        for (usize i=0; i<size; ++i) hash = (hash ^ bytes[i]) * 1099511628211ull;
    };
    mix(&a->contentHash, sizeof(a->contentHash));
    if (!a->contentHash) mix(a->sourcePath.data(), a->sourcePath.size());
    mix(&a->fileSizeBytes, sizeof(a->fileSizeBytes));
    mix(&a->video.width, sizeof(a->video.width));
    mix(&a->video.height, sizeof(a->video.height));
    mix(&a->video.fps, sizeof(a->video.fps));
    mix(&a->video.frameCount.value, sizeof(a->video.frameCount.value));
    mix(&a->timebaseFps, sizeof(a->timebaseFps));
    return hash ? hash : 1;
}
inline bool camera_timing_matches(const Layer& layer, const CameraTrackData& data, f64 fps) noexcept {
    if (!std::isfinite(fps) || fps<=0 || !data.frames ||
        layer.duration().value!=data.frames || data.sourceUs.size()!=data.frames) return false;
    for (u32 i=0; i<data.frames; ++i) {
        const auto source = layer.source_frame(FrameIndex{layer.start.value+i});
        if (!std::isfinite(source) ||
            static_cast<i64>(std::llround(std::max(0.0, source)*1e6/fps))!=data.sourceUs[i]) return false;
    }
    return true;
}
}
