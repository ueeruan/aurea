/*
 * AUREA RIFE — quadro intermediario por fluxo optico aprendido (RIFE v4.6
 * sobre ncnn, Vulkan quando ha GPU).
 *
 * Para que serve: camera lenta na exportacao. Entre dois quadros REAIS da
 * fonte, gera o quadro do instante t (0..1) estimando o movimento — nao e
 * a mistura dos dois (que deixa rastro) nem a repeticao (que engasga).
 *
 * rife.cpp e a copia corrigida do rife-ncnn-vulkan (ver rife/PATCHES.md).
 * Erro sempre PROPAGADO: falta de memoria da GPU vira codigo de erro, nunca
 * quadro com lixo e "sucesso".
 */
#ifndef AUREA_RIFE_H
#define AUREA_RIFE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#if defined(_WIN32)
#define AR_EXPORT __declspec(dllexport)
#else
#define AR_EXPORT __attribute__((visibility("default")))
#endif

#define AR_OK 0
#define AR_ERR_ARGS -1
#define AR_ERR_INFERENCE -2 /* rede falhou: memoria da GPU ou operador */
#define AR_ERR_MODEL -4
#define AR_ERR_IO -5        /* PNG ilegivel, tamanhos diferentes ou escrita falhou */

typedef struct ar_engine ar_engine;

typedef struct ar_info {
  int32_t gpu;  /* 1 = Vulkan, 0 = CPU */
  int32_t fp16; /* armazenamento fp16 ligado (depois da limpeza do ncnn) */
  int32_t int8; /* armazenamento int8 ligado */
  int32_t reservado;
  int64_t heap_budget_mb;
  char device[128];
} ar_info;

/* Carrega flownet.param/flownet.bin de model_dir (RIFE v4.x). use_gpu = 0
 * forca CPU. Com use_gpu = 1 e GPU que nao carrega o modelo, devolve nulo
 * (quem chama decide se tenta CPU). Causa escrita em err. */
AR_EXPORT ar_engine* ar_create(const char* model_dir, int32_t use_gpu, char* err,
                               int32_t err_len);
AR_EXPORT void ar_destroy(ar_engine* engine);
AR_EXPORT int32_t ar_info_get(ar_engine* engine, ar_info* out);

/* a e b: RGB24 (w x h), o mesmo tamanho. out recebe o quadro do instante t
 * (RGB24 w x h). t <= 0 copia a; t >= 1 copia b. */
AR_EXPORT int32_t ar_interpolate(ar_engine* engine, const uint8_t* a, const uint8_t* b,
                                 int32_t w, int32_t h, float t, uint8_t* out);

/* O mesmo com arquivos PNG (caminhos UTF-8). Guarda os dois ultimos quadros
 * lidos: uma sequencia de pares vizinhos le cada arquivo uma vez. A saida e
 * PNG RGB. */
AR_EXPORT int32_t ar_interpolate_png(ar_engine* engine, const char* a_path,
                                     const char* b_path, float t, const char* out_path);

#ifdef __cplusplus
}
#endif

#endif
