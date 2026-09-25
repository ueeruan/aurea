#pragma once

#include "aurea/core/Types.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <vector>

namespace aurea::ai {
// Export produces SDR BT.709 limited-range NV12. The neural model consumes
// nonlinear RGB [0,255], not linear-light renderer values or YCbCr planes.
inline u8 rgb_byte(float value) noexcept {
    return static_cast<u8>(std::clamp(std::lround(value), 0l, 255l));
}
inline void nv12_to_rgb709(const u8* y, const u8* uv, u32 width, u32 height, u8* rgb) noexcept {
    for (u32 row = 0; row < height; ++row) for (u32 col = 0; col < width; ++col) {
        const float l = (y[static_cast<usize>(row) * width + col] - 16.0f) * (255.0f / 219.0f);
        const usize chroma = static_cast<usize>(row / 2) * width + (col & ~1u);
        const float cb = (uv[chroma] - 128.0f) * (255.0f / 224.0f);
        const float cr = (uv[chroma + 1] - 128.0f) * (255.0f / 224.0f);
        u8* out = rgb + (static_cast<usize>(row) * width + col) * 3;
        out[0] = rgb_byte(l + 1.5748f * cr);
        out[1] = rgb_byte(l - 0.187324f * cb - 0.468124f * cr);
        out[2] = rgb_byte(l + 1.8556f * cb);
    }
}
inline void nv12_to_rgb709(const u8* y, const u8* uv, u32 width, u32 height, std::vector<u8>& rgb) {
    rgb.resize(static_cast<usize>(width) * height * 3);
    nv12_to_rgb709(y, uv, width, height, rgb.data());
}

// Tiles from a 2x/4x network have even origins and extents, so each chroma
// sample belongs to exactly one tile; no seam-specific averaging is needed.
inline bool rgb_tile_to_nv12_709(const u8* rgb, u32 stride, u32 x, u32 y, u32 width, u32 height,
                                u32 outputWidth, u32 outputHeight, u8* nv12) noexcept {
    if (!rgb || !nv12 || ((x | y | width | height | outputWidth | outputHeight) & 1u) ||
        x > outputWidth || width > outputWidth - x || y > outputHeight || height > outputHeight - y ||
        static_cast<u64>(stride) < static_cast<u64>(width) * 3) return false;
    u8* uv = nv12 + static_cast<usize>(outputWidth) * outputHeight;
    for (u32 row = 0; row < height; row += 2) for (u32 col = 0; col < width; col += 2) {
        float cb = 0, cr = 0;
        for (u32 dy = 0; dy < 2; ++dy) for (u32 dx = 0; dx < 2; ++dx) {
            const u8* p = rgb + static_cast<usize>(row + dy) * stride + (col + dx) * 3;
            const float l = .2126f * p[0] + .7152f * p[1] + .0722f * p[2];
            nv12[static_cast<usize>(y + row + dy) * outputWidth + x + col + dx] = rgb_byte(16 + l * (219.0f / 255.0f));
            cb += (p[2] - l) / 1.8556f;
            cr += (p[0] - l) / 1.5748f;
        }
        const usize at = static_cast<usize>((y + row) / 2) * outputWidth + x + col;
        uv[at] = rgb_byte(128 + cb * (224.0f / (4 * 255.0f)));
        uv[at + 1] = rgb_byte(128 + cr * (224.0f / (4 * 255.0f)));
    }
    return true;
}

// The renderer fills the complete rounded neural input, so its enlarged output
// must be fitted, never cropped. Bilinear pixel-center sampling preserves the
// complete frame and resamples interleaved chroma on its own half-size grid.
// In-place shrinking is supported: forward writes always precede unread source
// samples, including UV compaction after the entire Y plane. No allocation.
inline bool resize_nv12_709(const u8* source, u32 sourceWidth, u32 sourceHeight,
                            u8* destination, u32 width, u32 height) noexcept {
    if (!source || !destination || !sourceWidth || !sourceHeight || !width || !height ||
        ((sourceWidth | sourceHeight | width | height) & 1u)) return false;
    const u64 sourcePixels = static_cast<u64>(sourceWidth) * sourceHeight;
    const u64 outputPixels = static_cast<u64>(width) * height;
    const u64 maxPixels = static_cast<u64>(std::numeric_limits<usize>::max() / 3) * 2;
    if (sourcePixels > maxPixels || outputPixels > maxPixels) return false;
    if (sourceWidth == width && sourceHeight == height) {
        if (source != destination) std::memcpy(destination, source, static_cast<usize>(sourcePixels * 3 / 2));
        return true;
    }
    if (source == destination && (width > sourceWidth || height > sourceHeight)) return false;
    auto plane = [](const u8* src, u32 sw, u32 sh, u8* dst, u32 dw, u32 dh, u32 channels) {
        const f64 scaleX = static_cast<f64>(sw) / dw, scaleY = static_cast<f64>(sh) / dh;
        for (u32 y = 0; y < dh; ++y) {
            const f64 sy = std::clamp((y + 0.5) * scaleY - 0.5, 0.0, static_cast<f64>(sh - 1));
            const u32 y0 = static_cast<u32>(sy), y1 = std::min(y0 + 1, sh - 1);
            const f64 fy = sy - y0;
            for (u32 x = 0; x < dw; ++x) {
                const f64 sx = std::clamp((x + 0.5) * scaleX - 0.5, 0.0, static_cast<f64>(sw - 1));
                const u32 x0 = static_cast<u32>(sx), x1 = std::min(x0 + 1, sw - 1);
                const f64 fx = sx - x0;
                for (u32 c = 0; c < channels; ++c) {
                    const f64 a = src[(static_cast<usize>(y0) * sw + x0) * channels + c];
                    const f64 b = src[(static_cast<usize>(y0) * sw + x1) * channels + c];
                    const f64 d = src[(static_cast<usize>(y1) * sw + x0) * channels + c];
                    const f64 e = src[(static_cast<usize>(y1) * sw + x1) * channels + c];
                    dst[(static_cast<usize>(y) * dw + x) * channels + c] =
                        rgb_byte(static_cast<f32>((a + (b - a) * fx) * (1 - fy) + (d + (e - d) * fx) * fy));
                }
            }
        }
    };
    plane(source, sourceWidth, sourceHeight, destination, width, height, 1);
    plane(source + static_cast<usize>(sourcePixels), sourceWidth / 2, sourceHeight / 2,
          destination + static_cast<usize>(outputPixels), width / 2, height / 2, 2);
    return true;
}
} // namespace aurea::ai
