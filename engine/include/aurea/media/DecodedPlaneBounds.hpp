#pragma once

#include <cstdint>
#include <cstring>
#include <vector>

namespace aurea::media {

// AImage planes need not include padding after the last row. Validate the
// final sample, without treating rowStride * height as readable memory.
inline bool decoded_plane_fits(uint32_t width, uint32_t height, int rowStride,
                               int pixelStride, int length) noexcept {
    if (!width || !height || rowStride <= 0 || pixelStride <= 0 || length <= 0) return false;
    const uint64_t rowBytes = uint64_t(width - 1) * uint64_t(pixelStride) + 1;
    if (rowBytes > uint64_t(rowStride) || rowBytes > uint64_t(length)) return false;
    return uint64_t(height - 1) <= (uint64_t(length) - rowBytes) / uint64_t(rowStride);
}

// Copy only visible samples, never final-row padding or vendor-specific layouts.
// Validate every plane before reading any of them. Output owns its storage.
inline bool copy_decoded_yuv420(uint32_t width, uint32_t height,
        const uint8_t* const data[3], const int row[3], const int pixel[3],
        const int length[3], std::vector<uint8_t>& output) {
    if (!width || !height || width > 16384 || height > 16384) return false;
    const uint32_t cw = (width + 1) / 2, ch = (height + 1) / 2;
    for (int i = 0; i < 3; ++i) {
        if (!data[i] || (pixel[i] != 1 && (i == 0 || pixel[i] != 2)) ||
            !decoded_plane_fits(i ? cw : width, i ? ch : height, row[i], pixel[i], length[i])) return false;
    }
    const size_t yBytes = size_t(width) * height, cBytes = size_t(cw) * ch;
    output.resize(yBytes + 2 * cBytes);
    for (int i = 0; i < 3; ++i) {
        const uint32_t w = i ? cw : width, h = i ? ch : height;
        uint8_t* dst = output.data() + (i ? yBytes + size_t(i - 1) * cBytes : 0);
        for (uint32_t y = 0; y < h; ++y) {
            const uint8_t* src = data[i] + size_t(y) * row[i];
            if (pixel[i] == 1) std::memcpy(dst + size_t(y) * w, src, w);
            else for (uint32_t x = 0; x < w; ++x) dst[size_t(y) * w + x] = src[size_t(x) * 2];
        }
    }
    return true;
}

/// Região visível de um quadro decodificado, em px do buffer.
struct VisibleRegion {
    uint32_t left = 0, top = 0, width = 0, height = 0;
};

/// Folga máxima tratada como ALINHAMENTO do decoder (1080 → 1088, 720 → 736).
inline constexpr uint32_t kDecoderPaddingMax = 64;

// Oppo A94 (Helio P95, PowerVR GM9446): listras verdes/rosas nas bordas e
// faixas de lixo em cima/embaixo em ALGUNS vídeos (os de altura/largura fora
// do múltiplo de 16/32). A sobra do alinhamento do decoder aparecia porque
// cada fonte da região visível era usada sozinha: o crop do AImage (alguns
// decoders o devolvem como o buffer inteiro, 1920×1088), senão o crop do
// formato de saída (KEY_CROP_*), senão o tamanho do container. Agora as três
// se limitam: crop do AImage ∩ crop do formato, e o que passar do tamanho da
// trilha por no máximo `kDecoderPaddingMax` px é sobra de alinhamento e sai.
// Crops incoerentes (interseção vazia) voltam à regra antiga — nunca vazio.
//   imageCrop: AImageCropRect (right/bottom EXCLUSIVOS; right <= left = ausente)
//   formatCrop: KEY_CROP_LEFT/TOP/RIGHT/BOTTOM (INCLUSIVOS; right <= left = ausente)
//   streamW/H: largura/altura da trilha no container (0 = desconhecido)
inline bool visible_region(uint32_t bufW, uint32_t bufH, const int32_t imageCrop[4], const int32_t formatCrop[4],
                           uint32_t streamW, uint32_t streamH, VisibleRegion& out) noexcept {
    out = VisibleRegion{};
    if (!bufW || !bufH || bufW > 65536 || bufH > 65536) return false;
    const int64_t W = bufW, H = bufH;
    const bool haveImage = imageCrop && imageCrop[2] > imageCrop[0] && imageCrop[3] > imageCrop[1];
    const bool haveFormat = formatCrop && formatCrop[2] > formatCrop[0] && formatCrop[3] > formatCrop[1];
    int64_t l = 0, t = 0, r = W, b = H;
    if (haveImage) { l = imageCrop[0]; t = imageCrop[1]; r = imageCrop[2]; b = imageCrop[3]; }
    if (haveFormat) {
        const int64_t fl = formatCrop[0], ft = formatCrop[1];
        const int64_t fr = int64_t(formatCrop[2]) + 1, fb = int64_t(formatCrop[3]) + 1;
        const int64_t il = l > fl ? l : fl, it = t > ft ? t : ft;
        const int64_t ir = r < fr ? r : fr, ib = b < fb ? b : fb;
        if (ir > il && ib > it && il < W && it < H) { l = il; t = it; r = ir; b = ib; }
        else if (!haveImage) { l = fl; t = ft; r = fr; b = fb; }
    }
    l = l < 0 ? 0 : (l >= W ? 0 : l);
    t = t < 0 ? 0 : (t >= H ? 0 : t);
    r = r > W ? W : r;
    b = b > H ? H : b;
    if (r <= l) { l = 0; r = W; }
    if (b <= t) { t = 0; b = H; }
    if (!haveImage && !haveFormat) {
        // Nenhum crop: o tamanho da trilha manda (a regra de sempre).
        if (streamW && int64_t(streamW) < r) r = streamW;
        if (streamH && int64_t(streamH) < b) b = streamH;
    } else {
        // Sobra de alinhamento além do tamanho da trilha (só a folga pequena:
        // uma trilha com tamanho estranho no container não corta imagem de verdade).
        if (streamW && r - l > int64_t(streamW) && r - l - int64_t(streamW) <= int64_t(kDecoderPaddingMax)) r = l + streamW;
        if (streamH && b - t > int64_t(streamH) && b - t - int64_t(streamH) <= int64_t(kDecoderPaddingMax)) b = t + streamH;
    }
    out.left = uint32_t(l);
    out.top = uint32_t(t);
    out.width = uint32_t(r - l);
    out.height = uint32_t(b - t);
    return true;
}

} // namespace aurea::media
