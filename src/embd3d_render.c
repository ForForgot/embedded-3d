#include "embd3d_render.h"

#include <stddef.h>
#include <string.h>

#include "embd3d_fixed.h"
#include "embd3d_raster.h"

#if EMBD3D_ENABLE_LIGHTING
#include <math.h>
#endif

/* ===================================================================== *
 * Shared pieces: cache layout, emit, shading
 * ===================================================================== */

/* One cache slot is 28 bytes for both pipelines: 16 bytes of clip space,
 * 8 bytes of screen position, 2 bytes of depth, 2 bytes of validity stamp. */
typedef struct {
    int32_t c[4];
    int32_t s[2];
    uint16_t depth;
    uint16_t stamp;
} embd3d_cache_layout_t;

typedef char embd3d_cache_layout_must_be_28[
    sizeof(embd3d_cache_layout_t) == EMBD3D_VERTEX_CACHE_STRIDE ? 1 : -1];

#define EMBD3D_STAMP_OFFSET ((size_t)offsetof(embd3d_cache_layout_t, stamp))

typedef struct {
    embd3d_real_t cx, cy, cz, cw;
    int32_t sx, sy;
    uint16_t depth;
    uint16_t stamp;
} embd3d_cache_f_t;

#if EMBD3D_ENABLE_FIXED
typedef struct {
    embd3d_fx_t cx, cy, cz, cw;
    int32_t sx, sy;
    uint16_t depth;
    uint16_t stamp;
} embd3d_cache_fx_t;
#endif

#if EMBD3D_ENABLE_VERTEX_CACHE
/* Bumps the per-call stamp.  Must be called exactly once per draw call: a
 * cached vertex is only reused inside the call that produced it. */
static uint16_t cache_begin(embd3d_vertex_cache_t *c) {
    c->stamp++;
    if (c->stamp == 0) {
        /* 65535 calls later, stale stamps would collide: wipe them. */
        c->stamp = 1;
        for (uint16_t i = 0; i < c->capacity; i++) {
            *(uint16_t *)((uint8_t *)c->data + (size_t)i * EMBD3D_VERTEX_CACHE_STRIDE +
                          EMBD3D_STAMP_OFFSET) = 0;
        }
    }
    return c->stamp;
}
#endif /* EMBD3D_ENABLE_VERTEX_CACHE */




static void emit_tri(embd3d_framebuffer_t *fb, const embd3d_screen_vertex_t *sv, int n,
                     int wire, embd3d_cull_t cull) {
    if (wire) {
        for (int i = 0; i < n; i++) {
            const embd3d_screen_vertex_t *a = &sv[i];
            const embd3d_screen_vertex_t *b = &sv[(i + 1) % n];
            embd3d_raster_line(fb, a->x, a->y, a->depth, b->x, b->y, b->depth, a->color);
        }
    } else {
        for (int i = 1; i + 1 < n; i++) {
            embd3d_raster_triangle(fb, &sv[0], &sv[i], &sv[i + 1], cull);
        }
    }
}

#if EMBD3D_ENABLE_LIGHTING && EMBD3D_ENABLE_FLOAT
static embd3d_color_t shade_face(const embd3d_render_ctx_t *ctx, embd3d_color_t base,
                               const embd3d_real_t *p0, const embd3d_real_t *p1,
                               const embd3d_real_t *p2, const embd3d_mat4_t model) {
    embd3d_vec3_t a = { p0[0], p0[1], p0[2] };
    embd3d_vec3_t b = { p1[0], p1[1], p1[2] };
    embd3d_vec3_t c = { p2[0], p2[1], p2[2] };
    embd3d_vec3_t e1 = embd3d_vec3_sub(&b, &a);
    embd3d_vec3_t e2 = embd3d_vec3_sub(&c, &a);
    embd3d_vec3_t nrm = embd3d_vec3_cross(&e1, &e2);

    if (model != 0) {
        embd3d_mat4_transform_dir(&nrm, model);
    }
    nrm = embd3d_vec3_normalize(&nrm);

    embd3d_real_t ndl = embd3d_vec3_dot(&nrm, &ctx->light_dir);
    if (ndl < (embd3d_real_t)0) {
        ndl = -ndl;
    }
    embd3d_real_t s = ctx->ambient + ((embd3d_real_t)1 - ctx->ambient) * ndl;
    if (s > (embd3d_real_t)1) {
        s = (embd3d_real_t)1;
    }
    return embd3d_color_shade(base, (uint16_t)(s * (embd3d_real_t)256));
}
#endif

/* ===================================================================== *
 * Float vertex pipeline
 * ===================================================================== */

#if EMBD3D_ENABLE_FLOAT

typedef struct {
    embd3d_real_t x, y, z, w;
} clip_f_t;

static inline void xform_f(clip_f_t *o, const embd3d_real_t *p, const embd3d_mat4_t m) {
    const embd3d_real_t x = p[0], y = p[1], z = p[2];
    o->x = x * m[0] + y * m[4] + z * m[8] + m[12];
    o->y = x * m[1] + y * m[5] + z * m[9] + m[13];
    o->z = x * m[2] + y * m[6] + z * m[10] + m[14];
    o->w = x * m[3] + y * m[7] + z * m[11] + m[15];
}

static void project_f(const clip_f_t *v, const embd3d_framebuffer_t *fb,
                      const embd3d_render_ctx_t *ctx, embd3d_screen_vertex_t *out,
                      embd3d_color_t color) {
    embd3d_real_t w = v->w;
    if (w < (embd3d_real_t)1e-6) {
        w = (embd3d_real_t)1e-6;
    }
    embd3d_real_t iw = (embd3d_real_t)1 / w;
    embd3d_real_t nx = v->x * iw;
    embd3d_real_t ny = v->y * iw;

    /* guard band: keeps the integer rasterizer inside its int32 budget */
    if (nx > (embd3d_real_t)4) {
        nx = (embd3d_real_t)4;
    } else if (nx < (embd3d_real_t)-4) {
        nx = (embd3d_real_t)-4;
    }
    if (ny > (embd3d_real_t)4) {
        ny = (embd3d_real_t)4;
    } else if (ny < (embd3d_real_t)-4) {
        ny = (embd3d_real_t)-4;
    }

    out->x = (int32_t)((nx * (embd3d_real_t)0.5 + (embd3d_real_t)0.5) *
                       (embd3d_real_t)fb->width * (embd3d_real_t)EMBD3D_SUBPIXEL);
    out->y = (int32_t)(((embd3d_real_t)0.5 - ny * (embd3d_real_t)0.5) *
                       (embd3d_real_t)fb->height * (embd3d_real_t)EMBD3D_SUBPIXEL);

    embd3d_real_t d = iw * ctx->depth_k;
    if (d < (embd3d_real_t)0) {
        d = (embd3d_real_t)0;
    } else if (d > (embd3d_real_t)EMBD3D_DEPTH_MAX) {
        d = (embd3d_real_t)EMBD3D_DEPTH_MAX;
    }
    out->depth = (embd3d_depth_t)d;
    out->color = color;
}

static int inside_f(const clip_f_t *c, int full) {
    for (int i = 0; i < 3; i++) {
        if (c[i].z + c[i].w < (embd3d_real_t)0) {
            return 0;
        }
        if (full) {
            if (c[i].w - c[i].z < (embd3d_real_t)0) return 0;
            if (c[i].x + c[i].w < (embd3d_real_t)0) return 0;
            if (c[i].w - c[i].x < (embd3d_real_t)0) return 0;
            if (c[i].y + c[i].w < (embd3d_real_t)0) return 0;
            if (c[i].w - c[i].y < (embd3d_real_t)0) return 0;
        }
    }
    return 1;
}

static int clip_near_f(const clip_f_t *in, clip_f_t *out) {
    int n = 0;
    for (int i = 0; i < 3; i++) {
        const clip_f_t *cur = &in[i];
        const clip_f_t *nxt = &in[(i + 1) % 3];
        embd3d_real_t dc = cur->z + cur->w;
        embd3d_real_t dn = nxt->z + nxt->w;
        if (dc >= (embd3d_real_t)0) {
            out[n++] = *cur;
        }
        if ((dc >= (embd3d_real_t)0) != (dn >= (embd3d_real_t)0)) {
            embd3d_real_t t = dc / (dc - dn);
            out[n].x = cur->x + (nxt->x - cur->x) * t;
            out[n].y = cur->y + (nxt->y - cur->y) * t;
            out[n].z = cur->z + (nxt->z - cur->z) * t;
            out[n].w = cur->w + (nxt->w - cur->w) * t;
            n++;
        }
    }
    return n;
}

#if EMBD3D_ENABLE_FULL_CLIP
/* Six half spaces in clip space, (a,b,c,d) meaning a*x+b*y+c*z+d*w >= 0. */
static const embd3d_real_t k_planes_f[6][4] = {
    { 0, 0, 1, 1 },    /* near   */
    { 0, 0, -1, 1 },   /* far    */
    { 1, 0, 0, 1 },    /* left   */
    { -1, 0, 0, 1 },   /* right  */
    { 0, 1, 0, 1 },    /* bottom */
    { 0, -1, 0, 1 },   /* top    */
};

static int clip_half_f(const clip_f_t *in, int n, const embd3d_real_t pl[4],
                       clip_f_t *out) {
    int m = 0;
    for (int i = 0; i < n; i++) {
        const clip_f_t *cur = &in[i];
        const clip_f_t *nxt = &in[(i + 1) % n];
        embd3d_real_t dc = pl[0] * cur->x + pl[1] * cur->y + pl[2] * cur->z + pl[3] * cur->w;
        embd3d_real_t dn = pl[0] * nxt->x + pl[1] * nxt->y + pl[2] * nxt->z + pl[3] * nxt->w;
        if (dc >= (embd3d_real_t)0) {
            out[m++] = *cur;
        }
        if ((dc >= (embd3d_real_t)0) != (dn >= (embd3d_real_t)0)) {
            embd3d_real_t t = dc / (dc - dn);
            out[m].x = cur->x + (nxt->x - cur->x) * t;
            out[m].y = cur->y + (nxt->y - cur->y) * t;
            out[m].z = cur->z + (nxt->z - cur->z) * t;
            out[m].w = cur->w + (nxt->w - cur->w) * t;
            m++;
        }
    }
    return m;
}

static int clip_frustum_f(const clip_f_t *in, clip_f_t *out) {
    clip_f_t a[EMBD3D_MAX_CLIP_VERTS];
    clip_f_t b[EMBD3D_MAX_CLIP_VERTS];
    for (int i = 0; i < 3; i++) {
        a[i] = in[i];
    }
    int n = 3;
    clip_f_t *cur = a, *nxt = b;
    for (int p = 0; p < 6; p++) {
        n = clip_half_f(cur, n, k_planes_f[p], nxt);
        if (n < 3) {
            return 0;
        }
        clip_f_t *t = cur;
        cur = nxt;
        nxt = t;
    }
    for (int i = 0; i < n; i++) {
        out[i] = cur[i];
    }
    return n;
}
#endif /* EMBD3D_ENABLE_FULL_CLIP */

static int clip_project_f(const clip_f_t *in, const embd3d_framebuffer_t *fb,
                          const embd3d_render_ctx_t *ctx, embd3d_color_t color,
                          embd3d_screen_vertex_t *sv, int full) {
#if EMBD3D_ENABLE_FULL_CLIP
    if (full) {
        clip_f_t poly[EMBD3D_MAX_CLIP_VERTS];
        int n = clip_frustum_f(in, poly);
        for (int i = 0; i < n; i++) {
            project_f(&poly[i], fb, ctx, &sv[i], color);
        }
        return n;
    }
#else
    (void)full;
#endif
    {
        clip_f_t poly[4];
        int n = clip_near_f(in, poly);
        for (int i = 0; i < n; i++) {
            project_f(&poly[i], fb, ctx, &sv[i], color);
        }
        return n;
    }
}

static void draw_f_impl(embd3d_framebuffer_t *fb, const embd3d_mesh_t *mesh,
                        const embd3d_mat4_t mvp, const embd3d_mat4_t model,
                        const embd3d_render_ctx_t *ctx, embd3d_vertex_cache_t *cache) {
    if (mesh == 0 || mesh->indices == 0 || mesh->positions == 0 ||
        mesh->triangle_count == 0) {
        return;
    }

    const int wire = (ctx->flags & EMBD3D_FLAG_WIREFRAME) != 0;
    const int full = (ctx->flags & EMBD3D_FLAG_FULL_CLIP) != 0;
#if EMBD3D_ENABLE_LIGHTING
    const int unlit = (ctx->flags & EMBD3D_FLAG_UNLIT) != 0;
#else
    const int unlit = 1;
    (void)model;                    /* lighting is compiled out: no normal work */
#endif
    const embd3d_cull_t cull =
        (!wire && (ctx->flags & EMBD3D_FLAG_CULL_BACK)) ? EMBD3D_CULL_BACK : EMBD3D_CULL_NONE;

    embd3d_cache_f_t *ce = 0;
    uint16_t stamp = 0;
#if EMBD3D_ENABLE_VERTEX_CACHE
    if (cache != 0 && cache->data != 0 && cache->capacity >= mesh->vertex_count) {
        ce = (embd3d_cache_f_t *)cache->data;
        stamp = cache_begin(cache);
    }
#else
    (void)cache;
#endif

    for (uint32_t t = 0; t < (uint32_t)mesh->triangle_count; t++) {
        const embd3d_index_t *idx = &mesh->indices[t * 3u];
        const embd3d_real_t *ps[3];
        ps[0] = &mesh->positions[(uint32_t)idx[0] * 3u];
        ps[1] = &mesh->positions[(uint32_t)idx[1] * 3u];
        ps[2] = &mesh->positions[(uint32_t)idx[2] * 3u];

        embd3d_color_t color;
        if (unlit) {
            color = mesh->color;
        } else {
#if EMBD3D_ENABLE_LIGHTING
            color = shade_face(ctx, mesh->color, ps[0], ps[1], ps[2], model);
#else
            color = mesh->color;
#endif
        }

        clip_f_t clip[3];
        embd3d_screen_vertex_t pre[3];
        for (int k = 0; k < 3; k++) {
            uint16_t vi = idx[k];
            if (ce != 0 && ce[vi].stamp == stamp) {
                clip[k].x = ce[vi].cx;
                clip[k].y = ce[vi].cy;
                clip[k].z = ce[vi].cz;
                clip[k].w = ce[vi].cw;
                pre[k].x = ce[vi].sx;
                pre[k].y = ce[vi].sy;
                pre[k].depth = (embd3d_depth_t)ce[vi].depth;
            } else {
                xform_f(&clip[k], ps[k], mvp);
                project_f(&clip[k], fb, ctx, &pre[k], color);
                if (ce != 0) {
                    ce[vi].cx = clip[k].x;
                    ce[vi].cy = clip[k].y;
                    ce[vi].cz = clip[k].z;
                    ce[vi].cw = clip[k].w;
                    ce[vi].sx = pre[k].x;
                    ce[vi].sy = pre[k].y;
                    ce[vi].depth = (uint16_t)pre[k].depth;
                    ce[vi].stamp = stamp;
                }
            }
            pre[k].color = color;   /* per face colour, never cached */
        }

        embd3d_screen_vertex_t sv[EMBD3D_MAX_CLIP_VERTS];
        int n;
        if (inside_f(clip, full)) {
            sv[0] = pre[0];
            sv[1] = pre[1];
            sv[2] = pre[2];
            n = 3;
        } else {
            n = clip_project_f(clip, fb, ctx, color, sv, full);
            if (n < 3) {
                continue;
            }
        }
        if ((ctx->flags & EMBD3D_FLAG_DEPTH_TINT) != 0) {
            uint32_t d = ((uint32_t)pre[0].depth + (uint32_t)pre[1].depth +
                          (uint32_t)pre[2].depth) / 3u;
            uint16_t q = (uint16_t)(96u + (d * 160u) / EMBD3D_DEPTH_MAX);
            embd3d_color_t tinted = embd3d_color_shade(mesh->color, q);
            for (int i = 0; i < n; i++) {
                sv[i].color = tinted;
            }
        }
        emit_tri(fb, sv, n, wire, cull);
    }
}

#endif /* EMBD3D_ENABLE_FLOAT */

/* ===================================================================== *
 * Q16.16 vertex pipeline
 * ===================================================================== */

#if EMBD3D_ENABLE_FIXED

typedef struct {
    embd3d_fx_t x, y, z, w;
} clip_fx_t;

static inline void xform_fx(clip_fx_t *o, const embd3d_fx_t *p, const embd3d_mat4_fx_t m) {
    const int64_t x = p[0], y = p[1], z = p[2];
    o->x = (embd3d_fx_t)((x * m[0] + y * m[4] + z * m[8] + ((int64_t)m[12] << 16)) >> 16);
    o->y = (embd3d_fx_t)((x * m[1] + y * m[5] + z * m[9] + ((int64_t)m[13] << 16)) >> 16);
    o->z = (embd3d_fx_t)((x * m[2] + y * m[6] + z * m[10] + ((int64_t)m[14] << 16)) >> 16);
    o->w = (embd3d_fx_t)((x * m[3] + y * m[7] + z * m[11] + ((int64_t)m[15] << 16)) >> 16);
}

static void project_fx(const clip_fx_t *v, const embd3d_framebuffer_t *fb,
                       const embd3d_render_ctx_t *ctx, embd3d_screen_vertex_t *out,
                       embd3d_color_t color) {
    embd3d_fx_t w = v->w;
    if (w <= 0) {
        w = 1;
    }
    /* Q16.16 reciprocal: (1 << 32) / (w * 65536) == 65536 / w */
    embd3d_fx_t iw = (embd3d_fx_t)(((int64_t)1 << 32) / w);

    int32_t nx = (int32_t)(((int64_t)v->x * iw) >> 16);
    int32_t ny = (int32_t)(((int64_t)v->y * iw) >> 16);
    const int32_t lim = 4 * EMBD3D_FX_ONE;
    if (nx > lim) {
        nx = lim;
    } else if (nx < -lim) {
        nx = -lim;
    }
    if (ny > lim) {
        ny = lim;
    } else if (ny < -lim) {
        ny = -lim;
    }

    /* n in [-1,1] -> t in [0,1] (y flipped), both Q16.16, then to subpixels */
    int32_t tx = (nx >> 1) + EMBD3D_FX_HALF;
    int32_t ty = EMBD3D_FX_HALF - (ny >> 1);
    out->x = (int32_t)(((int64_t)tx * fb->width) >> (16 - EMBD3D_SUBPIXEL_BITS));
    out->y = (int32_t)(((int64_t)ty * fb->height) >> (16 - EMBD3D_SUBPIXEL_BITS));

    int32_t d = (int32_t)(((int64_t)iw * ctx->depth_k_i) >> 16);
    if (d < 0) {
        d = 0;
    } else if (d > (int32_t)EMBD3D_DEPTH_MAX) {
        d = (int32_t)EMBD3D_DEPTH_MAX;
    }
    out->depth = (embd3d_depth_t)d;
    out->color = color;
}

static int inside_fx(const clip_fx_t *c, int full) {
    for (int i = 0; i < 3; i++) {
        if (c[i].z + c[i].w < 0) {
            return 0;
        }
        if (full) {
            if (c[i].w - c[i].z < 0) return 0;
            if (c[i].x + c[i].w < 0) return 0;
            if (c[i].w - c[i].x < 0) return 0;
            if (c[i].y + c[i].w < 0) return 0;
            if (c[i].w - c[i].y < 0) return 0;
        }
    }
    return 1;
}

static int clip_near_fx(const clip_fx_t *in, clip_fx_t *out) {
    int n = 0;
    for (int i = 0; i < 3; i++) {
        const clip_fx_t *cur = &in[i];
        const clip_fx_t *nxt = &in[(i + 1) % 3];
        embd3d_fx_t dc = cur->z + cur->w;
        embd3d_fx_t dn = nxt->z + nxt->w;
        if (dc >= 0) {
            out[n++] = *cur;
        }
        if ((dc >= 0) != (dn >= 0)) {
            /* t = dc / (dc - dn) in Q16.16 */
            embd3d_fx_t t = (embd3d_fx_t)(((int64_t)dc << 16) / (dc - dn));
            out[n].x = cur->x + (embd3d_fx_t)(((int64_t)(nxt->x - cur->x) * t) >> 16);
            out[n].y = cur->y + (embd3d_fx_t)(((int64_t)(nxt->y - cur->y) * t) >> 16);
            out[n].z = cur->z + (embd3d_fx_t)(((int64_t)(nxt->z - cur->z) * t) >> 16);
            out[n].w = cur->w + (embd3d_fx_t)(((int64_t)(nxt->w - cur->w) * t) >> 16);
            n++;
        }
    }
    return n;
}

#if EMBD3D_ENABLE_FULL_CLIP
static const embd3d_fx_t k_planes_fx[6][4] = {
    { 0, 0, EMBD3D_FX_ONE, EMBD3D_FX_ONE },
    { 0, 0, -EMBD3D_FX_ONE, EMBD3D_FX_ONE },
    { EMBD3D_FX_ONE, 0, 0, EMBD3D_FX_ONE },
    { -EMBD3D_FX_ONE, 0, 0, EMBD3D_FX_ONE },
    { 0, EMBD3D_FX_ONE, 0, EMBD3D_FX_ONE },
    { 0, -EMBD3D_FX_ONE, 0, EMBD3D_FX_ONE },
};

/* Same as clip_half_f but with the plane coefficients folded in: the values
 * are only 0 or +-ONE, so no multiplications are needed. */
static embd3d_fx_t plane_eval_fx(const clip_fx_t *v, const embd3d_fx_t pl[4]) {
    int64_t s = 0;
    s += (int64_t)pl[0] * v->x;
    s += (int64_t)pl[1] * v->y;
    s += (int64_t)pl[2] * v->z;
    s += (int64_t)pl[3] * v->w;
    return (embd3d_fx_t)(s >> 16);
}

static int clip_half_fx(const clip_fx_t *in, int n, const embd3d_fx_t pl[4],
                        clip_fx_t *out) {
    int m = 0;
    for (int i = 0; i < n; i++) {
        const clip_fx_t *cur = &in[i];
        const clip_fx_t *nxt = &in[(i + 1) % n];
        embd3d_fx_t dc = plane_eval_fx(cur, pl);
        embd3d_fx_t dn = plane_eval_fx(nxt, pl);
        if (dc >= 0) {
            out[m++] = *cur;
        }
        if ((dc >= 0) != (dn >= 0)) {
            embd3d_fx_t t = (embd3d_fx_t)(((int64_t)dc << 16) / (dc - dn));
            out[m].x = cur->x + (embd3d_fx_t)(((int64_t)(nxt->x - cur->x) * t) >> 16);
            out[m].y = cur->y + (embd3d_fx_t)(((int64_t)(nxt->y - cur->y) * t) >> 16);
            out[m].z = cur->z + (embd3d_fx_t)(((int64_t)(nxt->z - cur->z) * t) >> 16);
            out[m].w = cur->w + (embd3d_fx_t)(((int64_t)(nxt->w - cur->w) * t) >> 16);
            m++;
        }
    }
    return m;
}

static int clip_frustum_fx(const clip_fx_t *in, clip_fx_t *out) {
    clip_fx_t a[EMBD3D_MAX_CLIP_VERTS];
    clip_fx_t b[EMBD3D_MAX_CLIP_VERTS];
    for (int i = 0; i < 3; i++) {
        a[i] = in[i];
    }
    int n = 3;
    clip_fx_t *cur = a, *nxt = b;
    for (int p = 0; p < 6; p++) {
        n = clip_half_fx(cur, n, k_planes_fx[p], nxt);
        if (n < 3) {
            return 0;
        }
        clip_fx_t *t = cur;
        cur = nxt;
        nxt = t;
    }
    for (int i = 0; i < n; i++) {
        out[i] = cur[i];
    }
    return n;
}
#endif /* EMBD3D_ENABLE_FULL_CLIP */

static int clip_project_fx(const clip_fx_t *in, const embd3d_framebuffer_t *fb,
                           const embd3d_render_ctx_t *ctx, embd3d_color_t color,
                           embd3d_screen_vertex_t *sv, int full) {
#if EMBD3D_ENABLE_FULL_CLIP
    if (full) {
        clip_fx_t poly[EMBD3D_MAX_CLIP_VERTS];
        int n = clip_frustum_fx(in, poly);
        for (int i = 0; i < n; i++) {
            project_fx(&poly[i], fb, ctx, &sv[i], color);
        }
        return n;
    }
#else
    (void)full;
#endif
    {
        clip_fx_t poly[4];
        int n = clip_near_fx(in, poly);
        for (int i = 0; i < n; i++) {
            project_fx(&poly[i], fb, ctx, &sv[i], color);
        }
        return n;
    }
}

static void draw_fx_impl(embd3d_framebuffer_t *fb, const embd3d_mesh_fx_t *mesh,
                         const embd3d_mat4_fx_t mvp, const embd3d_render_ctx_t *ctx,
                         embd3d_vertex_cache_t *cache) {
    if (mesh == 0 || mesh->indices == 0 || mesh->positions == 0 ||
        mesh->triangle_count == 0) {
        return;
    }

    const int wire = (ctx->flags & EMBD3D_FLAG_WIREFRAME) != 0;
    const int full = (ctx->flags & EMBD3D_FLAG_FULL_CLIP) != 0;
    const embd3d_cull_t cull =
        (!wire && (ctx->flags & EMBD3D_FLAG_CULL_BACK)) ? EMBD3D_CULL_BACK : EMBD3D_CULL_NONE;

    embd3d_cache_fx_t *ce = 0;
    uint16_t stamp = 0;
#if EMBD3D_ENABLE_VERTEX_CACHE
    if (cache != 0 && cache->data != 0 && cache->capacity >= mesh->vertex_count) {
        ce = (embd3d_cache_fx_t *)cache->data;
        stamp = cache_begin(cache);
    }
#else
    (void)cache;
#endif

    /* The integer pipeline has no float positions to light from, so it always
     * draws with the mesh colour (equivalent to EMBD3D_FLAG_UNLIT). */
    const embd3d_color_t color = mesh->color;

    for (uint32_t t = 0; t < (uint32_t)mesh->triangle_count; t++) {
        const embd3d_index_t *idx = &mesh->indices[t * 3u];
        const embd3d_fx_t *ps[3];
        ps[0] = &mesh->positions[(uint32_t)idx[0] * 3u];
        ps[1] = &mesh->positions[(uint32_t)idx[1] * 3u];
        ps[2] = &mesh->positions[(uint32_t)idx[2] * 3u];

        clip_fx_t clip[3];
        embd3d_screen_vertex_t pre[3];
        for (int k = 0; k < 3; k++) {
            uint16_t vi = idx[k];
            if (ce != 0 && ce[vi].stamp == stamp) {
                clip[k].x = ce[vi].cx;
                clip[k].y = ce[vi].cy;
                clip[k].z = ce[vi].cz;
                clip[k].w = ce[vi].cw;
                pre[k].x = ce[vi].sx;
                pre[k].y = ce[vi].sy;
                pre[k].depth = (embd3d_depth_t)ce[vi].depth;
            } else {
                xform_fx(&clip[k], ps[k], mvp);
                project_fx(&clip[k], fb, ctx, &pre[k], color);
                if (ce != 0) {
                    ce[vi].cx = clip[k].x;
                    ce[vi].cy = clip[k].y;
                    ce[vi].cz = clip[k].z;
                    ce[vi].cw = clip[k].w;
                    ce[vi].sx = pre[k].x;
                    ce[vi].sy = pre[k].y;
                    ce[vi].depth = (uint16_t)pre[k].depth;
                    ce[vi].stamp = stamp;
                }
            }
            pre[k].color = color;
        }

        embd3d_screen_vertex_t sv[EMBD3D_MAX_CLIP_VERTS];
        int n;
        if (inside_fx(clip, full)) {
            sv[0] = pre[0];
            sv[1] = pre[1];
            sv[2] = pre[2];
            n = 3;
        } else {
            n = clip_project_fx(clip, fb, ctx, color, sv, full);
            if (n < 3) {
                continue;
            }
        }
        if ((ctx->flags & EMBD3D_FLAG_DEPTH_TINT) != 0) {
            uint32_t d = ((uint32_t)pre[0].depth + (uint32_t)pre[1].depth +
                          (uint32_t)pre[2].depth) / 3u;
            uint16_t q = (uint16_t)(96u + (d * 160u) / EMBD3D_DEPTH_MAX);
            embd3d_color_t tinted = embd3d_color_shade(mesh->color, q);
            for (int i = 0; i < n; i++) {
                sv[i].color = tinted;
            }
        }
        emit_tri(fb, sv, n, wire, cull);
    }
}

#endif /* EMBD3D_ENABLE_FIXED */

/* ===================================================================== *
 * Public API
 * ===================================================================== */

void embd3d_render_ctx_init(embd3d_render_ctx_t *ctx, embd3d_real_t znear, embd3d_real_t zfar) {
    embd3d_render_ctx_init_tier(ctx, EMBD3D_TIER_BALANCED, znear, zfar);
}

void embd3d_render_ctx_init_tier(embd3d_render_ctx_t *ctx, embd3d_tier_t tier,
                               embd3d_real_t znear, embd3d_real_t zfar) {
    ctx->znear = znear;
    ctx->zfar = zfar;
    /* Depth key is 1/w scaled by znear, so the near plane maps to the maximum
     * key and everything in front of it is a monotonic function of 1/w. */
    ctx->depth_k = znear * (embd3d_real_t)EMBD3D_DEPTH_MAX;
    ctx->depth_k_i = (int32_t)(znear * (embd3d_real_t)EMBD3D_DEPTH_MAX);
    ctx->ambient = (embd3d_real_t)0.25;
    ctx->light_dir = embd3d_vec3_normalize(&(embd3d_vec3_t){
        (embd3d_real_t)0.35, (embd3d_real_t)0.55, (embd3d_real_t)-0.75 });

    switch (tier) {
    case EMBD3D_TIER_MINIMAL:
        /* no lighting, but keep a depth cue so the silhouette still reads */
        ctx->flags = EMBD3D_FLAG_CULL_BACK | EMBD3D_FLAG_UNLIT | EMBD3D_FLAG_DEPTH_TINT;
        break;
    case EMBD3D_TIER_QUALITY:
        ctx->flags = EMBD3D_FLAG_CULL_BACK | EMBD3D_FLAG_FULL_CLIP;
        break;
    case EMBD3D_TIER_BALANCED:
    default:
        ctx->flags = EMBD3D_FLAG_CULL_BACK;
        break;
    }
}

const char *embd3d_tier_name(embd3d_tier_t tier) {
    switch (tier) {
    case EMBD3D_TIER_MINIMAL:
        return "minimal";
    case EMBD3D_TIER_BALANCED:
        return "balanced";
    case EMBD3D_TIER_QUALITY:
        return "quality";
    default:
        return "unknown";
    }
}

embd3d_precision_t embd3d_tier_precision(embd3d_tier_t tier) {
#if !EMBD3D_ENABLE_FLOAT
    (void)tier;
    return EMBD3D_PRECISION_FIXED;
#elif !EMBD3D_ENABLE_FIXED
    (void)tier;
    return EMBD3D_PRECISION_FLOAT;
#else
    return (tier == EMBD3D_TIER_MINIMAL) ? EMBD3D_PRECISION_FIXED : EMBD3D_PRECISION_FLOAT;
#endif
}

void embd3d_vertex_cache_init(embd3d_vertex_cache_t *cache, void *data, uint16_t capacity) {
    cache->data = data;
    cache->capacity = capacity;
    cache->stamp = 0;
#if EMBD3D_ENABLE_VERTEX_CACHE
    /* Wipe the validity stamps: the buffer may be recycled from an earlier
     * cache (possibly filled by the other pipeline, with a different layout),
     * and stamp numbering restarts at 1. */
    if (data != 0) {
        for (uint16_t i = 0; i < capacity; i++) {
            *(uint16_t *)((uint8_t *)data + (size_t)i * EMBD3D_VERTEX_CACHE_STRIDE +
                          EMBD3D_STAMP_OFFSET) = 0;
        }
    }
#endif
}

#if EMBD3D_ENABLE_FLOAT
void embd3d_draw_mesh(embd3d_framebuffer_t *fb, const embd3d_mesh_t *mesh,
                    const embd3d_mat4_t mvp, const embd3d_mat4_t model,
                    const embd3d_render_ctx_t *ctx) {
    draw_f_impl(fb, mesh, mvp, model, ctx, 0);
}

void embd3d_draw_mesh_cached(embd3d_framebuffer_t *fb, const embd3d_mesh_t *mesh,
                           const embd3d_mat4_t mvp, const embd3d_mat4_t model,
                           const embd3d_render_ctx_t *ctx,
                           embd3d_vertex_cache_t *cache) {
    draw_f_impl(fb, mesh, mvp, model, ctx, cache);
}
#endif

#if EMBD3D_ENABLE_FIXED
void embd3d_draw_mesh_fx(embd3d_framebuffer_t *fb, const embd3d_mesh_fx_t *mesh,
                       const embd3d_mat4_fx_t mvp, const embd3d_render_ctx_t *ctx) {
    draw_fx_impl(fb, mesh, mvp, ctx, 0);
}

void embd3d_draw_mesh_fx_cached(embd3d_framebuffer_t *fb, const embd3d_mesh_fx_t *mesh,
                              const embd3d_mat4_fx_t mvp, const embd3d_render_ctx_t *ctx,
                              embd3d_vertex_cache_t *cache) {
    draw_fx_impl(fb, mesh, mvp, ctx, cache);
}
#endif
