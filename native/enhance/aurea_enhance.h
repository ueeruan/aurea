/*
 * AUREA ENHANCE — super-resolucao/restauracao por IA em C++ sobre ncnn.
 *
 * Implementacao propria (nao usa o realesrgan.cpp do upstream): tiles na
 * CPU com margem de contexto, inferencia no ncnn (Vulkan quando ha GPU),
 * clamp em ponto flutuante antes de arredondar e erro PROPAGADO — falta
 * de memoria nunca vira quadro preto com "sucesso".
 *
 * Modelo validado: realesr-animevideov3-x4 (SRVGGNetCompact, blob "data"
 * -> "output", RGB 0..1, escala nativa 4). Saidas 1x e 2x sao a inferencia
 * x4 reduzida por media de area: o custo e o do x4 na mesma entrada.
 */
#ifndef AUREA_ENHANCE_H
#define AUREA_ENHANCE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#if defined(_WIN32)
#define AE_EXPORT __declspec(dllexport)
#else
#define AE_EXPORT __attribute__((visibility("default")))
#endif

#define AE_OK 0
#define AE_ERR_ARGS -1
#define AE_ERR_INFERENCE -2 /* extract falhou: memoria da GPU ou operador */
#define AE_ERR_CANCELLED -3
#define AE_ERR_MODEL -4
#define AE_ERR_IO -5 /* PNG ilegivel ou escrita falhou */

typedef struct ae_engine ae_engine;

typedef struct ae_info {
  int32_t gpu;          /* 1 = Vulkan, 0 = CPU */
  int32_t fp16;         /* armazenamento fp16 ligado */
  int32_t tile;         /* lado do tile de entrada */
  int32_t model_scale;  /* escala nativa do modelo */
  int64_t heap_budget_mb;
  char device[128];
} ae_info;

/* Carrega o modelo UMA vez. use_gpu = 0 forca CPU. Devolve nulo em falha e
 * escreve a causa em err. */
AE_EXPORT ae_engine* ae_create(const char* param_path, const char* bin_path,
                               int32_t model_scale, int32_t use_gpu, char* err,
                               int32_t err_len);
/* DNI (deep network interpolation) de dois modelos com o MESMO param:
 * pesos = weight_a * A + (1 - weight_a) * B, misturados uma vez na carga.
 * E o "denoise strength" do Real-ESRGAN: A = realesr-general-x4v3 (limpa
 * forte), B = realesr-general-wdn-x4v3 (preserva o grao). So aceita as
 * camadas do SRVGGNetCompact. */
AE_EXPORT ae_engine* ae_create_dni(const char* param_path, const char* bin_a, const char* bin_b,
                                   float weight_a, int32_t model_scale, int32_t use_gpu,
                                   char* err, int32_t err_len);
AE_EXPORT void ae_destroy(ae_engine* engine);
AE_EXPORT int32_t ae_info_get(ae_engine* engine, ae_info* out);
/* Sobrescreve o tile automatico (32..512); 0 volta ao automatico. */
AE_EXPORT void ae_set_tile(ae_engine* engine, int32_t tile);

/*
 * Processa um quadro RGB24 (w x h). Saida RGB24 com (w*out_scale) x
 * (h*out_scale), out_scale em {1, 2, 4}. strength 0..1 mistura a saida da
 * IA com o original redimensionado (Catmull-Rom) no MESMO tamanho — e a
 * intensidade do efeito, nao um denoise. *cancel != 0 interrompe entre
 * tiles. Devolve AE_OK ou um AE_ERR_*.
 */
AE_EXPORT int32_t ae_process(ae_engine* engine, const uint8_t* rgb, int32_t w, int32_t h,
                             int32_t out_scale, float strength, uint8_t* out,
                             const int32_t* cancel);

/*
 * PNG -> PNG, para a exportacao do editor: le o quadro (RGB), roda
 * ae_process na escala out_scale e, com fit_w/fit_h > 0, leva ao tamanho
 * da composicao (Catmull-Rom, com o nucleo alargado ao reduzir). Gravacao
 * atomica: in_path == out_path substitui o quadro so quando tudo deu certo.
 */
AE_EXPORT int32_t ae_process_png(ae_engine* engine, const char* in_path, const char* out_path,
                                 int32_t out_scale, float strength, int32_t fit_w, int32_t fit_h,
                                 const int32_t* cancel);

/* Redimensionamento convencional (Catmull-Rom; alarga o nucleo ao reduzir)
 * para comparar antes/depois na mesma resolucao. */
AE_EXPORT int32_t ae_resize(const uint8_t* rgb, int32_t w, int32_t h, uint8_t* out,
                            int32_t ow, int32_t oh);

#ifdef __cplusplus
}
#endif

#endif
