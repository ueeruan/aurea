// CODIFICADOR DE VIDEO DO NUCLEO (Android): MediaCodec + MediaMuxer pelo NDK.
//
// O caminho antigo, por quadro: bytes pelo canal de plataforma (copias no
// codec do Dart, no motor e num byte[] novo), uma thread Java nova, a
// conversao RGBA->NV12 num laco da JVM e a copia para o codificador. Aqui o
// Dart entrega o ponteiro do quadro (FFI, sem canal), o nucleo copia uma vez
// para um buffer do proprio pool e devolve; uma thread C++ converte, entrega
// ao MediaCodec e escreve no MediaMuxer. Renderizar o proximo quadro e
// codificar este passam a acontecer AO MESMO TEMPO, com fila de dois quadros
// segurando a memoria.
#include "cor.h"

#include <android/log.h>
#include <fcntl.h>
#include <media/NdkMediaCodec.h>
#include <media/NdkMediaFormat.h>
#include <media/NdkMediaMuxer.h>
#include <dlfcn.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#define LOG(...) __android_log_print(ANDROID_LOG_INFO, "aurea_core", __VA_ARGS__)

namespace {

constexpr int32_t kNV12 = 21;  // COLOR_FormatYUV420SemiPlanar
constexpr int32_t kI420 = 19;  // COLOR_FormatYUV420Planar
// COLOR_FormatYUV420Flexible: o que os codificadores Codec2 aceitam (e o que o
// caminho em Kotlin pede). O arranjo real sai do formato de entrada.
constexpr int32_t kFlexivel = 0x7F420888;
constexpr size_t kFila = 2;

struct Codificador {
  AMediaCodec* codec = nullptr;
  AMediaMuxer* muxer = nullptr;
  int fd = -1;
  int w = 0, h = 0, fps = 30;
  int32_t cor = kNV12;
  int stride = 0, slice = 0;
  // Arranjo lido do "image-data" (MediaImage2); vale quando `planos`.
  bool planos = false;
  aurea::PlanoYuv py{}, pu{}, pv{};
  ssize_t trilha = -1;
  bool muxando = false;
  int64_t quadro = 0;

  std::mutex m;
  std::condition_variable cv;
  std::deque<std::vector<uint8_t>> fila;
  std::vector<std::vector<uint8_t>> livres;
  bool fechar = false;
  std::atomic<bool> falhou{false};
  std::thread trabalhador;

  bool drenar(bool fim) {
    AMediaCodecBufferInfo info;
    for (;;) {
      ssize_t i = AMediaCodec_dequeueOutputBuffer(codec, &info, fim ? 10000 : 0);
      if (i == AMEDIACODEC_INFO_TRY_AGAIN_LATER) {
        if (!fim) return true;
        continue;
      }
      if (i == AMEDIACODEC_INFO_OUTPUT_FORMAT_CHANGED) {
        AMediaFormat* f = AMediaCodec_getOutputFormat(codec);
        trilha = AMediaMuxer_addTrack(muxer, f);
        AMediaFormat_delete(f);
        if (trilha < 0 || AMediaMuxer_start(muxer) != AMEDIA_OK) return false;
        muxando = true;
        continue;
      }
      if (i == AMEDIACODEC_INFO_OUTPUT_BUFFERS_CHANGED) continue;
      if (i < 0) return false;
      size_t tam = 0;
      uint8_t* dados = AMediaCodec_getOutputBuffer(codec, size_t(i), &tam);
      const bool config = info.flags & AMEDIACODEC_BUFFER_FLAG_CODEC_CONFIG;
      if (dados && info.size > 0 && !config && muxando) {
        AMediaMuxer_writeSampleData(muxer, size_t(trilha), dados, &info);
      }
      AMediaCodec_releaseOutputBuffer(codec, size_t(i), false);
      if (info.flags & AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM) return true;
    }
  }

  bool codificar(const std::vector<uint8_t>& rgba) {
    ssize_t i = -1;
    for (int t = 0; t < 400 && i < 0; ++t) {
      i = AMediaCodec_dequeueInputBuffer(codec, 5000);
      if (i < 0 && !drenar(false)) return false;
    }
    if (i < 0) return false;
    size_t cap = 0;
    uint8_t* buf = AMediaCodec_getInputBuffer(codec, size_t(i), &cap);
    size_t precisa = size_t(stride) * size_t(slice) * 3 / 2;
    if (planos) {
      // O fim do plano mais distante: o buffer tem de chegar ate ele.
      auto fim = [&](const aurea::PlanoYuv& q, int pw, int ph) {
        return size_t(q.desloca) + size_t(ph - 1) * q.passo_linha + size_t(pw - 1) * q.passo_coluna + 1;
      };
      precisa = std::max({fim(py, w, h), fim(pu, w / 2, h / 2), fim(pv, w / 2, h / 2)});
    }
    if (!buf || cap < precisa) return false;
    if (planos) {
      aurea::rgba_para_yuv420(rgba.data(), w, h, buf, py, pu, pv);
    } else if (cor == kNV12) {
      aurea::rgba_para_nv12(rgba.data(), w, h, buf, stride, slice);
    } else {
      aurea::rgba_para_i420(rgba.data(), w, h, buf, stride, slice);
    }
    const uint64_t pts = uint64_t(quadro++) * 1000000ULL / uint64_t(fps);
    if (AMediaCodec_queueInputBuffer(codec, size_t(i), 0, precisa, pts, 0) != AMEDIA_OK) return false;
    return drenar(false);
  }

  void laco() {
    for (;;) {
      std::vector<uint8_t> q;
      {
        std::unique_lock<std::mutex> l(m);
        cv.wait(l, [&] { return fechar || !fila.empty(); });
        if (fila.empty()) return;
        q = std::move(fila.front());
        fila.pop_front();
      }
      if (!falhou && !codificar(q)) falhou = true;
      {
        std::lock_guard<std::mutex> l(m);
        livres.push_back(std::move(q));
      }
      cv.notify_all();
    }
  }

  void soltar(bool apagar, const std::string& caminho) {
    {
      std::lock_guard<std::mutex> l(m);
      fechar = true;
    }
    cv.notify_all();
    if (trabalhador.joinable()) trabalhador.join();
    if (codec) {
      AMediaCodec_stop(codec);
      AMediaCodec_delete(codec);
      codec = nullptr;
    }
    if (muxer) {
      if (muxando) AMediaMuxer_stop(muxer);
      AMediaMuxer_delete(muxer);
      muxer = nullptr;
    }
    if (fd >= 0) {
      close(fd);
      fd = -1;
    }
    if (apagar && !caminho.empty()) unlink(caminho.c_str());
  }
};

Codificador* g = nullptr;
std::string g_caminho;

AMediaCodec* configurar(const char* mime, int w, int h, int fps, int bitrate, int32_t cor) {
  AMediaCodec* c = AMediaCodec_createEncoderByType(mime);
  if (!c) return nullptr;
  AMediaFormat* f = AMediaFormat_new();
  AMediaFormat_setString(f, AMEDIAFORMAT_KEY_MIME, mime);
  AMediaFormat_setInt32(f, AMEDIAFORMAT_KEY_WIDTH, w);
  AMediaFormat_setInt32(f, AMEDIAFORMAT_KEY_HEIGHT, h);
  AMediaFormat_setInt32(f, AMEDIAFORMAT_KEY_COLOR_FORMAT, cor);
  AMediaFormat_setInt32(f, AMEDIAFORMAT_KEY_BIT_RATE, bitrate);
  AMediaFormat_setInt32(f, AMEDIAFORMAT_KEY_FRAME_RATE, fps);
  AMediaFormat_setInt32(f, AMEDIAFORMAT_KEY_I_FRAME_INTERVAL, 1);
  // BT.709 faixa limitada: as mesmas etiquetas do caminho em Kotlin.
  AMediaFormat_setInt32(f, "color-standard", 1);
  AMediaFormat_setInt32(f, "color-range", 2);
  AMediaFormat_setInt32(f, "color-transfer", 3);
  const bool ok = AMediaCodec_configure(c, f, nullptr, nullptr, 0) == AMEDIA_OK &&
                  AMediaCodec_start(c) == AMEDIA_OK;
  AMediaFormat_delete(f);
  if (!ok) {
    AMediaCodec_delete(c);
    return nullptr;
  }
  return c;
}

}  // namespace

extern "C" {

__attribute__((visibility("default"))) int32_t aurea_core_codificador_disponivel() { return 1; }

__attribute__((visibility("default")))
int32_t aurea_core_codificador_abrir(const char* caminho, int32_t w, int32_t h, int32_t fps,
                                     int32_t bitrate, int32_t hevc) {
  try {
    if (g) {
      g->soltar(true, g_caminho);
      delete g;
      g = nullptr;
    }
    if (!caminho || w <= 0 || h <= 0) return 0;
    auto* c = new Codificador();
    c->w = (w + 1) & ~1;
    c->h = (h + 1) & ~1;
    c->fps = fps < 1 ? 30 : fps;
    const char* mime = hevc ? "video/hevc" : "video/avc";
    AMediaCodec* codec = configurar(mime, c->w, c->h, c->fps, bitrate, kNV12);
    if (!codec) {
      codec = configurar(mime, c->w, c->h, c->fps, bitrate, kI420);
      c->cor = kI420;
    }
    if (!codec) {
      codec = configurar(mime, c->w, c->h, c->fps, bitrate, kFlexivel);
      c->cor = kFlexivel;
    }
    if (!codec && hevc) {
      mime = "video/avc";
      codec = configurar(mime, c->w, c->h, c->fps, bitrate, kNV12);
      c->cor = kNV12;
      if (!codec) {
        codec = configurar(mime, c->w, c->h, c->fps, bitrate, kI420);
        c->cor = kI420;
      }
    }
    if (!codec) {
      delete c;
      return 0;
    }
    c->codec = codec;
    c->stride = c->w;
    c->slice = c->h;
    // API 28+: o codificador diz o passo real do buffer (alguns alinham a 16).
    // A funcao so existe do Android 9 em diante e o app roda desde o 7:
    // procurada em tempo de execucao.
    using LerFormato = AMediaFormat* (*)(AMediaCodec*);
    const auto lerFormato =
        reinterpret_cast<LerFormato>(dlsym(RTLD_DEFAULT, "AMediaCodec_getInputFormat"));
    if (lerFormato) {
      if (AMediaFormat* in = lerFormato(codec)) {
        int32_t v = 0;
        if (AMediaFormat_getInt32(in, "stride", &v) && v >= c->w) c->stride = v;
        int32_t arranjo = 0;
        if (c->cor == kFlexivel && AMediaFormat_getInt32(in, "color-format", &arranjo) &&
            (arranjo == kNV12 || arranjo == kI420)) {
          c->cor = arranjo;
        }
        // O MAPA DOS PLANOS (MediaImage2), o mesmo que a API Image do Java le:
        // uint32 tipo, planos, largura, altura, bits, bits alocados; e por
        // plano uint32 deslocamento, int32 passo coluna e linha, uint32
        // subamostragem horizontal e vertical.
        void* dados = nullptr;
        size_t tam = 0;
        if (AMediaFormat_getBuffer(in, "image-data", &dados, &tam) && dados && tam >= 6 * 4 + 3 * 5 * 4) {
          uint32_t cab[6];
          memcpy(cab, dados, sizeof(cab));
          const uint32_t tipoYuv = 1;
          if (cab[0] == tipoYuv && cab[1] == 3 && cab[4] == 8) {
            aurea::PlanoYuv p[3];
            bool ok = true;
            for (int k = 0; k < 3; ++k) {
              int32_t campo[5];
              memcpy(campo, static_cast<uint8_t*>(dados) + 24 + k * 20, sizeof(campo));
              p[k] = {uint32_t(campo[0]), campo[1], campo[2]};
              const int32_t sub = k == 0 ? 1 : 2;
              ok = ok && campo[1] > 0 && campo[2] > 0 && campo[3] == sub && campo[4] == sub;
            }
            if (ok) {
              c->planos = true;
              c->py = p[0];
              c->pu = p[1];
              c->pv = p[2];
              c->cor = kNV12;  // qualquer arranjo conhecido: o mapa manda
              LOG("planos Y %u/%d/%d U %u/%d/%d V %u/%d/%d", p[0].desloca, p[0].passo_coluna,
                  p[0].passo_linha, p[1].desloca, p[1].passo_coluna, p[1].passo_linha,
                  p[2].desloca, p[2].passo_coluna, p[2].passo_linha);
            }
          }
        }
        if (AMediaFormat_getInt32(in, "slice-height", &v) && v >= c->h) c->slice = v;
        AMediaFormat_delete(in);
      }
    }
    if (c->cor == kFlexivel) {
      // Arranjo desconhecido (Android antigo sem o formato de entrada):
      // melhor o caminho em Kotlin do que um video com a cor embaralhada.
      LOG("codificador flexivel sem arranjo conhecido; fica o caminho antigo");
      c->soltar(false, "");
      delete c;
      return 0;
    }
    c->fd = open(caminho, O_CREAT | O_TRUNC | O_RDWR, 0644);
    if (c->fd < 0) {
      c->soltar(false, "");
      delete c;
      return 0;
    }
    c->muxer = AMediaMuxer_new(c->fd, AMEDIAMUXER_OUTPUT_FORMAT_MPEG_4);
    if (!c->muxer) {
      c->soltar(true, caminho);
      delete c;
      return 0;
    }
    c->trabalhador = std::thread([c] { c->laco(); });
    g = c;
    g_caminho = caminho;
    LOG("codificador %s %dx%d cor %d passo %d/%d", mime, c->w, c->h, c->cor, c->stride, c->slice);
    return 1;
  } catch (...) {
    return 0;
  }
}

// O quadro [rgba] (w x h) entra na fila: uma copia, e volta. Espera so
// quando ja ha dois quadros na fila (a memoria fica limitada).
__attribute__((visibility("default")))
int32_t aurea_core_codificador_quadro(const uint8_t* rgba, int32_t w, int32_t h) {
  try {
    Codificador* c = g;
    if (!c || !rgba || c->falhou) return 0;
    std::vector<uint8_t> buf;
    {
      std::unique_lock<std::mutex> l(c->m);
      c->cv.wait(l, [&] { return c->fila.size() < kFila || c->falhou; });
      if (c->falhou) return 0;
      if (!c->livres.empty()) {
        buf = std::move(c->livres.back());
        c->livres.pop_back();
      }
    }
    // O quadro chega no tamanho pedido; dimensao impar ganha a borda repetida.
    buf.assign(size_t(c->w) * size_t(c->h) * 4, 0);
    const int cw = w < c->w ? w : c->w, ch = h < c->h ? h : c->h;
    for (int j = 0; j < c->h; ++j) {
      const int sj = j < ch ? j : ch - 1;
      memcpy(buf.data() + size_t(j) * c->w * 4, rgba + size_t(sj) * w * 4, size_t(cw) * 4);
      for (int i = cw; i < c->w; ++i) {
        memcpy(buf.data() + (size_t(j) * c->w + i) * 4, buf.data() + (size_t(j) * c->w + cw - 1) * 4, 4);
      }
    }
    {
      std::lock_guard<std::mutex> l(c->m);
      c->fila.push_back(std::move(buf));
    }
    c->cv.notify_all();
    return 1;
  } catch (...) {
    return 0;
  }
}

__attribute__((visibility("default"))) int32_t aurea_core_codificador_terminar() {
  try {
    Codificador* c = g;
    if (!c) return 0;
    {
      std::unique_lock<std::mutex> l(c->m);
      c->cv.wait(l, [&] { return c->fila.empty() || c->falhou; });
      c->fechar = true;
    }
    c->cv.notify_all();
    if (c->trabalhador.joinable()) c->trabalhador.join();
    bool ok = !c->falhou;
    if (ok) {
      ssize_t i = -1;
      for (int t = 0; t < 400 && i < 0; ++t) i = AMediaCodec_dequeueInputBuffer(c->codec, 5000);
      ok = i >= 0 &&
           AMediaCodec_queueInputBuffer(c->codec, size_t(i), 0, 0, 0,
                                        AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM) == AMEDIA_OK &&
           c->drenar(true) && c->muxando;
    }
    c->soltar(!ok, g_caminho);
    delete c;
    g = nullptr;
    return ok ? 1 : 0;
  } catch (...) {
    return 0;
  }
}

__attribute__((visibility("default"))) void aurea_core_codificador_cancelar() {
  try {
    if (!g) return;
    g->soltar(true, g_caminho);
    delete g;
    g = nullptr;
  } catch (...) {
  }
}

}  // extern "C"
