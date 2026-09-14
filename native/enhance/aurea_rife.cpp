#include "aurea_rife.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <mutex>
#include <new>
#include <string>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include "aurea_gpu.h"
#include "aurea_png.h"
#include "cpu.h"
#include "gpu.h"
#include "net.h"
#include "rife.h"

namespace {

void writeErr(char* err, int32_t len, const char* msg) {
  if (!err || len <= 0) return;
  std::snprintf(err, static_cast<size_t>(len), "%s", msg);
}

#if defined(_WIN32)
std::wstring largo(const std::string& s) {
  if (s.empty()) return std::wstring();
  const int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), nullptr, 0);
  std::wstring w(static_cast<size_t>(n), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), &w[0], n);
  return w;
}
#endif

// Um quadro decodificado, lembrado pelo caminho.
struct Quadro {
  std::string caminho;
  aurea_png::Imagem imagem;
};

}  // namespace

struct ar_engine {
  RIFE* rife = nullptr;
  bool gpu = false;
  bool fp16 = false;
  bool int8 = false;
  int64_t heapMb = 0;
  char device[128] = {0};
  std::mutex lock;  // um quadro por vez por motor
  Quadro cache[2];
};

namespace {

int32_t interpolarTravado(ar_engine* e, const uint8_t* a, const uint8_t* b, int w, int h, float t,
                          uint8_t* out) {
#if defined(_WIN32)
  const size_t n = static_cast<size_t>(w) * static_cast<size_t>(h) * 3;
  // rife.cpp no Windows le e escreve BGR (convencao do WIC do upstream).
  std::unique_ptr<uint8_t[]> ba(new (std::nothrow) uint8_t[n]);
  std::unique_ptr<uint8_t[]> bb(new (std::nothrow) uint8_t[n]);
  if (!ba || !bb) return AR_ERR_INFERENCE;
  for (size_t i = 0; i < n; i += 3) {
    ba[i] = a[i + 2];
    ba[i + 1] = a[i + 1];
    ba[i + 2] = a[i];
    bb[i] = b[i + 2];
    bb[i + 1] = b[i + 1];
    bb[i + 2] = b[i];
  }
  const ncnn::Mat in0(w, h, static_cast<void*>(ba.get()), static_cast<size_t>(3u), 1);
  const ncnn::Mat in1(w, h, static_cast<void*>(bb.get()), static_cast<size_t>(3u), 1);
#else
  const ncnn::Mat in0(w, h, const_cast<void*>(static_cast<const void*>(a)), static_cast<size_t>(3u), 1);
  const ncnn::Mat in1(w, h, const_cast<void*>(static_cast<const void*>(b)), static_cast<size_t>(3u), 1);
#endif
  ncnn::Mat saida(w, h, static_cast<void*>(out), static_cast<size_t>(3u), 1);
  if (e->rife->process(in0, in1, t, saida) != 0) return AR_ERR_INFERENCE;
#if defined(_WIN32)
  for (size_t i = 0; i < n; i += 3) std::swap(out[i], out[i + 2]);
#endif
  return AR_OK;
}

// Quadro do cache ou lido agora. [manter] nao pode ser despejado (e o
// outro lado do par em andamento).
const Quadro* quadro(ar_engine* e, const std::string& caminho, const Quadro* manter) {
  for (Quadro& q : e->cache) {
    if (q.imagem.rgb && q.caminho == caminho) return &q;
  }
  aurea_png::Imagem lida;
  if (!aurea_png::ler(caminho, &lida)) return nullptr;
  Quadro* vaga = &e->cache[0];
  if (vaga == manter || (!e->cache[1].imagem.rgb && manter != &e->cache[1])) vaga = &e->cache[1];
  vaga->caminho = caminho;
  vaga->imagem = std::move(lida);
  return vaga;
}

}  // namespace

extern "C" {

ar_engine* ar_create(const char* model_dir, int32_t use_gpu, char* err, int32_t err_len) {
  if (!model_dir || !*model_dir) {
    writeErr(err, err_len, "pasta do modelo vazia");
    return nullptr;
  }
  std::string dir(model_dir);
  while (dir.size() > 1 && (dir.back() == '/' || dir.back() == '\\')) dir.pop_back();
  if (!aurea_png::legivel(dir + "/flownet.param") || !aurea_png::legivel(dir + "/flownet.bin")) {
    writeErr(err, err_len, "arquivos do modelo ausentes ou ilegiveis");
    return nullptr;
  }
  ar_engine* e = new (std::nothrow) ar_engine();
  if (!e) {
    writeErr(err, err_len, "sem memoria");
    return nullptr;
  }
  int gpuid = -1;
  if (use_gpu) {
    int indice = 0;
    ncnn::VulkanDevice* dev = aurea_gpu::adquirir(&indice);
    if (!dev) {
      delete e;
      writeErr(err, err_len, "sem GPU Vulkan");
      return nullptr;
    }
    e->gpu = true;
    gpuid = indice;
    e->fp16 = dev->info.support_fp16_storage();
    e->int8 = dev->info.support_int8_storage();
    e->heapMb = static_cast<int64_t>(dev->get_heap_budget());
    std::snprintf(e->device, sizeof(e->device), "%s", ncnn::get_gpu_info(indice).device_name());
  } else {
    std::snprintf(e->device, sizeof(e->device), "CPU");
  }
  const int threads = std::max(1, std::min(4, ncnn::get_big_cpu_count()));
  // tta, tta temporal e uhd desligados; v4 ligado (so flownet).
  e->rife = new (std::nothrow) RIFE(gpuid, false, false, false, threads, false, true);
  if (!e->rife) {
    ar_destroy(e);
    writeErr(err, err_len, "sem memoria");
    return nullptr;
  }
#if defined(_WIN32)
  const int r = e->rife->load(largo(dir));
#else
  const int r = e->rife->load(dir);
#endif
  if (r != 0) {
    ar_destroy(e);
    writeErr(err, err_len, use_gpu ? "modelo nao carregou na GPU" : "modelo invalido");
    return nullptr;
  }
  return e;
}

void ar_destroy(ar_engine* e) {
  if (!e) return;
  // A rede (e os pipelines da GPU) saem antes da instancia Vulkan.
  delete e->rife;
  e->rife = nullptr;
  const bool gpu = e->gpu;
  delete e;
  if (gpu) aurea_gpu::liberar();
}

int32_t ar_info_get(ar_engine* e, ar_info* out) {
  if (!e || !out) return AR_ERR_ARGS;
  std::memset(out, 0, sizeof(*out));
  out->gpu = e->gpu ? 1 : 0;
  out->fp16 = e->fp16 ? 1 : 0;
  out->int8 = e->int8 ? 1 : 0;
  out->heap_budget_mb = e->heapMb;
  std::snprintf(out->device, sizeof(out->device), "%s", e->device);
  return AR_OK;
}

int32_t ar_interpolate(ar_engine* e, const uint8_t* a, const uint8_t* b, int32_t w, int32_t h,
                       float t, uint8_t* out) {
  if (!e || !e->rife || !a || !b || !out || w <= 0 || h <= 0) return AR_ERR_ARGS;
  if (w > 8192 || h > 8192 || std::isnan(t)) return AR_ERR_ARGS;
  const size_t n = static_cast<size_t>(w) * static_cast<size_t>(h) * 3;
  if (t <= 0) {
    std::memcpy(out, a, n);
    return AR_OK;
  }
  if (t >= 1) {
    std::memcpy(out, b, n);
    return AR_OK;
  }
  std::lock_guard<std::mutex> g(e->lock);
  return interpolarTravado(e, a, b, w, h, t, out);
}

int32_t ar_interpolate_png(ar_engine* e, const char* a_path, const char* b_path, float t,
                           const char* out_path) {
  if (!e || !e->rife || !a_path || !b_path || !out_path || std::isnan(t)) return AR_ERR_ARGS;
  std::lock_guard<std::mutex> g(e->lock);
  const Quadro* qa = quadro(e, a_path, nullptr);
  if (!qa) return AR_ERR_IO;
  const Quadro* qb = quadro(e, b_path, qa);
  if (!qb) return AR_ERR_IO;
  const aurea_png::Imagem& ia = qa->imagem;
  const aurea_png::Imagem& ib = qb->imagem;
  if (ia.w != ib.w || ia.h != ib.h || ia.w > 8192 || ia.h > 8192) return AR_ERR_IO;
  const int w = ia.w, h = ia.h;
  const size_t n = static_cast<size_t>(w) * static_cast<size_t>(h) * 3;
  std::unique_ptr<uint8_t[]> px(new (std::nothrow) uint8_t[n]);
  if (!px) return AR_ERR_INFERENCE;
  if (t <= 0) {
    std::memcpy(px.get(), ia.rgb.get(), n);
  } else if (t >= 1) {
    std::memcpy(px.get(), ib.rgb.get(), n);
  } else {
    const int32_t rc = interpolarTravado(e, ia.rgb.get(), ib.rgb.get(), w, h, t, px.get());
    if (rc != AR_OK) return rc;
  }
  return aurea_png::gravar(out_path, px.get(), w, h) ? AR_OK : AR_ERR_IO;
}

}  // extern "C"
