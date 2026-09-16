#pragma once
#include <cstdint>

namespace aurea {

// RGBA (como o Flutter entrega) -> NV12 BT.709 faixa limitada. Mesma matriz
// inteira do VideoEncoder.kt, para o arquivo sair igual ao do caminho antigo.
// [stride] e [slice] sao os do buffer do codificador (>= w e >= h).
void rgba_para_nv12(const uint8_t* rgba, int w, int h, uint8_t* out, int stride, int slice);

// Mesma conta para I420 (planar), para codificadores sem semi-planar.
void rgba_para_i420(const uint8_t* rgba, int w, int h, uint8_t* out, int stride, int slice);

// Onde fica cada plano no buffer do codificador (o MediaImage2 do Android):
// deslocamento do primeiro byte e passo por coluna e por linha da amostra.
struct PlanoYuv {
  uint32_t desloca;
  int32_t passo_coluna;
  int32_t passo_linha;
};

// Escreve Y, U e V (4:2:0) em qualquer arranjo descrito por [y], [u], [v]:
// NV12, NV21, I420, YV12 com qualquer passo. Mesma matriz das outras.
void rgba_para_yuv420(const uint8_t* rgba, int w, int h, uint8_t* out, PlanoYuv y, PlanoYuv u,
                      PlanoYuv v);

}  // namespace aurea
