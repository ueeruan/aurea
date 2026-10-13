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

// O caminho padrão do vídeo no Android é o GL do DRIVER (AndroidVideoPath.hpp),
// em toda marca e GPU. As decisões por fabricante abaixo valem só quando o
// backend não importa AHardwareBuffer RGBA (GLES) e no modo seguro. O antigo
// zero-copy com YCbCr externo no Vulkan saiu: era o trecho dependente de
// driver — preview piscando no Immortalis-G715 (Xiaomi 13T Pro), listras nas
// bordas no PowerVR GM9446 (Oppo A94), crash em Samsung, bytes crus no
// gfxstream do emulador.

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

// O decoder de ÁUDIO de software do AOSP para o tipo (export compatível,
// refeito depois de o decoder do aparelho travar no mix). nullptr = sem um
// conhecido; quem chama usa o decoder padrão do tipo.
inline const char* software_audio_decoder(std::string_view mime, bool legacy = false) noexcept {
    if (mime == "audio/mp4a-latm") return legacy ? "OMX.google.aac.decoder" : "c2.android.aac.decoder";
    if (mime == "audio/mpeg") return legacy ? "OMX.google.mp3.decoder" : "c2.android.mp3.decoder";
    if (mime == "audio/opus") return legacy ? "OMX.google.opus.decoder" : "c2.android.opus.decoder";
    if (mime == "audio/vorbis") return legacy ? "OMX.google.vorbis.decoder" : "c2.android.vorbis.decoder";
    if (mime == "audio/flac") return legacy ? "OMX.google.flac.decoder" : "c2.android.flac.decoder";
    if (mime == "audio/3gpp") return legacy ? "OMX.google.amrnb.decoder" : "c2.android.amrnb.decoder";
    if (mime == "audio/amr-wb") return legacy ? "OMX.google.amrwb.decoder" : "c2.android.amrwb.decoder";
    if (mime == "audio/g711-alaw") return legacy ? "OMX.google.g711.alaw.decoder" : "c2.android.g711.alaw.decoder";
    if (mime == "audio/g711-mlaw") return legacy ? "OMX.google.g711.mlaw.decoder" : "c2.android.g711.mlaw.decoder";
    if (mime == "audio/raw") return legacy ? "OMX.google.raw.decoder" : "c2.android.raw.decoder";
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
