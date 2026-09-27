#pragma once

#include "aurea/core/Result.hpp"

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

// Readable-planes mode (Samsung, and the sticky "safe video mode" any device
// enters after a crash with a video open): the hardware codec renders into a
// YUV_420_888 ImageReader that the CPU reads. A vendor codec that cannot fill
// a flexible 4:2:0 layout still configures and delivers images, but the
// planes do not map (`AImage_getPlaneData` fails, pixel stride 2 on Y, fewer
// than 3 planes...): `wrap()` answers UnsupportedFormat/UnsupportedFeature.
// Those codes were NOT a reason to retry with the platform software decoder
// (only Timeout/DecodeFailed were), so every frame failed, VideoSource
// retried forever and the layer stayed BLACK on the device — while the same
// file played on the emulator, whose readable-planes path ends up on the
// software codec anyway. The AOSP software decoders always emit a standard
// flexible layout, so in readable-planes mode a mapping failure on a hardware
// codec is a reason to fall back too. Zero-copy keeps the old rule (there the
// GPU samples the buffer; no plane mapping happens). Never twice: a software
// codec that fails has nothing left to fall back to.
inline bool video_software_fallback(Errc code, bool zeroCopy, bool hardwareDecoder,
                                    bool alreadySoftware) noexcept {
    if (!hardwareDecoder || alreadySoftware) return false;
    if (code == Errc::Timeout || code == Errc::DecodeFailed) return true;
    if (zeroCopy) return false;
    return code == Errc::UnsupportedFormat || code == Errc::UnsupportedFeature;
}

} // namespace aurea::android
