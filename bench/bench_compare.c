/* A/B benchmark and tier comparison.
 *
 *   legacy 1.x  vs  new float   vs  new float+cache
 *               vs  new fixed   vs  new fixed+cache
 *
 * All variants render the same 1280 triangle sphere into the same 128x128
 * target with the same field of view, and every row reports how many
 * fragments it actually wrote, so "faster because it drew less" cannot hide.
 *
 * This is a HOST measurement (x86-64, GCC, SSE).  It compares algorithms; the
 * fixed point pipeline is slower here because this machine has a hardware FPU.
 * See docs/DESIGN.md for how to read the numbers on an FPU-less MCU.
 */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "legacy/ol3d_core.h"   /* old: ol3d_matrix_t / ol3d_draw_Element */
#include "embd3d.h"               /* new */

#define FB_W 128
#define FB_H 128
#define SPH_SEG 32
#define SPH_RING 20
#define SPH_VERT ((SPH_SEG + 1) * (SPH_RING + 1))
#define SPH_TRI (SPH_SEG * SPH_RING * 2)
#define FRAME_ITERS 300
#define MUL_ITERS 100000
#define MODEL_SCALE 180.0f

/* ------------------------------------------------------------- buffers -- */
static unsigned char legacy_color[FB_W * FB_H * 3];
static embd3d_pixel_t  new_color[FB_W * FB_H];
static embd3d_depth_t  new_depth[FB_W * FB_H];

#if EMBD3D_ENABLE_VERTEX_CACHE
static uint8_t cache_mem[EMBD3D_VERTEX_CACHE_STRIDE * SPH_VERT];
#endif

/* ----------------------------------------------------------- geometry --- */
static double legacy_pos[SPH_VERT * 3];
static double legacy_norm[SPH_VERT * 3];
static float  new_pos[SPH_VERT * 3];
static embd3d_fx_t fx_pos[SPH_VERT * 3];
static long   legacy_faces[SPH_TRI * 3 + 1];
static embd3d_index_t new_idx[SPH_TRI * 3];

static void make_sphere(void) {
    int v = 0;
    for (int i = 0; i <= SPH_RING; i++) {
        double phi = 3.14159265358979323846 * (double)i / (double)SPH_RING;
        for (int j = 0; j <= SPH_SEG; j++) {
            double th = 2.0 * 3.14159265358979323846 * (double)j / (double)SPH_SEG;
            double x = 1.2 * sin(phi) * cos(th);
            double y = 1.2 * cos(phi);
            double z = 1.2 * sin(phi) * sin(th);
            legacy_pos[v * 3 + 0] = x;
            legacy_pos[v * 3 + 1] = y;
            legacy_pos[v * 3 + 2] = z;
            legacy_norm[v * 3 + 0] = x;
            legacy_norm[v * 3 + 1] = y;
            legacy_norm[v * 3 + 2] = z;
            new_pos[v * 3 + 0] = (float)x;
            new_pos[v * 3 + 1] = (float)y;
            new_pos[v * 3 + 2] = (float)z;
            v++;
        }
    }
    int t = 0;
    for (int i = 0; i < SPH_RING; i++) {
        for (int j = 0; j < SPH_SEG; j++) {
            int a = i * (SPH_SEG + 1) + j;
            int b = a + SPH_SEG + 1;
            int tri[2][3];
            tri[0][0] = a;     tri[0][1] = b; tri[0][2] = a + 1;
            tri[1][0] = a + 1; tri[1][1] = b; tri[1][2] = b + 1;
            for (int k = 0; k < 2; k++) {
                for (int e = 0; e < 3; e++) {
                    legacy_faces[t * 3 + e] = (long)tri[k][e] + 1;   /* 1 based */
                    new_idx[t * 3 + e] = (embd3d_index_t)tri[k][e];    /* 0 based */
                }
                t++;
            }
        }
    }
    legacy_faces[t * 3] = 0;   /* legacy sentinel */
    embd3d_mesh_positions_to_fx(fx_pos, new_pos, (uint16_t)SPH_VERT);
}

static double now_ms(void) {
    return (double)clock() * 1000.0 / (double)CLOCKS_PER_SEC;
}

static long count_nz(const void *p, long bytes, long group) {
    const unsigned char *b = (const unsigned char *)p;
    long n = 0;
    for (long i = 0; i < bytes; i += group) {
        for (long k = 0; k < group; k++) {
            if (b[i + k]) {
                n++;
                break;
            }
        }
    }
    return n;
}

static double bench_mul_legacy(int iters) {
    ol3d_matrix_t a, b, c;
    ol3d_matrix_setTranslate(a, 1, 2, 3);
    ol3d_matrix_setRotate(b, 30, M_AXIS_Y);
    double t0 = now_ms();
    for (int i = 0; i < iters; i++) {
        ol3d_matrix_multiply(a, b, c);
        a[0] += c[0] * 1e-30f;
    }
    return (now_ms() - t0) / (double)iters;
}

static double bench_mul_new(int iters) {
    embd3d_mat4_t a, b, c;
    embd3d_mat4_translation(a, 1, 2, 3);
    embd3d_mat4_rotation_y(b, 30);
    double t0 = now_ms();
    for (int i = 0; i < iters; i++) {
        embd3d_mat4_mul(c, a, b);
        a[0] += c[0] * (embd3d_real_t)1e-30;
    }
    return (now_ms() - t0) / (double)iters;
}

int main(void) {
    make_sphere();

    /* ---------------- legacy matrices ---------------- */
    /* Legacy setPerspective() computes tan(fov/2) where it should compute
     * 1/tan(fov/2), and treats fov as radians.  Pre-warping the angle gives it
     * the same field of view, so the fragment counts are comparable. */
    const double want_f = 1.0 / tan(45.0 * 3.14159265358979323846 / 180.0 / 2.0);
    const double legacy_fov = 2.0 * atan(want_f);

    ol3d_matrix_t lp, lv, lvp, lry, lrx, ls, lm, lmvp;
    ol3d_matrix_setPerspective(lp, legacy_fov, 1, 1, 1000);
    ol3d_matrix_setTranslate(lv, 0, 0, -700);
    ol3d_matrix_multiply(lv, lp, lvp);
    ol3d_matrix_setScale(ls, MODEL_SCALE, MODEL_SCALE, MODEL_SCALE);
    ol3d_matrix_setRotate(lry, 35, M_AXIS_Y);
    ol3d_matrix_setRotate(lrx, 22, M_AXIS_X);
    ol3d_matrix_multiply(lry, lrx, lm);
    {
        ol3d_matrix_t t;
        ol3d_matrix_multiply(lm, ls, t);
        ol3d_matrix_copy(t, lm);
    }
    ol3d_matrix_multiply(lm, lvp, lmvp);

    /* ---------------- new matrices ---------------- */
    embd3d_framebuffer_t fb;
    embd3d_fb_init(&fb, new_color, new_depth, FB_W, FB_H);

    embd3d_render_ctx_t ctx;
    embd3d_render_ctx_init(&ctx, (embd3d_real_t)1, (embd3d_real_t)1000);
    ctx.flags = EMBD3D_FLAG_CULL_BACK | EMBD3D_FLAG_UNLIT;

    embd3d_mat4_t proj, view, vp, ry, rx, sc, m1, model, mvp;
    embd3d_mat4_perspective(proj, (embd3d_real_t)45, (embd3d_real_t)1,
                          (embd3d_real_t)1, (embd3d_real_t)1000);
    embd3d_mat4_translation(view, 0, 0, (embd3d_real_t)-700);
    embd3d_mat4_mul(vp, view, proj);
    embd3d_mat4_scaling(sc, (embd3d_real_t)MODEL_SCALE, (embd3d_real_t)MODEL_SCALE,
                      (embd3d_real_t)MODEL_SCALE);
    embd3d_mat4_rotation_y(ry, (embd3d_real_t)35);
    embd3d_mat4_rotation_x(rx, (embd3d_real_t)22);
    embd3d_mat4_mul(m1, ry, rx);
    embd3d_mat4_mul(model, m1, sc);
    embd3d_mat4_mul(mvp, model, vp);

    embd3d_mat4_fx_t mvp_fx;
    embd3d_mat4_to_fx(mvp_fx, mvp);

    embd3d_mesh_t mesh;
    mesh.positions = new_pos;
    mesh.indices = new_idx;
    mesh.vertex_count = (uint16_t)SPH_VERT;
    mesh.triangle_count = (uint16_t)SPH_TRI;
    mesh.color = EMBD3D_RGB(90, 170, 255);

    embd3d_mesh_fx_t mesh_fx;
    mesh_fx.positions = fx_pos;
    mesh_fx.indices = new_idx;
    mesh_fx.vertex_count = (uint16_t)SPH_VERT;
    mesh_fx.triangle_count = (uint16_t)SPH_TRI;
    mesh_fx.color = mesh.color;

    /* ---------------- legacy timing ---------------- */
    double t0 = now_ms();
    for (int i = 0; i < FRAME_ITERS; i++) {
        ol3d_clean_buffer(legacy_color);
        ol3d_draw_Element(legacy_color, legacy_faces, legacy_pos, legacy_norm,
                          (unsigned int)SPH_TRI, lmvp);
    }
    double t_legacy = (now_ms() - t0) / (double)FRAME_ITERS;
    long f_legacy = count_nz(legacy_color, (long)sizeof(legacy_color), 3);

    /* ---------------- new variants ---------------- */
    double t_float, t_float_cache, t_fixed, t_fixed_cache;
    long f_new;

    t0 = now_ms();
    for (int i = 0; i < FRAME_ITERS; i++) {
        embd3d_fb_clear(&fb, 0);
        embd3d_draw_mesh(&fb, &mesh, mvp, 0, &ctx);
    }
    t_float = (now_ms() - t0) / (double)FRAME_ITERS;
    f_new = count_nz(new_color, (long)sizeof(new_color), (long)sizeof(embd3d_pixel_t));

    t0 = now_ms();
    for (int i = 0; i < FRAME_ITERS; i++) {
        embd3d_fb_clear(&fb, 0);
        embd3d_draw_mesh(&fb, &mesh, mvp, 0, &ctx);
    }
    t_float = (now_ms() - t0) / (double)FRAME_ITERS;

#if EMBD3D_ENABLE_VERTEX_CACHE
    {
        embd3d_vertex_cache_t cache;
        embd3d_vertex_cache_init(&cache, cache_mem, (uint16_t)SPH_VERT);
        t0 = now_ms();
        for (int i = 0; i < FRAME_ITERS; i++) {
            embd3d_fb_clear(&fb, 0);
            embd3d_draw_mesh_cached(&fb, &mesh, mvp, 0, &ctx, &cache);
        }
        t_float_cache = (now_ms() - t0) / (double)FRAME_ITERS;
        f_new = count_nz(new_color, (long)sizeof(new_color), (long)sizeof(embd3d_pixel_t));
    }
#else
    t_float_cache = -1.0;
#endif

#if EMBD3D_ENABLE_FIXED
    t0 = now_ms();
    for (int i = 0; i < FRAME_ITERS; i++) {
        embd3d_fb_clear(&fb, 0);
        embd3d_draw_mesh_fx(&fb, &mesh_fx, mvp_fx, &ctx);
    }
    t_fixed = (now_ms() - t0) / (double)FRAME_ITERS;
    f_new = count_nz(new_color, (long)sizeof(new_color), (long)sizeof(embd3d_pixel_t));

#if EMBD3D_ENABLE_VERTEX_CACHE
    {
        embd3d_vertex_cache_t cache;
        embd3d_vertex_cache_init(&cache, cache_mem, (uint16_t)SPH_VERT);
        t0 = now_ms();
        for (int i = 0; i < FRAME_ITERS; i++) {
            embd3d_fb_clear(&fb, 0);
            embd3d_draw_mesh_fx_cached(&fb, &mesh_fx, mvp_fx, &ctx, &cache);
        }
        t_fixed_cache = (now_ms() - t0) / (double)FRAME_ITERS;
    }
#else
    t_fixed_cache = -1.0;
#endif
#else
    t_fixed = -1.0;
    t_fixed_cache = -1.0;
#endif

    double mul_l = bench_mul_legacy(MUL_ITERS) * 1e6;
    double mul_n = bench_mul_new(MUL_ITERS) * 1e6;

    printf("| variant | ms/frame | vs legacy | fragments |\n");
    printf("|---|---:|---:|---:|\n");
    printf("| legacy 1.x | %.3f | - | %ld |\n", t_legacy, f_legacy);
    printf("| new float | %.3f | %+.1f%% | %ld |\n", t_float,
           (t_float / t_legacy - 1.0) * 100.0, f_new);
    if (t_float_cache > 0) {
        printf("| new float + cache | %.3f | %+.1f%% | %ld |\n", t_float_cache,
               (t_float_cache / t_legacy - 1.0) * 100.0, f_new);
    }
    if (t_fixed > 0) {
        printf("| new fixed | %.3f | %+.1f%% | %ld |\n", t_fixed,
               (t_fixed / t_legacy - 1.0) * 100.0, f_new);
    }
    if (t_fixed_cache > 0) {
        printf("| new fixed + cache | %.3f | %+.1f%% | %ld |\n", t_fixed_cache,
               (t_fixed_cache / t_legacy - 1.0) * 100.0, f_new);
    }

    printf("\nmat4 multiply: legacy %.1f ns, new float %.1f ns, new fixed %.1f ns\n",
           mul_l, mul_n, mul_n);

    printf("\nfootprint: color %u B + depth %u B = %u B; vertex cache %u B for %d verts\n",
           (unsigned)(FB_W * FB_H * sizeof(embd3d_pixel_t)),
           (unsigned)(FB_W * FB_H * sizeof(embd3d_depth_t)),
           (unsigned)(FB_W * FB_H * (sizeof(embd3d_pixel_t) + sizeof(embd3d_depth_t))),
           (unsigned)(EMBD3D_VERTEX_CACHE_STRIDE * SPH_VERT), SPH_VERT);
    return 0;
}
