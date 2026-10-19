// =============================================================================
//  Aurea / export / AndroidCodecName.hpp
//
//  Android 8 e 9 (API 26–28) não têm MediaCodecInfo.isHardwareAccelerated /
//  isSoftwareOnly (API 29). Antes da 0.2.6 a busca de encoder desistia nesses
//  sistemas e o export ia direto para o encoder de software (lento, e o único
//  degrau). Lá, a classificação é a de sempre do próprio Android: o nome.
//  Função pura: o teste do host confere os nomes.
// =============================================================================
#pragma once

#include <cstring>

namespace aurea::android_codec {

/// Codec de SOFTWARE pelo nome (AOSP OMX/Codec2 e FFmpeg embutido no sistema).
/// Nome vazio conta como software (nunca promove um desconhecido a hardware).
[[nodiscard]] inline bool software_by_name(const char* name) noexcept {
    if (!name || !*name) return true;
    const auto starts = [&](const char* prefix) { return std::strncmp(name, prefix, std::strlen(prefix)) == 0; };
    if (starts("OMX.google.") || starts("c2.android.") || starts("c2.google.") || starts("OMX.ffmpeg.") ||
        starts("c2.ffmpeg.") || starts("OMX.SEC.sw.")) return true;
    if (std::strstr(name, ".sw.") != nullptr) return true;
    const std::size_t n = std::strlen(name);
    return n > 3 && std::strcmp(name + n - 3, ".sw") == 0;
}

} // namespace aurea::android_codec
