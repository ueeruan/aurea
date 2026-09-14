/*
 * AUREA TIMECORE — o nucleo temporal do Time Remap, em C puro na fronteira.
 *
 * Uma unica implementacao para Android, iOS e o host dos testes. O Dart
 * chama por FFI (native assets); o motor de interpolacao do Android
 * (native/timeremap) compila as mesmas fontes.
 *
 * Separa DUAS perguntas que o sistema antigo misturava:
 *   1. MAPEAMENTO: s = T(t), o instante da fonte para o instante local da
 *      camada (curva com easing, loops, extrapolacao, velocidade, reverso).
 *   2. ENQUADRAMENTO: dados os PTS reais da fonte, quais dois quadros
 *      cercam s e em que alpha = (s - PTS_A) / (PTS_B - PTS_A).
 * A interpolacao de imagem (RIFE) mora fora daqui.
 *
 * Unidades: tempo em microssegundos (int64); valores de curva em
 * segundos (double), como o parametro 'tempo' do projeto.
 */
#ifndef AUREA_TIMECORE_H
#define AUREA_TIMECORE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#if defined(_WIN32)
#define ATC_EXPORT __declspec(dllexport)
#else
#define ATC_EXPORT __attribute__((visibility("default")))
#endif

#define ATC_VERSION 1

/* Tipos de easing: MESMA ORDEM do enum EasingType do Dart. */
enum {
  ATC_EASE_CUBIC_BEZIER = 0,
  ATC_EASE_BOUNCE = 1,
  ATC_EASE_ELASTIC = 2,
  ATC_EASE_CYCLIC = 3,
  ATC_EASE_RANDOM = 4,
  ATC_EASE_STEPS = 5,
  ATC_EASE_ELASTIC_STEPS = 6,
  ATC_EASE_SPRING = 7,
  ATC_EASE_HOLD = 8
};

/* Loop dos keyframes: MESMA ORDEM de LoopMode / LoopWhen do Dart. */
enum {
  ATC_LOOP_NONE = 0,
  ATC_LOOP_CYCLE = 1,
  ATC_LOOP_PING_PONG = 2,
  ATC_LOOP_OFFSET = 3,
  ATC_LOOP_CONTINUE = 4
};
enum { ATC_LOOP_AFTER = 0, ATC_LOOP_BEFORE = 1, ATC_LOOP_BOTH = 2 };

/*
 * Fora do intervalo dos keyframes:
 *   HOLD   = o valueAt do AnimatedDouble (segura a ponta ou aplica o loop);
 *   LINEAR = continua com a inclinacao secante do trecho da ponta (clipe de
 *            video: transicoes e bordas congeladas pedem instantes fora).
 * Com loop ativo, os dois modos seguem o loop.
 */
enum { ATC_EXTRAP_HOLD = 0, ATC_EXTRAP_LINEAR = 1 };

/* 96 bytes, alinhamento de 8. O Dart confere o tamanho. */
typedef struct atc_keyframe {
  int64_t time_us;
  double value;
  int32_t type;
  int32_t count;
  double x1, y1, x2, y2;
  double smooth, intensity, response, damping, velocity;
} atc_keyframe;

typedef struct atc_curve atc_curve;

/* Uma camada de video vista pelo mapeamento. 40 bytes. */
typedef struct atc_layer {
  int64_t source_offset_us;
  int64_t duration_us;
  double speed;
  int32_t reverse;
  int32_t reserved;
  const atc_curve* curve; /* nulo = velocidade constante */
} atc_layer;

enum {
  ATC_BRACKET_EXACT = 0,       /* o instante e um quadro original: a */
  ATC_BRACKET_INTERPOLATE = 1, /* entre a e b, em alpha */
  ATC_BRACKET_BEFORE = 2,      /* antes do primeiro quadro: a = 0 */
  ATC_BRACKET_AFTER = 3        /* depois do ultimo quadro: a = n - 1 */
};

/* 40 bytes. */
typedef struct atc_bracket {
  int32_t a;
  int32_t b;
  int32_t kind;
  int32_t reserved;
  int64_t pts_a_us;
  int64_t pts_b_us;
  double alpha;
} atc_bracket;

typedef struct atc_frame_index atc_frame_index;

ATC_EXPORT int32_t atc_version(void);
ATC_EXPORT int32_t atc_sizeof_keyframe(void);
ATC_EXPORT int32_t atc_sizeof_layer(void);
ATC_EXPORT int32_t atc_sizeof_bracket(void);

/* ---- easing isolado ---- */
ATC_EXPORT double atc_ease_transform(const atc_keyframe* ease, double t);
ATC_EXPORT double atc_ease_derivative(const atc_keyframe* ease, double t);

/* ---- curva ---- */
/* Copia os keyframes (reordena por tempo; valor nao finito vira 0). */
ATC_EXPORT atc_curve* atc_curve_create(double base, const atc_keyframe* keyframes,
                                       int32_t count, int32_t loop_mode,
                                       int32_t loop_when, int32_t loop_count);
/* Assinatura compativel com NativeFinalizer: void (*)(void*). */
ATC_EXPORT void atc_curve_destroy(void* curve);
ATC_EXPORT int32_t atc_curve_count(const atc_curve* curve);
ATC_EXPORT double atc_curve_value(const atc_curve* curve, int64_t t_us, int32_t extrap);
/* Derivada em unidades de valor por SEGUNDO. Nos pontos de keyframe vale a
 * derivada do trecho que comeca ali (a mesma convencao da avaliacao). */
ATC_EXPORT double atc_curve_slope(const atc_curve* curve, int64_t t_us, int32_t extrap);
ATC_EXPORT void atc_curve_values(const atc_curve* curve, const int64_t* t_us,
                                 int32_t n, int32_t extrap, double* out);
/* Extremos REAIS entre o primeiro e o ultimo keyframe (inclui o overshoot
 * do easing) e a base. Devolve 0 sem keyframes (lo = hi = base). */
ATC_EXPORT int32_t atc_curve_range(const atc_curve* curve, double* lo, double* hi);

/* ---- camada ---- */
ATC_EXPORT int64_t atc_layer_span_us(const atc_layer* layer);
ATC_EXPORT int64_t atc_layer_source_us(const atc_layer* layer, int64_t local_us);
ATC_EXPORT int64_t atc_layer_absolute_source_us(const atc_layer* layer, int64_t local_us);
/* Segundos de fonte por segundo de timeline; negativo = reverso. */
ATC_EXPORT double atc_layer_rate(const atc_layer* layer, int64_t local_us);
ATC_EXPORT void atc_layer_absolute_many(const atc_layer* layer, const int64_t* local_us,
                                        int32_t n, int64_t* out);

/*
 * Recorta o mapeamento da camada entre [from, to] (tempo local) numa
 * curva nova, relativa a `from`, com os valores ja SEM o minimo (que sai
 * em *min_value e vira o novo sourceOffset). Trechos bezier/linear/hold
 * sao divididos EXATAMENTE (de Casteljau); os demais sao aproximados por
 * pedacos de Hermite com erro maximo tolerance_us na fonte.
 * Devolve a quantidade de keyframes; se passar de `capacity`, devolve o
 * negativo do tamanho necessario e nao escreve nada.
 */
ATC_EXPORT int32_t atc_layer_slice(const atc_layer* layer, int64_t from_us, int64_t to_us,
                                   int64_t tolerance_us, atc_keyframe* out,
                                   int32_t capacity, double* min_value);

/*
 * VELOCIDADE -> TEMPO. Keyframes cujo valor e a velocidade (1 = normal) e
 * cujo easing molda a velocidade entre eles. Integra T(t) = start + ∫v e
 * devolve keyframes de VALOR (segundos de fonte) com bezier. Velocidade
 * linear ou constante sai exata; o resto com erro <= tolerance_us.
 * Mesmo contrato de capacidade de atc_layer_slice.
 */
ATC_EXPORT int32_t atc_speed_to_value(const atc_keyframe* speed, int32_t count,
                                      double start_value, int64_t tolerance_us,
                                      atc_keyframe* out, int32_t capacity);

/* ---- tabela de quadros ---- */
/* PTS em qualquer ordem; repetidos sao descartados; valor < -2^62 ignorado.
 * last_duration_us: quanto o ultimo quadro dura (0 = desconhecido). */
ATC_EXPORT atc_frame_index* atc_frame_index_create(const int64_t* pts_us, int32_t n,
                                                   int64_t last_duration_us);
ATC_EXPORT void atc_frame_index_destroy(void* index);
ATC_EXPORT int32_t atc_frame_index_count(const atc_frame_index* index);
ATC_EXPORT int64_t atc_frame_index_pts(const atc_frame_index* index, int32_t i);
/* Indice do quadro que ESTA NA TELA em source_us (maior PTS <= s). */
ATC_EXPORT int32_t atc_frame_index_floor(const atc_frame_index* index, int64_t source_us);
/*
 * Enquadra s. snap_us e snap_alpha decidem quando reutilizar o quadro
 * original em vez de interpolar (distancia absoluta ou fracao do
 * intervalo). Devolve 0 em sucesso, -1 sem quadros.
 */
ATC_EXPORT int32_t atc_frame_bracket(const atc_frame_index* index, int64_t source_us,
                                     int64_t snap_us, double snap_alpha, atc_bracket* out);

#ifdef __cplusplus
}
#endif

#endif /* AUREA_TIMECORE_H */
