#ifndef EMBD3D_MAT4_H
#define EMBD3D_MAT4_H

#include "embd3d_config.h"
#include "embd3d_vec3.h"


#ifdef __cplusplus
extern "C" {
#endif

/* 4x4 matrix, ROW major, row-vector convention:  v' = v * M
 * (identical storage convention to the original lib, so MATRIX_* style
 * initialisers keep working; only the API argument order changed.) */
typedef embd3d_real_t embd3d_mat4_t[16];

#define EMBD3D_MAT4_IDENTITY {                    \
    (embd3d_real_t)1, (embd3d_real_t)0, (embd3d_real_t)0, (embd3d_real_t)0, \
    (embd3d_real_t)0, (embd3d_real_t)1, (embd3d_real_t)0, (embd3d_real_t)0, \
    (embd3d_real_t)0, (embd3d_real_t)0, (embd3d_real_t)1, (embd3d_real_t)0, \
    (embd3d_real_t)0, (embd3d_real_t)0, (embd3d_real_t)0, (embd3d_real_t)1  \
}

#define EMBD3D_PI 3.14159265358979323846
/* degrees -> radians */
#define EMBD3D_RAD(a) ((embd3d_real_t)(a) * (embd3d_real_t)(EMBD3D_PI / 180.0))

/* Every function takes the destination FIRST (the old lib took the source
 * first, which is what made the transpose() bug possible). */

void embd3d_mat4_identity(embd3d_mat4_t m);
void embd3d_mat4_copy(embd3d_mat4_t dst, const embd3d_mat4_t src);
/* dst = a * b  (dst may alias a or b) */
void embd3d_mat4_mul(embd3d_mat4_t dst, const embd3d_mat4_t a, const embd3d_mat4_t b);
/* m = m * b */
void embd3d_mat4_mul_inplace(embd3d_mat4_t m, const embd3d_mat4_t b);
void embd3d_mat4_transpose(embd3d_mat4_t m);

/* fov_y in DEGREES (the old code fed 60 straight into tan() as radians and
 * used tan() where it needed 1/tan()). */
void embd3d_mat4_perspective(embd3d_mat4_t m, embd3d_real_t fov_y_deg,
                           embd3d_real_t aspect, embd3d_real_t znear, embd3d_real_t zfar);
void embd3d_mat4_ortho(embd3d_mat4_t m, embd3d_real_t l, embd3d_real_t r,
                     embd3d_real_t b, embd3d_real_t t,
                     embd3d_real_t znear, embd3d_real_t zfar);

void embd3d_mat4_translation(embd3d_mat4_t m, embd3d_real_t x, embd3d_real_t y, embd3d_real_t z);
void embd3d_mat4_scaling(embd3d_mat4_t m, embd3d_real_t x, embd3d_real_t y, embd3d_real_t z);
void embd3d_mat4_rotation_x(embd3d_mat4_t m, embd3d_real_t deg);
void embd3d_mat4_rotation_y(embd3d_mat4_t m, embd3d_real_t deg);
void embd3d_mat4_rotation_z(embd3d_mat4_t m, embd3d_real_t deg);
/* Right handed rotation around an arbitrary axis (Rodrigues). */
void embd3d_mat4_rotation(embd3d_mat4_t m, const embd3d_vec3_t *axis, embd3d_real_t deg);

void embd3d_mat4_look_at(embd3d_mat4_t m, const embd3d_vec3_t *eye,
                       const embd3d_vec3_t *target, const embd3d_vec3_t *up);

/* p' = p * M, perspective divided by w. */
void embd3d_mat4_transform_point(embd3d_vec3_t *p, const embd3d_mat4_t m);
/* Direction (w = 0): rotation/scale only, no translation. */

void embd3d_mat4_transform_dir(embd3d_vec3_t *d, const embd3d_mat4_t m);

#ifdef __cplusplus
}
#endif


#endif /* EMBD3D_MAT4_H */
