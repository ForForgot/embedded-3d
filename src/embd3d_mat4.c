#include "embd3d_mat4.h"

#include <math.h>

static inline embd3d_real_t embd3d_sin(embd3d_real_t deg) {
    return (embd3d_real_t)sin((double)EMBD3D_RAD(deg));
}
static inline embd3d_real_t embd3d_cos(embd3d_real_t deg) {
    return (embd3d_real_t)cos((double)EMBD3D_RAD(deg));
}

void embd3d_mat4_identity(embd3d_mat4_t m) {
    embd3d_mat4_t id = EMBD3D_MAT4_IDENTITY;
    embd3d_mat4_copy(m, id);
}

void embd3d_mat4_copy(embd3d_mat4_t dst, const embd3d_mat4_t src) {
    for (unsigned i = 0; i < 16u; i++) {
        dst[i] = src[i];
    }
}

void embd3d_mat4_mul(embd3d_mat4_t dst, const embd3d_mat4_t a, const embd3d_mat4_t b) {
    embd3d_mat4_t tmp;
    for (unsigned r = 0; r < 4u; r++) {
        const embd3d_real_t *ar = a + r * 4u;
        for (unsigned col = 0; col < 4u; col++) {
            tmp[r * 4u + col] = ar[0] * b[col] +
                                ar[1] * b[4u + col] +
                                ar[2] * b[8u + col] +
                                ar[3] * b[12u + col];
        }
    }
    embd3d_mat4_copy(dst, tmp);
}

void embd3d_mat4_mul_inplace(embd3d_mat4_t m, const embd3d_mat4_t b) {
    embd3d_mat4_mul(m, m, b);
}

void embd3d_mat4_transpose(embd3d_mat4_t m) {
    for (unsigned r = 0; r < 4u; r++) {
        for (unsigned col = r + 1u; col < 4u; col++) {
            embd3d_real_t t = m[r * 4u + col];
            m[r * 4u + col] = m[col * 4u + r];
            m[col * 4u + r] = t;
        }
    }
}

void embd3d_mat4_perspective(embd3d_mat4_t m, embd3d_real_t fov_y_deg,
                           embd3d_real_t aspect, embd3d_real_t znear, embd3d_real_t zfar) {
    /* f = 1 / tan(fov/2)  -- the reciprocal the original code forgot. */
    embd3d_real_t f = (embd3d_real_t)1 /
                    (embd3d_real_t)tan((double)(EMBD3D_RAD(fov_y_deg) * (embd3d_real_t)0.5));
    m[0]  = f / aspect;
    m[1]  = 0;
    m[2]  = 0;
    m[3]  = 0;
    m[4]  = 0;
    m[5]  = f;
    m[6]  = 0;
    m[7]  = 0;
    m[8]  = 0;
    m[9]  = 0;
    m[10] = (zfar + znear) / (znear - zfar);
    m[11] = (embd3d_real_t)-1;
    m[12] = 0;
    m[13] = 0;
    m[14] = (embd3d_real_t)2 * zfar * znear / (znear - zfar);
    m[15] = 0;
}

void embd3d_mat4_ortho(embd3d_mat4_t m, embd3d_real_t l, embd3d_real_t r,
                     embd3d_real_t b, embd3d_real_t t,
                     embd3d_real_t znear, embd3d_real_t zfar) {
    embd3d_real_t rl = (embd3d_real_t)1 / (r - l);
    embd3d_real_t tb = (embd3d_real_t)1 / (t - b);
    embd3d_real_t fn = (embd3d_real_t)1 / (zfar - znear);

    m[0] = (embd3d_real_t)2 * rl; m[1] = 0;               m[2] = 0;               m[3] = 0;
    m[4] = 0;                   m[5] = (embd3d_real_t)2 * tb; m[6] = 0;           m[7] = 0;
    m[8] = 0;                   m[9] = 0;               m[10] = (embd3d_real_t)-2 * fn; m[11] = 0;
    m[12] = -(r + l) * rl;      m[13] = -(t + b) * tb;
    m[14] = -(zfar + znear) * fn; m[15] = (embd3d_real_t)1;
}

void embd3d_mat4_translation(embd3d_mat4_t m, embd3d_real_t x, embd3d_real_t y, embd3d_real_t z) {
    embd3d_mat4_t t = EMBD3D_MAT4_IDENTITY;
    t[12] = x;
    t[13] = y;
    t[14] = z;
    embd3d_mat4_copy(m, t);
}

void embd3d_mat4_scaling(embd3d_mat4_t m, embd3d_real_t x, embd3d_real_t y, embd3d_real_t z) {
    embd3d_mat4_t t = EMBD3D_MAT4_IDENTITY;
    t[0] = x;
    t[5] = y;
    t[10] = z;
    embd3d_mat4_copy(m, t);
}

/* All rotations are the transpose of the textbook column-vector matrix,
 * which is exactly what a row-vector pipeline needs. */
void embd3d_mat4_rotation_x(embd3d_mat4_t m, embd3d_real_t deg) {
    embd3d_real_t s = embd3d_sin(deg);
    embd3d_real_t c = embd3d_cos(deg);
    embd3d_mat4_t t = EMBD3D_MAT4_IDENTITY;
    t[5] = c;  t[6] = s;
    t[9] = -s; t[10] = c;
    embd3d_mat4_copy(m, t);
}

void embd3d_mat4_rotation_y(embd3d_mat4_t m, embd3d_real_t deg) {
    embd3d_real_t s = embd3d_sin(deg);
    embd3d_real_t c = embd3d_cos(deg);
    embd3d_mat4_t t = EMBD3D_MAT4_IDENTITY;
    t[0] = c; t[2] = -s;
    t[8] = s; t[10] = c;
    embd3d_mat4_copy(m, t);
}

void embd3d_mat4_rotation_z(embd3d_mat4_t m, embd3d_real_t deg) {
    embd3d_real_t s = embd3d_sin(deg);
    embd3d_real_t c = embd3d_cos(deg);
    embd3d_mat4_t t = EMBD3D_MAT4_IDENTITY;
    t[0] = c;  t[1] = s;
    t[4] = -s; t[5] = c;
    embd3d_mat4_copy(m, t);
}

void embd3d_mat4_rotation(embd3d_mat4_t m, const embd3d_vec3_t *axis, embd3d_real_t deg) {
    embd3d_vec3_t n = embd3d_vec3_normalize(axis);
    embd3d_real_t s = embd3d_sin(deg);
    embd3d_real_t c = embd3d_cos(deg);
    embd3d_real_t ic = (embd3d_real_t)1 - c;

    /* transpose of the column-vector Rodrigues matrix */
    embd3d_mat4_t t;
    t[0]  = c + n.x * n.x * ic;
    t[1]  = n.x * n.y * ic + n.z * s;
    t[2]  = n.x * n.z * ic - n.y * s;
    t[3]  = 0;
    t[4]  = n.y * n.x * ic - n.z * s;
    t[5]  = c + n.y * n.y * ic;
    t[6]  = n.y * n.z * ic + n.x * s;
    t[7]  = 0;
    t[8]  = n.z * n.x * ic + n.y * s;
    t[9]  = n.z * n.y * ic - n.x * s;
    t[10] = c + n.z * n.z * ic;
    t[11] = 0;
    t[12] = 0; t[13] = 0; t[14] = 0; t[15] = 1;
    embd3d_mat4_copy(m, t);
}

void embd3d_mat4_look_at(embd3d_mat4_t m, const embd3d_vec3_t *eye,
                       const embd3d_vec3_t *target, const embd3d_vec3_t *up) {
    embd3d_vec3_t f = embd3d_vec3_normalize(&(embd3d_vec3_t){
        target->x - eye->x, target->y - eye->y, target->z - eye->z });
    embd3d_vec3_t s = embd3d_vec3_normalize(&(embd3d_vec3_t){
        f.y * up->z - f.z * up->y,
        f.z * up->x - f.x * up->z,
        f.x * up->y - f.y * up->x });
    embd3d_vec3_t u = embd3d_vec3_cross(&s, &f);

    m[0]  = s.x;  m[1]  = u.x;  m[2]  = -f.x; m[3]  = 0;
    m[4]  = s.y;  m[5]  = u.y;  m[6]  = -f.y; m[7]  = 0;
    m[8]  = s.z;  m[9]  = u.z;  m[10] = -f.z; m[11] = 0;
    m[12] = -(s.x * eye->x + s.y * eye->y + s.z * eye->z);
    m[13] = -(u.x * eye->x + u.y * eye->y + u.z * eye->z);
    m[14] = f.x * eye->x + f.y * eye->y + f.z * eye->z;
    m[15] = 1;
}

void embd3d_mat4_transform_point(embd3d_vec3_t *p, const embd3d_mat4_t m) {
    embd3d_real_t x = p->x, y = p->y, z = p->z;
    embd3d_real_t w = x * m[3] + y * m[7] + z * m[11] + m[15];
    if (w != (embd3d_real_t)0 && w != (embd3d_real_t)1) {
        w = (embd3d_real_t)1 / w;
    } else {
        w = (embd3d_real_t)1;
    }
    p->x = (x * m[0] + y * m[4] + z * m[8] + m[12]) * w;
    p->y = (x * m[1] + y * m[5] + z * m[9] + m[13]) * w;
    p->z = (x * m[2] + y * m[6] + z * m[10] + m[14]) * w;
}

void embd3d_mat4_transform_dir(embd3d_vec3_t *d, const embd3d_mat4_t m) {
    embd3d_real_t x = d->x, y = d->y, z = d->z;
    d->x = x * m[0] + y * m[4] + z * m[8];
    d->y = x * m[1] + y * m[5] + z * m[9];
    d->z = x * m[2] + y * m[6] + z * m[10];
}
