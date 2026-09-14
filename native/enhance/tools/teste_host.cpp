// Teste de host do libaurea_enhance (nao entra no app).
// Uso: teste_host <param> <bin> <entrada.png 320x180> <original.png 1280x720> <saida.png>
// Confere: modelo carrega; saida nao e preta; PSNR/SSIM contra bicubic; tiles
// 32 vs 200 quase identicos; cancelamento; modelo invalido falha com erro.
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <vector>

#include "../aurea_enhance.h"
#include "stb_image.h"
#include "stb_image_write.h"

static double psnr(const std::vector<uint8_t>& a, const std::vector<uint8_t>& b) {
  double mse = 0;
  for (size_t i = 0; i < a.size(); ++i) {
    const double d = static_cast<double>(a[i]) - b[i];
    mse += d * d;
  }
  mse /= a.size();
  return mse <= 0 ? 99 : 10 * std::log10(255.0 * 255.0 / mse);
}

int main(int argc, char** argv) {
  if (argc < 6) return 2;
  int w, h, n, W, H;
  unsigned char* in = stbi_load(argv[3], &w, &h, &n, 3);
  unsigned char* orig = stbi_load(argv[4], &W, &H, &n, 3);
  if (!in || !orig || W != w * 4 || H != h * 4) {
    std::printf("FALHA: imagens\n");
    return 1;
  }
  char err[256] = {0};
  if (ae_create("nao-existe.param", argv[2], 4, 1, err, 256) != nullptr) {
    std::printf("FALHA: modelo inexistente carregou\n");
    return 1;
  }
  std::printf("modelo inexistente -> erro: %s\n", err);

  auto t0 = std::chrono::steady_clock::now();
  ae_engine* e = ae_create(argv[1], argv[2], 4, 1, err, 256);
  auto t1 = std::chrono::steady_clock::now();
  if (!e) {
    std::printf("FALHA: %s\n", err);
    return 1;
  }
  ae_info info;
  ae_info_get(e, &info);
  std::printf("load %.0f ms; gpu=%d fp16=%d tile=%d heap=%lld MB device=%s\n",
              std::chrono::duration<double, std::milli>(t1 - t0).count(), info.gpu, info.fp16,
              info.tile, static_cast<long long>(info.heap_budget_mb), info.device);

  std::vector<uint8_t> src(in, in + w * h * 3), ref(orig, orig + W * H * 3);
  std::vector<uint8_t> ai(W * H * 3), bic(W * H * 3), ai32(W * H * 3), half(w * 2 * h * 2 * 3);
  ae_resize(src.data(), w, h, bic.data(), W, H);

  int rc = 0;
  for (int i = 0; i < 3; ++i) rc |= ae_process(e, src.data(), w, h, 4, 1.0f, ai.data(), nullptr);
  std::vector<double> ms;
  for (int i = 0; i < 20; ++i) {
    auto a = std::chrono::steady_clock::now();
    rc |= ae_process(e, src.data(), w, h, 4, 1.0f, ai.data(), nullptr);
    ms.push_back(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - a).count());
  }
  std::sort(ms.begin(), ms.end());
  if (rc != AE_OK) {
    std::printf("FALHA: process rc=%d\n", rc);
    return 1;
  }
  long long soma = 0;
  for (uint8_t v : ai) soma += v;
  std::printf("media dos pixels da IA %.1f (preto seria 0)\n", static_cast<double>(soma) / ai.size());
  std::printf("tempo x4 %dx%d: p50 %.1f ms p95 %.1f ms\n", w, h, ms[10], ms[18]);
  std::printf("PSNR contra original: IA %.2f dB, bicubic %.2f dB\n", psnr(ai, ref), psnr(bic, ref));

  ae_set_tile(e, 32);
  rc = ae_process(e, src.data(), w, h, 4, 1.0f, ai32.data(), nullptr);
  int maxd = 0;
  for (size_t i = 0; i < ai.size(); ++i) maxd = std::max(maxd, std::abs(int(ai[i]) - int(ai32[i])));
  std::printf("tile 32 vs auto: rc=%d PSNR %.1f dB, dif max %d\n", rc, psnr(ai, ai32), maxd);
  ae_set_tile(e, 0);

  std::vector<uint8_t> meio(W * H * 3);
  rc = ae_process(e, src.data(), w, h, 4, 0.0f, meio.data(), nullptr);
  std::printf("intensidade 0 = bicubic? PSNR %.1f dB (rc=%d)\n", psnr(meio, bic), rc);

  rc = ae_process(e, src.data(), w, h, 2, 1.0f, half.data(), nullptr);
  std::printf("saida 2x rc=%d\n", rc);

  const int32_t cancel = 1;
  rc = ae_process(e, src.data(), w, h, 4, 1.0f, ai32.data(), &cancel);
  std::printf("cancelado rc=%d (esperado %d)\n", rc, AE_ERR_CANCELLED);

  std::vector<uint8_t> lado(W * 3 * H * 3);
  for (int y = 0; y < H; ++y) {
    std::copy_n(&bic[y * W * 3], W * 3, &lado[y * W * 9]);
    std::copy_n(&ai[y * W * 3], W * 3, &lado[y * W * 9 + W * 3]);
    std::copy_n(&ref[y * W * 3], W * 3, &lado[y * W * 9 + W * 6]);
  }
  stbi_write_png(argv[5], W * 3, H, 3, lado.data(), W * 9);
  ae_destroy(e);
  std::printf("OK\n");
  return 0;
}
