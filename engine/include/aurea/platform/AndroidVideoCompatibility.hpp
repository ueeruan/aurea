#pragma once

#include <string_view>

namespace aurea::android {

// Samsung video import crash mitigation. Keep MediaCodec hardware decoding,
// but use readable YUV planes instead of PRIVATE gralloc buffers sampled by
// the GPU (zero-copy). Evidence: limited to Android 12/12L, it stopped the
// crash on the reported Galaxy A51; other Samsung models and Android versions
// (Exynos AND Snapdragon, so the common factor is Samsung's codec/gralloc
// vendor layer, not one GPU driver) kept closing on any video import. It now
// applies to every Samsung release the app supports (API 26+).
inline bool needs_readable_video_planes(std::string_view manufacturer, int sdk) noexcept {
    if (sdk < 26) return false;
    constexpr std::string_view samsung = "samsung";
    if (manufacturer.size() != samsung.size()) return false;
    for (size_t i = 0; i < samsung.size(); ++i) {
        const char ch = manufacturer[i];
        if ((ch >= 'A' && ch <= 'Z' ? ch + ('a' - 'A') : ch) != samsung[i]) return false;
    }
    return true;
}

} // namespace aurea::android
