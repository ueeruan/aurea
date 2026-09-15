/* RASTREIO DE CAMERA 3D DO AUREA (C API). Dois objetos:
 *   att_tracker: segue pontos em quadros de cinza (KLT piramidal com
 *                checagem de ida e volta);
 *   att_sfm:     resolve a camera a partir dos rastros (reconstrucao
 *                incremental, homografia para chao plano, ajuste de feixes
 *                com focal livre, pose de todo quadro, modo tripe).
 * Coordenadas em px do quadro analisado. Pose: Xcam = R*Xmundo + t.
 */
#ifndef AUREA_TRACKER_H
#define AUREA_TRACKER_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ATT_OK 0
#define ATT_ERR_ARGS -1
#define ATT_ERR_FEW_POINTS -2
#define ATT_ERR_NO_PARALLAX -3
#define ATT_ERR_DIVERGED -4

#define ATT_MODE_AUTO 0
#define ATT_MODE_TRIPOD 1

#if defined(_WIN32)
#define ATT_API __declspec(dllexport)
#else
#define ATT_API __attribute__((visibility("default")))
#endif

typedef struct att_tracker att_tracker;
typedef struct att_sfm att_sfm;

ATT_API int32_t att_version(void);

ATT_API att_tracker* att_tracker_create(int32_t width, int32_t height,
                                        int32_t max_points);
ATT_API void att_tracker_destroy(att_tracker* t);
/* Um quadro de cinza (width*height bytes). Devolve quantos pontos vivos. */
ATT_API int32_t att_tracker_push(att_tracker* t, const uint8_t* gray,
                                 int32_t frame);
ATT_API int32_t att_tracker_observation_count(att_tracker* t);
/* [id, quadro, x, y] por observacao; devolve quantas escreveu. */
ATT_API int32_t att_tracker_observations(att_tracker* t, double* out,
                                         int32_t max);

ATT_API att_sfm* att_sfm_create(int32_t width, int32_t height, int32_t frames,
                                int32_t fps);
ATT_API void att_sfm_destroy(att_sfm* s);
/* obs: [id, quadro, x, y] * n */
ATT_API void att_sfm_add(att_sfm* s, const double* obs, int32_t n);
ATT_API int32_t att_sfm_solve(att_sfm* s, double focal_guess, int32_t mode);
ATT_API double att_sfm_focal(att_sfm* s);
ATT_API double att_sfm_error(att_sfm* s);
ATT_API int32_t att_sfm_is_tripod(att_sfm* s);
ATT_API int32_t att_sfm_pose_count(att_sfm* s);
/* [quadro, R(9, por linha), t(3)] por pose */
ATT_API int32_t att_sfm_poses(att_sfm* s, double* out, int32_t max);
ATT_API int32_t att_sfm_point_count(att_sfm* s);
/* [id, x, y, z, erro_px, vistas] por ponto */
ATT_API int32_t att_sfm_points(att_sfm* s, double* out, int32_t max);

#ifdef __cplusplus
}
#endif

#endif
