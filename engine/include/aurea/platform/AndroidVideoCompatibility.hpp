#pragma once

#include "aurea/core/Result.hpp"

#include <string_view>

namespace aurea::android {

// Samsung vendor CPU mappings also fault in planes_from and texture upload.
// Use the platform software decoder before producing the first frame.
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

inline bool needs_software_video(std::string_view manufacturer, int sdk, bool safeMode = false) noexcept {
    return safeMode || needs_readable_video_planes(manufacturer, sdk);
}

// Explicit allowlist: never silently return to a vendor decoder in safe mode.
inline const char* software_video_decoder(std::string_view mime, bool legacy = false) noexcept {
    if (mime == "video/avc") return legacy ? "OMX.google.h264.decoder" : "c2.android.avc.decoder";
    if (mime == "video/hevc") return legacy ? "OMX.google.hevc.decoder" : "c2.android.hevc.decoder";
    if (mime == "video/x-vnd.on2.vp8") return legacy ? "OMX.google.vp8.decoder" : "c2.android.vp8.decoder";
    if (mime == "video/x-vnd.on2.vp9") return legacy ? "OMX.google.vp9.decoder" : "c2.android.vp9.decoder";
    if (mime == "video/mp4v-es") return legacy ? "OMX.google.mpeg4.decoder" : "c2.android.mpeg4.decoder";
    if (mime == "video/3gpp") return legacy ? "OMX.google.h263.decoder" : "c2.android.h263.decoder";
    if (mime == "video/av01") return legacy ? nullptr : "c2.android.av1.decoder";
    return nullptr;
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
