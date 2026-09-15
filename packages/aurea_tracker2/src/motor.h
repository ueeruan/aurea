/* O MOTOR 2.0 DO RASTREIO DE CAMERA 3D DO AUREA (C API).
 *
 * Dois objetos, como no motor 1 — mas com o que faltava a ele:
 *
 *   at2_seguidor: segue pontos em quadros de cinza. Lucas-Kanade iterativo
 *                 em piramide (subpixel por construcao), checagem de ida e
 *                 volta E trava de deriva: todo ponto e conferido contra o
 *                 molde do quadro em que NASCEU, e um ponto que ja nao
 *                 parece com ele morre em vez de virar fantasma.
 *   at2_cena:     resolve a camera a partir dos rastros. Reconstrucao
 *                 incremental com escolha de modelo (homografia x
 *                 essencial), ajuste de feixes robusto (Huber) com focal E
 *                 distorcao radial k1 livres, tripe detectado sozinho, e
 *                 VALIDACAO DURA: quando a filmagem nao sustenta uma cena,
 *                 a resposta e um codigo de erro com nome — nunca uma cena
 *                 inventada.
 *
 * Coordenadas em pixels do quadro analisado. Pose: Xcam = R*Xmundo + t,
 * camera olhando +Z, Y da imagem para baixo. Deterministico: a mesma
 * entrada da a mesma saida em qualquer aparelho.
 */
#ifndef AUREA_TRACKER2_MOTOR_H
#define AUREA_TRACKER2_MOTOR_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define AT2_OK 0
#define AT2_ERR_ARGS -1
#define AT2_ERR_POUCOS_PONTOS -2
#define AT2_ERR_SEM_PARALAXE -3
#define AT2_ERR_NAO_CONVERGIU -4
#define AT2_ERR_POUCOS_QUADROS -5

#define AT2_MODO_AUTO 0
#define AT2_MODO_TRIPE 1

#if defined(_WIN32)
#define AT2_API __declspec(dllexport)
#else
#define AT2_API __attribute__((visibility("default")))
#endif

/* Fases do progresso: 1 = seguindo pontos (a = quadro, b = total),
 * 2 = resolvendo a cena (a = passo, b = total). A fracao ja vem 0..1. */
typedef void (*at2_progresso)(int32_t fase, double fracao, int32_t a,
                              int32_t b, void* alvo);

typedef struct at2_seguidor at2_seguidor;
typedef struct at2_cena at2_cena;

/* Identidade do motor ("2.0.0"): serve de prova de que a biblioteca nativa
 * CARREGOU — quem nao consegue chamar isto nao tem motor, e o app tem de
 * dizer isso em vez de fingir que rastreou. */
AT2_API const char* at2_versao(void);

AT2_API at2_seguidor* at2_seguidor_criar(int32_t largura, int32_t altura,
                                         int32_t maximo_de_pontos);
AT2_API void at2_seguidor_destruir(at2_seguidor* s);
/* Um quadro de cinza (largura*altura bytes), em ordem. Devolve quantos
 * pontos estao vivos depois dele. */
AT2_API int32_t at2_seguidor_empurrar(at2_seguidor* s, const uint8_t* cinza,
                                      int32_t quadro);
AT2_API int32_t at2_seguidor_quantas(at2_seguidor* s);
/* [id, quadro, x, y] por observacao; devolve quantas escreveu. */
AT2_API int32_t at2_seguidor_observacoes(at2_seguidor* s, double* saida,
                                         int32_t maximo);

AT2_API at2_cena* at2_cena_criar(int32_t largura, int32_t altura,
                                 int32_t quadros, int32_t fps);
AT2_API void at2_cena_destruir(at2_cena* c);
/* obs: [id, quadro, x, y] * quantas */
AT2_API void at2_cena_observar(at2_cena* c, const double* obs,
                               int32_t quantas);
/* focal_px = 0 deixa a focal livre. Devolve um codigo AT2_*. O progresso
 * pode ser nulo. */
AT2_API int32_t at2_cena_resolver(at2_cena* c, double focal_px, int32_t modo,
                                  at2_progresso progresso, void* alvo);
AT2_API double at2_cena_focal(at2_cena* c);
/* Distorcao radial k1 do quadro analisado (r em unidades de focal). */
AT2_API double at2_cena_distorcao(at2_cena* c);
AT2_API double at2_cena_erro(at2_cena* c);
AT2_API int32_t at2_cena_tripe(at2_cena* c);
AT2_API int32_t at2_cena_quantas_poses(at2_cena* c);
/* [quadro, R(9, por linha), t(3)] por pose */
AT2_API int32_t at2_cena_poses(at2_cena* c, double* saida, int32_t maximo);
AT2_API int32_t at2_cena_quantos_pontos(at2_cena* c);
/* [id, x, y, z, erro_px, vistas] por ponto */
AT2_API int32_t at2_cena_pontos(at2_cena* c, double* saida, int32_t maximo);

#ifdef __cplusplus
}
#endif

#endif /* AUREA_TRACKER2_MOTOR_H */
