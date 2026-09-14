#include "aurea_enhance.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <mutex>
#include <new>
#include <string>
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

// ---------------------------------------------------------------- DNI
// DEEP NETWORK INTERPOLATION: o "denoise strength" do Real-ESRGAN mistura os
// PESOS de dois modelos com a mesma arquitetura (realesr-general-x4v3, que
// limpa forte, e realesr-general-wdn-x4v3, que preserva o grao). A mistura
// e feita aqui, uma vez, e o ncnn carrega o resultado da memoria.

// Meia precisao IEEE 754 -> float.
float meiaParaFloat(uint16_t h) {
  const unsigned sinal = (h >> 15) & 1u, expoente = (h >> 10) & 0x1fu, mantissa = h & 0x3ffu;
  float v;
  if (expoente == 0) {
    v = std::ldexp(static_cast<float>(mantissa), -24);
  } else if (expoente == 31) {
    v = mantissa ? NAN : INFINITY;
  } else {
    v = std::ldexp(static_cast<float>(mantissa | 0x400u), static_cast<int>(expoente) - 25);
  }
  return sinal ? -v : v;
}

bool lerArquivoInteiro(const char* caminho, std::vector<unsigned char>& out) {
  FILE* f = std::fopen(caminho, "rb");
  if (!f) return false;
  bool ok = false;
  if (std::fseek(f, 0, SEEK_END) == 0) {
    const long n = std::ftell(f);
    if (n > 0 && n < 256L * 1024 * 1024 && std::fseek(f, 0, SEEK_SET) == 0) {
      out.resize(static_cast<size_t>(n));
      ok = std::fread(out.data(), 1, out.size(), f) == out.size();
    }
  }
  std::fclose(f);
  return ok;
}

struct CamadaComPesos {
  bool convolucao = false;  // senao PReLU
  long pesos = 0;           // Convolution: 6=weight_data_size; PReLU: 0=num_slope
  long bias = 0;            // Convolution com 5=1: 0=num_output
};

// So as camadas do SRVGGNetCompact: qualquer outra com pesos e recusada
// (misturar um layout que nao se conhece daria lixo com "sucesso").
bool lerCamadas(const std::vector<unsigned char>& texto, std::vector<CamadaComPesos>& out) {
  std::string s(texto.begin(), texto.end());
  size_t pos = 0;
  int linha = 0;
  while (pos < s.size()) {
    size_t fim = s.find('\n', pos);
    if (fim == std::string::npos) fim = s.size();
    std::string l = s.substr(pos, fim - pos);
    pos = fim + 1;
    std::vector<std::string> partes;
    size_t i = 0;
    while (i < l.size()) {
      while (i < l.size() && (l[i] == ' ' || l[i] == '\t' || l[i] == '\r')) ++i;
      size_t j = i;
      while (j < l.size() && l[j] != ' ' && l[j] != '\t' && l[j] != '\r') ++j;
      if (j > i) partes.push_back(l.substr(i, j - i));
      i = j;
    }
    if (partes.empty()) continue;
    if (linha++ < 2) {
      if (linha == 1 && partes[0] != "7767517") return false;
      continue;
    }
    if (partes.size() < 4) return false;
    const std::string& tipo = partes[0];
    const long entradas = std::strtol(partes[2].c_str(), nullptr, 10);
    const long saidas = std::strtol(partes[3].c_str(), nullptr, 10);
    if (entradas < 0 || saidas < 0 || static_cast<size_t>(4 + entradas + saidas) > partes.size()) return false;
    long k0 = -1, k5 = 0, k6 = -1;
    for (size_t k = static_cast<size_t>(4 + entradas + saidas); k < partes.size(); ++k) {
      const size_t igual = partes[k].find('=');
      if (igual == std::string::npos) return false;
      const long chave = std::strtol(partes[k].substr(0, igual).c_str(), nullptr, 10);
      const long valor = std::strtol(partes[k].substr(igual + 1).c_str(), nullptr, 10);
      if (chave == 0) k0 = valor;
      if (chave == 5) k5 = valor;
      if (chave == 6) k6 = valor;
    }
    if (tipo == "Convolution") {
      if (k0 <= 0 || k6 <= 0) return false;
      CamadaComPesos c;
      c.convolucao = true;
      c.pesos = k6;
      c.bias = k5 == 1 ? k0 : 0;
      out.push_back(c);
    } else if (tipo == "PReLU") {
      CamadaComPesos c;
      c.pesos = k0 > 0 ? k0 : 1;
      out.push_back(c);
    } else if (tipo != "Input" && tipo != "Split" && tipo != "PixelShuffle" &&
               tipo != "Interp" && tipo != "BinaryOp") {
      return false;
    }
  }
  return !out.empty();
}

// Le n floats de [p]: com marca (fp32 = 0 ou fp16) ou crus.
bool lerBloco(const std::vector<unsigned char>& d, size_t& p, long n, bool comMarca,
              std::vector<float>& out) {
  const size_t qtd = static_cast<size_t>(n);
  out.resize(qtd);
  uint32_t marca = 0;
  if (comMarca) {
    if (d.size() - p < 4) return false;
    std::memcpy(&marca, &d[p], 4);
    p += 4;
  }
  if (marca == 0) {
    if (d.size() - p < qtd * 4) return false;
    std::memcpy(out.data(), &d[p], qtd * 4);
    p += qtd * 4;
    return true;
  }
  if (marca == 0x01306B47u) {
    const size_t bytes = (qtd * 2 + 3) / 4 * 4;
    if (d.size() - p < bytes) return false;
    for (size_t i = 0; i < qtd; ++i) {
      uint16_t h;
      std::memcpy(&h, &d[p + 2 * i], 2);
      out[i] = meiaParaFloat(h);
    }
    p += bytes;
    return true;
  }
  return false;  // int8/codebook: nao ha como misturar
}

bool misturarPesos(const std::vector<CamadaComPesos>& camadas, const std::vector<unsigned char>& a,
                   const std::vector<unsigned char>& b, float pesoA,
                   std::vector<unsigned char>& saida) {
  size_t pa = 0, pb = 0;
  std::vector<float> va, vb;
  auto junta = [&](long n, bool comMarca) -> bool {
    if (!lerBloco(a, pa, n, comMarca, va) || !lerBloco(b, pb, n, comMarca, vb)) return false;
    if (comMarca) {
      const uint32_t zero = 0;  // sai sempre em float32
      const size_t at = saida.size();
      saida.resize(at + 4);
      std::memcpy(&saida[at], &zero, 4);
    }
    const size_t at = saida.size();
    saida.resize(at + va.size() * 4);
    for (size_t i = 0; i < va.size(); ++i) {
      const float v = pesoA * va[i] + (1.0f - pesoA) * vb[i];
      std::memcpy(&saida[at + i * 4], &v, 4);
    }
    return true;
  };
  for (const CamadaComPesos& c : camadas) {
    if (c.convolucao) {
      if (!junta(c.pesos, true)) return false;
      if (c.bias > 0 && !junta(c.bias, false)) return false;
    } else if (!junta(c.pesos, false)) {
      return false;
    }
  }
  return pa == a.size() && pb == b.size();
}

}  // namespace

struct ae_engine {
  ncnn::Net net;
  // Pesos misturados (DNI): o ncnn os REFERENCIA, entao vivem com o motor.
  std::vector<unsigned char> pesos;
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
  // bin_path nulo so por dentro (ae_create_dni): prepara o motor sem pesos.
  if (!fileReadable(param_path) || (bin_path && !fileReadable(bin_path))) {
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
  if (bin_path && (e->net.load_param(param_path) != 0 || e->net.load_model(bin_path) != 0)) {
    writeErr(err, err_len, "modelo invalido (param/bin nao carregou)");
    ae_destroy(e);
    return nullptr;
  }
  return e;
}

ae_engine* ae_create_dni(const char* param_path, const char* bin_a, const char* bin_b,
                         float weight_a, int32_t model_scale, int32_t use_gpu, char* err,
                         int32_t err_len) {
  if (!fileReadable(param_path) || !fileReadable(bin_a) || !fileReadable(bin_b)) {
    writeErr(err, err_len, "arquivos do modelo ausentes ou ilegiveis");
    return nullptr;
  }
  if (std::isnan(weight_a) || weight_a < 0 || weight_a > 1) {
    writeErr(err, err_len, "peso da mistura fora de 0..1");
    return nullptr;
  }
  std::vector<unsigned char> texto, a, b;
  std::vector<CamadaComPesos> camadas;
  if (!lerArquivoInteiro(param_path, texto) || !lerCamadas(texto, camadas)) {
    writeErr(err, err_len, "param nao e um SRVGGNetCompact que se saiba misturar");
    return nullptr;
  }
  if (!lerArquivoInteiro(bin_a, a) || !lerArquivoInteiro(bin_b, b)) {
    writeErr(err, err_len, "bin ilegivel");
    return nullptr;
  }
  std::vector<unsigned char> mistura;
  mistura.reserve(std::max(a.size(), b.size()) * 2);
  if (!misturarPesos(camadas, a, b, weight_a, mistura)) {
    writeErr(err, err_len, "os dois modelos nao tem o mesmo formato");
    return nullptr;
  }
  // Mesmo preparo do ae_create, sem carregar o bin do disco.
  ae_engine* e = ae_create(param_path, nullptr, model_scale, use_gpu, err, err_len);
  if (!e) return nullptr;
  e->pesos.swap(mistura);
  if (e->net.load_param(param_path) != 0 ||
      e->net.load_model(e->pesos.data()) != e->pesos.size()) {
    writeErr(err, err_len, "modelo misturado nao carregou");
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
