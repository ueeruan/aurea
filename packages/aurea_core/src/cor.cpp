#include "cor.h"

#include <cstring>

namespace aurea {

static inline uint8_t clamp8(int v) { return v < 0 ? 0 : (v > 255 ? 255 : uint8_t(v)); }

static void luma(const uint8_t* rgba, int w, int h, uint8_t* out, int stride) {
  for (int j = 0; j < h; ++j) {
    const uint8_t* p = rgba + size_t(j) * w * 4;
    uint8_t* y = out + size_t(j) * stride;
    for (int i = 0; i < w; ++i, p += 4) {
      y[i] = clamp8(((47 * p[0] + 157 * p[1] + 16 * p[2] + 128) >> 8) + 16);
    }
  }
}

// Cromas pela amostra do canto par, como o caminho em Kotlin.
void rgba_para_nv12(const uint8_t* rgba, int w, int h, uint8_t* out, int stride, int slice) {
  luma(rgba, w, h, out, stride);
  const uint8_t* base = out + size_t(slice) * stride;
  for (int j = 0; j < h; j += 2) {
    const uint8_t* p = rgba + size_t(j) * w * 4;
    uint8_t* uv = const_cast<uint8_t*>(base) + size_t(j / 2) * stride;
    for (int i = 0; i < w; i += 2, p += 8) {
      const int r = p[0], g = p[1], b = p[2];
      *uv++ = clamp8(((-26 * r - 87 * g + 113 * b + 128) >> 8) + 128);
      *uv++ = clamp8(((113 * r - 102 * g - 11 * b + 128) >> 8) + 128);
    }
  }
}

void rgba_para_i420(const uint8_t* rgba, int w, int h, uint8_t* out, int stride, int slice) {
  luma(rgba, w, h, out, stride);
  const int cs = stride / 2;
  uint8_t* us = out + size_t(slice) * stride;
  uint8_t* vs = us + size_t(slice / 2) * cs;
  for (int j = 0; j < h; j += 2) {
    const uint8_t* p = rgba + size_t(j) * w * 4;
    uint8_t* u = us + size_t(j / 2) * cs;
    uint8_t* v = vs + size_t(j / 2) * cs;
    for (int i = 0; i < w; i += 2, p += 8) {
      const int r = p[0], g = p[1], b = p[2];
      *u++ = clamp8(((-26 * r - 87 * g + 113 * b + 128) >> 8) + 128);
      *v++ = clamp8(((113 * r - 102 * g - 11 * b + 128) >> 8) + 128);
    }
  }
}

void rgba_para_yuv420(const uint8_t* rgba, int w, int h, uint8_t* out, PlanoYuv y, PlanoYuv u,
                      PlanoYuv v) {
  for (int j = 0; j < h; ++j) {
    const uint8_t* p = rgba + size_t(j) * w * 4;
    uint8_t* linha = out + y.desloca + size_t(j) * y.passo_linha;
    for (int i = 0; i < w; ++i, p += 4) {
      linha[size_t(i) * y.passo_coluna] =
          clamp8(((47 * p[0] + 157 * p[1] + 16 * p[2] + 128) >> 8) + 16);
    }
  }
  for (int j = 0; j < h; j += 2) {
    const uint8_t* p = rgba + size_t(j) * w * 4;
    uint8_t* lu = out + u.desloca + size_t(j / 2) * u.passo_linha;
    uint8_t* lv = out + v.desloca + size_t(j / 2) * v.passo_linha;
    for (int i = 0; i < w; i += 2, p += 8) {
      const int r = p[0], g = p[1], b = p[2];
      lu[size_t(i / 2) * u.passo_coluna] = clamp8(((-26 * r - 87 * g + 113 * b + 128) >> 8) + 128);
      lv[size_t(i / 2) * v.passo_coluna] = clamp8(((113 * r - 102 * g - 11 * b + 128) >> 8) + 128);
    }
  }
}

}  // namespace aurea
