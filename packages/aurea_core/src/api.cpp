// Ponto de entrada comum do nucleo. Tudo aqui e C puro com try/catch: uma
// excecao C++ que atravesse a fronteira do FFI derruba o app.
#include "cor.h"

#include <cstdint>

#if defined(_WIN32)
#define API __declspec(dllexport)
#else
#define API __attribute__((visibility("default")))
#endif

extern "C" {

API int32_t aurea_core_versao() { return 1; }

// Conversao exposta para teste (o codificador usa a mesma).
API int32_t aurea_core_rgba_para_nv12(const uint8_t* rgba, int32_t w, int32_t h, uint8_t* out) {
  try {
    if (!rgba || !out || w <= 0 || h <= 0 || (w & 1) || (h & 1)) return 0;
    aurea::rgba_para_nv12(rgba, w, h, out, w, h);
    return 1;
  } catch (...) {
    return 0;
  }
}

#if !defined(__ANDROID__)
// Fora do Android o codificador do nucleo ainda nao existe: o app usa o
// caminho da plataforma (AVAssetWriter no iOS).
API int32_t aurea_core_codificador_disponivel() { return 0; }
API int32_t aurea_core_codificador_abrir(const char*, int32_t, int32_t, int32_t, int32_t, int32_t) { return 0; }
API int32_t aurea_core_codificador_quadro(const uint8_t*, int32_t, int32_t) { return 0; }
API int32_t aurea_core_codificador_terminar() { return 0; }
API void aurea_core_codificador_cancelar() {}
#endif

}  // extern "C"
