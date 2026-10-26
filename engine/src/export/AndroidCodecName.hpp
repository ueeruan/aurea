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

/// 0.3.2 (Moto g52, OMX.qcom.video.encoder.avc, Android 13): no OMX da
/// Qualcomm o BITRATE_MODE_VBR do Android vira o VBR com quadros variáveis
/// (VBR_VFR): o controle de taxa DESCARTA quadros, o encoder devolve buffers
/// vazios que o MediaCodec não repassa, e o export para com a fila cheia
/// (medido: VBR e sem modo travam em 1–3 s; CBR exporta os 1800 quadros).
/// Nesses encoders o export pede CBR no lugar do VBR — a mesma taxa alvo —
/// mesmo sem o CBR anunciado (o g52 não anuncia e aceita); se o configure
/// recusar, o encoder abre de novo no modo pedido.
[[nodiscard]] inline bool vbr_drops_frames(const char* name) noexcept {
    return name && std::strncmp(name, "OMX.qcom.", 9) == 0;
}

/// O modo de taxa que o export pede (0 CBR, 1 VBR) a um encoder do aparelho.
[[nodiscard]] inline unsigned device_rate_mode(const char* name, unsigned requested) noexcept {
    return requested == 1 && vbr_drops_frames(name) ? 0u : requested;
}

} // namespace aurea::android_codec
