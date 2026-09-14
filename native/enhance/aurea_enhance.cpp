#include "aurea_enhance.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <mutex>
#include <new>
#include <vector>

#include "aurea_gpu.h"
#include "aurea_png.h"
#include "cpu.h"
#include "gpu.h"
#include "net.h"

namespace {

void writeErr(char* err, int32_t len, const char* msg) {
  if (!err || len <= 0) return;
  std::snprintf(err, static_cast<size_t>(len), "%s", msg);
}

bool fileReadable(const char* path) {
  if (!path) return false;
  FILE* f = std::fopen(path, "rb");
  if (!f) return false;
  std::fclose(f);
  return true;
}

// Catmull-Rom separavel (a = -0.5), bordas replicadas.
double cubicW(double x) {
  x = std::fabs(x);
  if (x < 1) return 1.5 * x * x * x - 2.5 * x * x + 1;
  if (x < 2) return -0.5 * x * x * x + 2.5 * x * x - 4 * x + 2;
  return 0;
}

// Pesos de uma dimensao. Ampliando, e o Catmull-Rom de 4 amostras de
// sempre; REDUZINDO, o nucleo alarga na razao da reducao (sem isso a saida
// x4 da IA reduzida para o tamanho da composicao serrilhava).
struct Janela {
  std::vector<int> primeiro;  // primeira amostra de cada pixel de saida
  std::vector<double> peso;   // taps por pixel de saida, ja normalizados
  int taps = 0;
};

Janela janela(int entrada, int saida) {
  Janela j;
  const double escala = static_cast<double>(entrada) / saida;
  const double f = std::max(1.0, escala);
  const double raio = 2.0 * f;
  j.taps = static_cast<int>(std::ceil(raio)) * 2 + 1;
  j.primeiro.resize(static_cast<size_t>(saida));
  j.peso.assign(static_cast<size_t>(saida) * j.taps, 0.0);
  for (int o = 0; o < saida; ++o) {
    const double centro = (o + 0.5) * escala - 0.5;
    const int p0 = static_cast<int>(std::floor(centro - raio)) + 1;
    j.primeiro[o] = p0;
    double soma = 0;
    for (int k = 0; k < j.taps; ++k) {
      const double wk = cubicW((p0 + k - centro) / f);
      j.peso[static_cast<size_t>(o) * j.taps + k] = wk;
      soma += wk;
    }
    if (soma != 0) {
      for (int k = 0; k < j.taps; ++k) j.peso[static_cast<size_t>(o) * j.taps + k] /= soma;
    }
  }
  return j;
}

void resizeCubic(const uint8_t* src, int w, int h, uint8_t* dst, int ow, int oh) {
  std::vector<float> tmp(static_cast<size_t>(ow) * h * 3);
  const Janela jx = janela(w, ow), jy = janela(h, oh);
  for (int x = 0; x < ow; ++x) {
    const int p0 = jx.primeiro[x];
    const double* wt = &jx.peso[static_cast<size_t>(x) * jx.taps];
    for (int y = 0; y < h; ++y) {
      for (int c = 0; c < 3; ++c) {
        double v = 0;
        for (int k = 0; k < jx.taps; ++k) {
          if (wt[k] == 0) continue;
          const int xx = std::min(w - 1, std::max(0, p0 + k));
          v += wt[k] * src[(static_cast<size_t>(y) * w + xx) * 3 + c];
        }
        tmp[(static_cast<size_t>(y) * ow + x) * 3 + c] = static_cast<float>(v);
      }
    }
  }
  for (int y = 0; y < oh; ++y) {
    const int p0 = jy.primeiro[y];
    const double* wt = &jy.peso[static_cast<size_t>(y) * jy.taps];
    for (int x = 0; x < ow; ++x) {
      for (int c = 0; c < 3; ++c) {
        double v = 0;
        for (int k = 0; k < jy.taps; ++k) {
          if (wt[k] == 0) continue;
          const int yy = std::min(h - 1, std::max(0, p0 + k));
          v += wt[k] * tmp[(static_cast<size_t>(yy) * ow + x) * 3 + c];
        }
        dst[(static_cast<size_t>(y) * ow + x) * 3 + c] =
            static_cast<uint8_t>(std::min(255.0, std::max(0.0, v)) + 0.5);
      }
    }
  }
}

}  // namespace

struct ae_engine {
  ncnn::Net net;
  ncnn::VulkanDevice* vkdev = nullptr;
  int scale = 4;
  int tile = 64;
  int autoTile = 64;
  bool gpu = false;
  int64_t heapMb = 0;
  char device[128] = {0};
  std::mutex lock;  // um quadro por vez por motor
};

extern "C" {

ae_engine* ae_create(const char* param_path, const char* bin_path, int32_t model_scale,
                     int32_t use_gpu, char* err, int32_t err_len) {
  if (!fileReadable(param_path) || !fileReadable(bin_path)) {
    writeErr(err, err_len, "arquivos do modelo ausentes ou ilegiveis");
    return nullptr;
  }
  if (model_scale != 2 && model_scale != 3 && model_scale != 4) {
    writeErr(err, err_len, "escala de modelo invalida");
    return nullptr;
  }
  ae_engine* e = new (std::nothrow) ae_engine();
  if (!e) {
    writeErr(err, err_len, "sem memoria");
    return nullptr;
  }
  e->scale = model_scale;
  if (use_gpu) {
    // Instancia Vulkan contada junto com o RIFE (aurea_gpu.h).
    e->vkdev = aurea_gpu::adquirir();
    e->gpu = e->vkdev != nullptr;
  }
  e->net.opt.use_vulkan_compute = e->gpu;
  e->net.opt.use_fp16_packed = e->gpu;
  e->net.opt.use_fp16_storage = e->gpu;
  e->net.opt.use_fp16_arithmetic = false;
  e->net.opt.num_threads = std::max(1, std::min(4, ncnn::get_big_cpu_count()));
  if (e->gpu) {
    e->net.set_vulkan_device(e->vkdev);
    e->heapMb = static_cast<int64_t>(e->vkdev->get_heap_budget());
    std::snprintf(e->device, sizeof(e->device), "%s",
                  ncnn::get_gpu_info(ncnn::get_default_gpu_index()).device_name());
    // Mesma escada do upstream, medida no host: tiles grandes custam
    // menos tempo, pequenos menos VRAM.
    e->autoTile = e->heapMb > 1900 ? 200 : e->heapMb > 550 ? 100 : e->heapMb > 190 ? 64 : 32;
  } else {
    std::snprintf(e->device, sizeof(e->device), "CPU");
    e->autoTile = 128;
  }
  e->tile = e->autoTile;
  if (e->net.load_param(param_path) != 0 || e->net.load_model(bin_path) != 0) {
    writeErr(err, err_len, "modelo invalido (param/bin nao carregou)");
    ae_destroy(e);
    return nullptr;
  }
  return e;
}

void ae_destroy(ae_engine* e) {
  if (!e) return;
  const bool gpu = e->gpu;
  e->net.clear();
  delete e;
  if (gpu) aurea_gpu::liberar();
}

int32_t ae_info_get(ae_engine* e, ae_info* out) {
  if (!e || !out) return AE_ERR_ARGS;
  std::memset(out, 0, sizeof(*out));
  out->gpu = e->gpu ? 1 : 0;
  out->fp16 = e->net.opt.use_fp16_storage ? 1 : 0;
  out->tile = e->tile;
  out->model_scale = e->scale;
  out->heap_budget_mb = e->heapMb;
  std::snprintf(out->device, sizeof(out->device), "%s", e->device);
  return AE_OK;
}

void ae_set_tile(ae_engine* e, int32_t tile) {
  if (!e) return;
  e->tile = tile <= 0 ? e->autoTile : std::max(32, std::min(512, tile));
}

int32_t ae_resize(const uint8_t* rgb, int32_t w, int32_t h, uint8_t* out, int32_t ow,
                  int32_t oh) {
  if (!rgb || !out || w <= 0 || h <= 0 || ow <= 0 || oh <= 0) return AE_ERR_ARGS;
  resizeCubic(rgb, w, h, out, ow, oh);
  return AE_OK;
}

int32_t ae_process(ae_engine* e, const uint8_t* rgb, int32_t w, int32_t h, int32_t out_scale,
                   float strength, uint8_t* out, const int32_t* cancel) {
  if (!e || !rgb || !out || w <= 0 || h <= 0) return AE_ERR_ARGS;
  if (out_scale != 1 && out_scale != 2 && out_scale != 4) return AE_ERR_ARGS;
  if (out_scale > e->scale) return AE_ERR_ARGS;
  std::lock_guard<std::mutex> g(e->lock);
  const int S = e->scale;
  const int pad = 10;  // contexto do campo receptivo (o mesmo do upstream)
  const int tile = e->tile;
  const int ow = w * S, oh = h * S;
  // Saida da IA na escala nativa, em float, ja com clamp.
  // Sem excecoes (o ncnn compila com -fno-exceptions): alocacao nothrow.
  const size_t bigSize = static_cast<size_t>(ow) * oh * 3;
  std::unique_ptr<float[]> bigBuf(new (std::nothrow) float[bigSize]);
  if (!bigBuf) return AE_ERR_INFERENCE;
  float* const big = bigBuf.get();
  const float norm[3] = {1 / 255.0f, 1 / 255.0f, 1 / 255.0f};
  std::vector<uint8_t> patch;
  for (int ty = 0; ty < h; ty += tile) {
    for (int tx = 0; tx < w; tx += tile) {
      if (cancel && *cancel) return AE_ERR_CANCELLED;
      const int cw = std::min(tile, w - tx), ch = std::min(tile, h - ty);
      const int x0 = std::max(0, tx - pad), y0 = std::max(0, ty - pad);
      const int x1 = std::min(w, tx + cw + pad), y1 = std::min(h, ty + ch + pad);
      const int pw = x1 - x0, ph = y1 - y0;
      patch.resize(static_cast<size_t>(pw) * ph * 3);
      for (int y = 0; y < ph; ++y) {
        std::memcpy(&patch[static_cast<size_t>(y) * pw * 3],
                    &rgb[(static_cast<size_t>(y0 + y) * w + x0) * 3],
                    static_cast<size_t>(pw) * 3);
      }
      ncnn::Mat in = ncnn::Mat::from_pixels(patch.data(), ncnn::Mat::PIXEL_RGB, pw, ph);
      in.substract_mean_normalize(nullptr, norm);
      ncnn::Mat result;
      {
        ncnn::Extractor ex = e->net.create_extractor();
        if (ex.input("data", in) != 0) return AE_ERR_INFERENCE;
        if (ex.extract("output", result) != 0) return AE_ERR_INFERENCE;
      }
      if (result.empty() || result.c != 3 || result.w != pw * S || result.h != ph * S) {
        return AE_ERR_INFERENCE;
      }
      // Copia so o miolo do tile (sem a margem de contexto).
      const int offX = (tx - x0) * S, offY = (ty - y0) * S;
      for (int c = 0; c < 3; ++c) {
        const float* plane = result.channel(c);
        for (int y = 0; y < ch * S; ++y) {
          const float* row = plane + static_cast<size_t>(offY + y) * result.w + offX;
          float* dst = &big[(static_cast<size_t>(ty * S + y) * ow + tx * S) * 3];
          for (int x = 0; x < cw * S; ++x) {
            float v = row[x] * 255.0f;
            // clamp em float ANTES de arredondar (o shader do upstream
            // convertia negativo para uint: indefinido em Adreno/Mali).
            v = v < 0 ? 0 : (v > 255 ? 255 : v);
            dst[x * 3 + c] = v;
          }
        }
      }
    }
  }
  // Reduz para a escala pedida por media de area.
  const int f = S / out_scale;
  const int fw = w * out_scale, fh = h * out_scale;
  std::vector<uint8_t> base;
  const float s = std::min(1.0f, std::max(0.0f, strength));
  if (s < 1.0f) {
    base.resize(static_cast<size_t>(fw) * fh * 3);
    resizeCubic(rgb, w, h, base.data(), fw, fh);
  }
  const float inv = 1.0f / static_cast<float>(f * f);
  for (int y = 0; y < fh; ++y) {
    for (int x = 0; x < fw; ++x) {
      for (int c = 0; c < 3; ++c) {
        float acc = 0;
        for (int yy = 0; yy < f; ++yy) {
          const float* row = &big[(static_cast<size_t>(y * f + yy) * ow + x * f) * 3];
          for (int xx = 0; xx < f; ++xx) acc += row[xx * 3 + c];
        }
        float v = acc * inv;
        const size_t i = (static_cast<size_t>(y) * fw + x) * 3 + c;
        if (s < 1.0f) v = s * v + (1 - s) * base[i];
        out[i] = static_cast<uint8_t>(v + 0.5f);
      }
    }
  }
  return AE_OK;
}

int32_t ae_process_png(ae_engine* e, const char* in_path, const char* out_path, int32_t out_scale,
                       float strength, int32_t fit_w, int32_t fit_h, const int32_t* cancel) {
  if (!e || !in_path || !out_path) return AE_ERR_ARGS;
  if (out_scale != 1 && out_scale != 2 && out_scale != 4) return AE_ERR_ARGS;
  if (fit_w < 0 || fit_h < 0 || fit_w > 8192 || fit_h > 8192) return AE_ERR_ARGS;
  aurea_png::Imagem entrada;
  if (!aurea_png::ler(in_path, &entrada)) return AE_ERR_IO;
  const int w = entrada.w, h = entrada.h;
  if (w > 4096 || h > 4096) return AE_ERR_ARGS;
  const int ow = w * out_scale, oh = h * out_scale;
  std::unique_ptr<uint8_t[]> ia(new (std::nothrow) uint8_t[static_cast<size_t>(ow) * oh * 3]);
  if (!ia) return AE_ERR_INFERENCE;
  const int32_t rc = ae_process(e, entrada.rgb.get(), w, h, out_scale, strength, ia.get(), cancel);
  if (rc != AE_OK) return rc;
  if (fit_w <= 0 || fit_h <= 0 || (fit_w == ow && fit_h == oh)) {
    return aurea_png::gravar(out_path, ia.get(), ow, oh) ? AE_OK : AE_ERR_IO;
  }
  std::unique_ptr<uint8_t[]> final(new (std::nothrow) uint8_t[static_cast<size_t>(fit_w) * fit_h * 3]);
  if (!final) return AE_ERR_INFERENCE;
  resizeCubic(ia.get(), ow, oh, final.get(), fit_w, fit_h);
  return aurea_png::gravar(out_path, final.get(), fit_w, fit_h) ? AE_OK : AE_ERR_IO;
}

}  // extern "C"
