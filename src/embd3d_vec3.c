#include "embd3d_vec3.h"

#include <math.h>

/* Single wrapper so the scalar type can be swapped without touching the
 * call sites (sqrt() promotes float to double on most libms). */
static inline embd3d_real_t embd3d_sqrt(embd3d_real_t v) {
    return (embd3d_real_t)sqrt((double)v);
}

embd3d_real_t embd3d_vec3_dot(const embd3d_vec3_t *a, const embd3d_vec3_t *b) {
    return a->x * b->x + a->y * b->y + a->z * b->z;
}

embd3d_vec3_t embd3d_vec3_hadamard(const embd3d_vec3_t *a, const embd3d_vec3_t *b) {
    embd3d_vec3_t r;
    r.x = a->x * b->x;
    r.y = a->y * b->y;
    r.z = a->z * b->z;
    return r;
}

embd3d_real_t embd3d_vec3_length(const embd3d_vec3_t *a) {
    return embd3d_sqrt(embd3d_vec3_dot(a, a));
}

embd3d_vec3_t embd3d_vec3_normalize(const embd3d_vec3_t *a) {
    embd3d_real_t len = embd3d_vec3_length(a);
    embd3d_vec3_t r;
    if (len <= (embd3d_real_t)0) {
        r.x = r.y = r.z = (embd3d_real_t)0;
        return r;
    }
    len = (embd3d_real_t)1 / len;      /* one division instead of three */
    r.x = a->x * len;
    r.y = a->y * len;
    r.z = a->z * len;
    return r;
}

embd3d_vec3_t embd3d_vec3_add(const embd3d_vec3_t *a, const embd3d_vec3_t *b) {
    embd3d_vec3_t r;
    r.x = a->x + b->x;
    r.y = a->y + b->y;
    r.z = a->z + b->z;
    return r;
}

embd3d_vec3_t embd3d_vec3_sub(const embd3d_vec3_t *a, const embd3d_vec3_t *b) {
    embd3d_vec3_t r;
    r.x = a->x - b->x;
    r.y = a->y - b->y;
    r.z = a->z - b->z;
    return r;
}

embd3d_vec3_t embd3d_vec3_scale(const embd3d_vec3_t *a, embd3d_real_t s) {
    embd3d_vec3_t r;
    r.x = a->x * s;
    r.y = a->y * s;
    r.z = a->z * s;
    return r;
}

embd3d_vec3_t embd3d_vec3_cross(const embd3d_vec3_t *a, const embd3d_vec3_t *b) {
    embd3d_vec3_t r;
    r.x = a->y * b->z - a->z * b->y;
    r.y = a->z * b->x - a->x * b->z;
    r.z = a->x * b->y - a->y * b->x;
    return r;
}

embd3d_vec3_t embd3d_vec3_lerp(const embd3d_vec3_t *a, const embd3d_vec3_t *b, embd3d_real_t t) {
    embd3d_vec3_t r;
    r.x = a->x + (b->x - a->x) * t;
    r.y = a->y + (b->y - a->y) * t;
    r.z = a->z + (b->z - a->z) * t;
    return r;
}
