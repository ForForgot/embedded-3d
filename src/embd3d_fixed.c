#include "embd3d_fixed.h"

#if EMBD3D_ENABLE_FIXED

static embd3d_fx_t to_fx_rounded(embd3d_real_t v) {
    embd3d_real_t s = v * (embd3d_real_t)EMBD3D_FX_ONE;
    return (embd3d_fx_t)(s >= (embd3d_real_t)0 ? s + (embd3d_real_t)0.5
                                          : s - (embd3d_real_t)0.5);
}

void embd3d_mat4_to_fx(embd3d_mat4_fx_t dst, const embd3d_mat4_t src) {
    for (unsigned i = 0; i < 16u; i++) {
        dst[i] = to_fx_rounded(src[i]);
    }
}

void embd3d_mat4_fx_identity(embd3d_mat4_fx_t m) {
    for (unsigned i = 0; i < 16u; i++) {
        m[i] = 0;
    }
    m[0] = EMBD3D_FX_ONE;
    m[5] = EMBD3D_FX_ONE;
    m[10] = EMBD3D_FX_ONE;
    m[15] = EMBD3D_FX_ONE;
}

void embd3d_mat4_fx_mul(embd3d_mat4_fx_t dst, const embd3d_mat4_fx_t a, const embd3d_mat4_fx_t b) {
    embd3d_mat4_fx_t tmp;
    for (unsigned r = 0; r < 4u; r++) {
        const embd3d_fx_t *ar = a + r * 4u;
        for (unsigned c = 0; c < 4u; c++) {
            int64_t s = (int64_t)ar[0] * b[c] + (int64_t)ar[1] * b[4u + c] +
                        (int64_t)ar[2] * b[8u + c] + (int64_t)ar[3] * b[12u + c];
            tmp[r * 4u + c] = (embd3d_fx_t)(s >> 16);
        }
    }
    for (unsigned i = 0; i < 16u; i++) {
        dst[i] = tmp[i];
    }
}

void embd3d_mesh_positions_to_fx(embd3d_fx_t *dst, const embd3d_real_t *src,
                               uint16_t vertex_count) {
    uint32_t n = (uint32_t)vertex_count * 3u;
    for (uint32_t i = 0; i < n; i++) {
        dst[i] = to_fx_rounded(src[i]);
    }
}

#endif /* EMBD3D_ENABLE_FIXED */
