#pragma once

#include <string_view>

namespace aurea::android {

// Android 12/12L Samsung import crash mitigation. Keep MediaCodec hardware
// decoding, but use readable YUV planes instead of PRIVATE gralloc buffers.
// This policy is deliberately limited to the reported OS/vendor combination;
// physical-device confirmation is still required before removing the quirk.
inline bool needs_readable_video_planes(std::string_view manufacturer, int sdk) noexcept {
    if (sdk != 31 && sdk != 32) return false;
    constexpr std::string_view samsung = "samsung";
    if (manufacturer.size() != samsung.size()) return false;
    for (size_t i = 0; i < samsung.size(); ++i) {
        const char ch = manufacturer[i];
        if ((ch >= 'A' && ch <= 'Z' ? ch + ('a' - 'A') : ch) != samsung[i]) return false;
    }
    return true;
}

} // namespace aurea::android
