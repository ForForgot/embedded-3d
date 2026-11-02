#ifndef EMBD3D_VEC3_H
#define EMBD3D_VEC3_H

#include "embd3d_config.h"


#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    embd3d_real_t x;
    embd3d_real_t y;
    embd3d_real_t z;
} embd3d_vec3_t;

static inline embd3d_vec3_t embd3d_vec3_make(embd3d_real_t x, embd3d_real_t y, embd3d_real_t z) {
    embd3d_vec3_t v;
    v.x = x;
    v.y = y;
    v.z = z;
    return v;
}

/* Real dot product.  NOTE: the old ol3d_vector_dot() returned the
 * component-wise (Hadamard) product; that behaviour now has its own name. */
embd3d_real_t embd3d_vec3_dot(const embd3d_vec3_t *a, const embd3d_vec3_t *b);
embd3d_vec3_t embd3d_vec3_hadamard(const embd3d_vec3_t *a, const embd3d_vec3_t *b);
embd3d_real_t embd3d_vec3_length(const embd3d_vec3_t *a);
embd3d_vec3_t embd3d_vec3_normalize(const embd3d_vec3_t *a);
embd3d_vec3_t embd3d_vec3_add(const embd3d_vec3_t *a, const embd3d_vec3_t *b);
embd3d_vec3_t embd3d_vec3_sub(const embd3d_vec3_t *a, const embd3d_vec3_t *b);
embd3d_vec3_t embd3d_vec3_scale(const embd3d_vec3_t *a, embd3d_real_t s);
embd3d_vec3_t embd3d_vec3_cross(const embd3d_vec3_t *a, const embd3d_vec3_t *b);

embd3d_vec3_t embd3d_vec3_lerp(const embd3d_vec3_t *a, const embd3d_vec3_t *b, embd3d_real_t t);

#ifdef __cplusplus
}
#endif


#endif /* EMBD3D_VEC3_H */
