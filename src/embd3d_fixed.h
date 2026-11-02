#ifndef EMBD3D_FIXED_H
#define EMBD3D_FIXED_H

#include "embd3d_config.h"
#include "embd3d_mat4.h"
#include "embd3d_mesh.h"

#ifdef __cplusplus
extern "C" {
#endif

#if EMBD3D_ENABLE_FIXED

/* Q16.16 fixed point matrix, same row major / row-vector layout as
 * embd3d_mat4_t, so one converter call per frame is enough:
 *
 *     embd3d_mat4_t    mvp;         ...built in float...
 *     embd3d_mat4_fx_t mvp_fx;
 *     embd3d_mat4_to_fx(mvp_fx, mvp);
 *     embd3d_draw_mesh_fx(&fb, &mesh_fx, mvp_fx, &ctx);
 *
 * Range is +-32768 with a resolution of 1/65536, which covers a scene of a
 * few thousand units around the origin.  Scale your model into that range.
 */
typedef embd3d_fx_t embd3d_mat4_fx_t[16];

static inline embd3d_fx_t embd3d_fx_from_real(embd3d_real_t v) {
    return (embd3d_fx_t)(v * (embd3d_real_t)EMBD3D_FX_ONE);
}
static inline embd3d_fx_t embd3d_fx_from_int(int v) {
    return (embd3d_fx_t)(v * EMBD3D_FX_ONE);
}
static inline embd3d_real_t embd3d_fx_to_real(embd3d_fx_t v) {
    return (embd3d_real_t)v / (embd3d_real_t)EMBD3D_FX_ONE;
}
/* Q16.16 * Q16.16 -> Q16.16 */
static inline embd3d_fx_t embd3d_fx_mul(embd3d_fx_t a, embd3d_fx_t b) {
    return (embd3d_fx_t)(((int64_t)a * (int64_t)b) >> 16);
}

/* Float matrix -> Q16.16 matrix.  Do this once per frame, not per triangle. */
void embd3d_mat4_to_fx(embd3d_mat4_fx_t dst, const embd3d_mat4_t src);
void embd3d_mat4_fx_identity(embd3d_mat4_fx_t m);
void embd3d_mat4_fx_mul(embd3d_mat4_fx_t dst, const embd3d_mat4_fx_t a, const embd3d_mat4_fx_t b);

/* Converts a mesh's float positions in place into a caller owned Q16.16
 * array (3 values per vertex).  Do this once at load time. */
void embd3d_mesh_positions_to_fx(embd3d_fx_t *dst, const embd3d_real_t *src,
                               uint16_t vertex_count);

#endif /* EMBD3D_ENABLE_FIXED */

#ifdef __cplusplus
}
#endif

#endif /* EMBD3D_FIXED_H */
