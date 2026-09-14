#include "aurea_png.h"

#include <cstdlib>
#include <cstring>
#include <new>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#if defined(__ANDROID__)
// zlib do sistema (API estavel do NDK): nivel 1 comprime o quadro em uma
// fracao do tempo do compressor embutido do stb.
#include <zlib.h>
static unsigned char* aurea_zlib(unsigned char* data, int len, int* out_len, int) {
  uLongf cap = compressBound(static_cast<uLong>(len));
  unsigned char* out = static_cast<unsigned char*>(std::malloc(cap));
  if (!out) return nullptr;
  if (compress2(out, &cap, data, static_cast<uLong>(len), Z_BEST_SPEED) != Z_OK) {
    std::free(out);
    return nullptr;
  }
  *out_len = static_cast<int>(cap);
  return out;
}
#define STBIW_ZLIB_COMPRESS aurea_zlib
#endif

// Implementacoes do stb so neste arquivo, e com ligacao interna: outra
// biblioteca do processo com stb nao colide com esta.
#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#include "stb_image.h"
#define STB_IMAGE_WRITE_STATIC
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STBI_WRITE_NO_STDIO
#include "stb_image_write.h"

namespace aurea_png {

namespace {

#if defined(_WIN32)
std::wstring largo(const std::string& s) {
  if (s.empty()) return std::wstring();
  const int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), nullptr, 0);
  std::wstring w(static_cast<size_t>(n), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), &w[0], n);
  return w;
}
#endif

// Le o arquivo inteiro sem excecoes (o ncnn do Android compila sem elas).
std::unique_ptr<unsigned char[]> lerArquivo(const std::string& caminho, int* tamanho) {
  FILE* f = abrir(caminho, "rb");
  if (!f) return nullptr;
  std::unique_ptr<unsigned char[]> dados;
  if (std::fseek(f, 0, SEEK_END) == 0) {
    const long n = std::ftell(f);
    if (n > 0 && n < 512L * 1024 * 1024 && std::fseek(f, 0, SEEK_SET) == 0) {
      dados.reset(new (std::nothrow) unsigned char[static_cast<size_t>(n)]);
      if (dados && std::fread(dados.get(), 1, static_cast<size_t>(n), f) == static_cast<size_t>(n)) {
        *tamanho = static_cast<int>(n);
      } else {
        dados.reset();
      }
    }
  }
  std::fclose(f);
  return dados;
}

struct Saida {
  FILE* f = nullptr;
  bool ok = true;
};

void escreve(void* ctx, void* data, int size) {
  Saida* s = static_cast<Saida*>(ctx);
  if (s->ok && size > 0 &&
      std::fwrite(data, 1, static_cast<size_t>(size), s->f) != static_cast<size_t>(size)) {
    s->ok = false;
  }
}

void apagar(const std::string& caminho) {
#if defined(_WIN32)
  _wremove(largo(caminho).c_str());
#else
  std::remove(caminho.c_str());
#endif
}

}  // namespace

void LiberaPixels::operator()(unsigned char* p) const { stbi_image_free(p); }

FILE* abrir(const std::string& caminho, const char* modo) {
#if defined(_WIN32)
  const std::wstring m = largo(modo);
  return _wfopen(largo(caminho).c_str(), m.c_str());
#else
  return std::fopen(caminho.c_str(), modo);
#endif
}

bool legivel(const std::string& caminho) {
  FILE* f = abrir(caminho, "rb");
  if (!f) return false;
  std::fclose(f);
  return true;
}

bool ler(const std::string& caminho, Imagem* saida) {
  if (!saida) return false;
  int tamanho = 0;
  const std::unique_ptr<unsigned char[]> bytes = lerArquivo(caminho, &tamanho);
  if (!bytes) return false;
  int w = 0, h = 0, canais = 0;
  unsigned char* px = stbi_load_from_memory(bytes.get(), tamanho, &w, &h, &canais, 3);
  if (!px) return false;
  saida->w = w;
  saida->h = h;
  saida->rgb.reset(px);
  return true;
}

bool gravar(const std::string& caminho, const unsigned char* rgb, int w, int h) {
  if (!rgb || w <= 0 || h <= 0) return false;
  // Sem filtro e compressao minima: arquivo de passagem que vive minutos
  // (a mesma escolha do FFmpeg na extracao).
  stbi_write_png_compression_level = 1;
  stbi_write_force_png_filter = 0;
  const std::string temporario = caminho + ".tmp";
  Saida s;
  s.f = abrir(temporario, "wb");
  if (!s.f) return false;
  const int escrito = stbi_write_png_to_func(escreve, &s, w, h, 3, rgb, w * 3);
  const bool fechou = std::fclose(s.f) == 0;
  if (!escrito || !s.ok || !fechou) {
    apagar(temporario);
    return false;
  }
#if defined(_WIN32)
  if (!MoveFileExW(largo(temporario).c_str(), largo(caminho).c_str(), MOVEFILE_REPLACE_EXISTING)) {
    apagar(temporario);
    return false;
  }
#else
  if (std::rename(temporario.c_str(), caminho.c_str()) != 0) {
    apagar(temporario);
    return false;
  }
#endif
  return true;
}

}  // namespace aurea_png
