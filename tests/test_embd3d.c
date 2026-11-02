/* Unit tests for embd3d.  No framework: a failing CHECK prints and the process
 * exits non zero, which is all CTest needs. */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "embd3d.h"

#define CHECK(cond)                                                          \
    do {                                                                     \
        if (!(cond)) {                                                       \
            printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);         \
            g_failures++;                                                    \
        }                                                                    \
    } while (0)

#define CHECK_NEAR(a, b, eps)                                                \
    do {                                                                     \
        double va_ = (double)(a), vb_ = (double)(b);                         \
        if (fabs(va_ - vb_) > (eps)) {                                       \
            printf("  FAIL %s:%d  %s=%.6f vs %s=%.6f\n", __FILE__, __LINE__, \
                   #a, va_, #b, vb_);                                        \
            g_failures++;                                                    \
        }                                                                    \
    } while (0)

static int g_failures = 0;

#define TEST_WHITE EMBD3D_RGB(255, 255, 255)

/* Depth keys are relative so the suite also runs with EMBD3D_DEPTH_BITS=8.
 * Bigger means nearer, 0 is the cleared value. */
#define Z_FAR  ((int)(EMBD3D_DEPTH_MAX / 8))
#define Z_MID  ((int)(EMBD3D_DEPTH_MAX / 2))
#define Z_NEAR ((int)(EMBD3D_DEPTH_MAX - EMBD3D_DEPTH_MAX / 8))

#define FBW 128
#define FBH 128

/* The suite renders into a FBW px wide buffer and the rasterizer's edge
 * functions are int32: 4.5 * FBW * 2^bits must stay below 2^31, which caps
 * the subpixel precision at 5 bits here.  Without this guard an oversized
 * value fails as a mysterious "nothing was drawn". */
#if EMBD3D_SUBPIXEL_BITS > 5
#error "the 128 px test viewport needs EMBD3D_SUBPIXEL_BITS <= 5"
#endif
static embd3d_pixel_t g_color[FBW * FBH];
static embd3d_depth_t g_depth[FBW * FBH];
static embd3d_framebuffer_t g_fb;

static long lit_pixels(void) {
    long n = 0;
    for (long i = 0; i < (long)(FBW * FBH); i++) {
        if (g_color[i] != 0) {
            n++;
        }
    }
    return n;
}

static void reset_fb(void) {
    embd3d_fb_clear(&g_fb, 0);
}

/* --------------------------------------------------------------- vec3 --- */
static void test_vec3(void) {
    embd3d_vec3_t a = { 1, 2, 3 };
    embd3d_vec3_t b = { 4, 5, 6 };

    CHECK_NEAR(embd3d_vec3_dot(&a, &b), 32.0, 1e-6);        /* the real dot */
    embd3d_vec3_t h = embd3d_vec3_hadamard(&a, &b);
    CHECK_NEAR(h.x, 4.0, 1e-6);
    CHECK_NEAR(h.y, 10.0, 1e-6);
    CHECK_NEAR(h.z, 18.0, 1e-6);

    embd3d_vec3_t c = embd3d_vec3_cross(&a, &b);
    CHECK_NEAR(c.x, -3.0, 1e-6);
    CHECK_NEAR(c.y, 6.0, 1e-6);
    CHECK_NEAR(c.z, -3.0, 1e-6);

    embd3d_vec3_t v = { 3, 4, 0 };
    CHECK_NEAR(embd3d_vec3_length(&v), 5.0, 1e-6);
    embd3d_vec3_t u = embd3d_vec3_normalize(&v);
    CHECK_NEAR(embd3d_vec3_length(&u), 1.0, 1e-6);

    embd3d_vec3_t zero = { 0, 0, 0 };
    embd3d_vec3_t z = embd3d_vec3_normalize(&zero);   /* must not divide by zero */
    CHECK_NEAR(z.x, 0.0, 1e-6);
    CHECK_NEAR(z.y, 0.0, 1e-6);
}

/* --------------------------------------------------------------- mat4 --- */
static void test_mat4(void) {
    embd3d_mat4_t m, n, p;

    /* translation lives in the last ROW (row vector convention) */
    embd3d_mat4_translation(m, 10, 20, 30);
    CHECK_NEAR(m[12], 10.0, 1e-6);
    CHECK_NEAR(m[13], 20.0, 1e-6);
    CHECK_NEAR(m[14], 30.0, 1e-6);
    CHECK_NEAR(m[15], 1.0, 1e-6);

    embd3d_vec3_t v = { 1, 0, 0 };
    embd3d_mat4_transform_point(&v, m);
    CHECK_NEAR(v.x, 11.0, 1e-6);
    CHECK_NEAR(v.y, 20.0, 1e-6);
    CHECK_NEAR(v.z, 30.0, 1e-6);

    /* multiply against a hand computed product */
    embd3d_mat4_t a = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16 };
    embd3d_mat4_t b;
    embd3d_mat4_translation(b, 10, 20, 30);
    embd3d_mat4_mul(p, a, b);
    CHECK_NEAR(p[0], 41.0, 1e-4);
    CHECK_NEAR(p[1], 82.0, 1e-4);
    CHECK_NEAR(p[2], 123.0, 1e-4);
    CHECK_NEAR(p[3], 4.0, 1e-4);
    CHECK_NEAR(p[12], 173.0, 1e-4);
    CHECK_NEAR(p[15], 16.0, 1e-4);

    /* transpose: (m^T)^T == m, and a real transpose is not the identity */
    embd3d_mat4_copy(n, a);
    embd3d_mat4_transpose(n);
    CHECK_NEAR(n[1], 5.0, 1e-6);
    CHECK_NEAR(n[4], 2.0, 1e-6);
    embd3d_mat4_transpose(n);
    for (int i = 0; i < 16; i++) {
        CHECK_NEAR(n[i], a[i], 1e-6);
    }

    /* perspective: f = 1/tan(fov/2), row vector layout, w = -z_eye */
    embd3d_mat4_perspective(m, 90, 1, 1, 100);
    CHECK_NEAR(m[0], 1.0, 1e-5);          /* 1/tan(45 deg) = 1 */
    CHECK_NEAR(m[5], 1.0, 1e-5);
    CHECK_NEAR(m[11], -1.0, 1e-6);
    CHECK_NEAR(m[10], (100.0 + 1.0) / (1.0 - 100.0), 1e-4);
    CHECK_NEAR(m[14], 2.0 * 100.0 * 1.0 / (1.0 - 100.0), 1e-4);

    embd3d_mat4_perspective(m, 60, 1, 1, 1000);
    CHECK_NEAR(m[5], 1.0 / tan(60.0 * EMBD3D_PI / 180.0 / 2.0), 1e-4);
    CHECK(m[5] > 1.0f);                   /* the old code produced -6.4 here */

    /* a point on the near plane maps to z_ndc = -1, w = distance */
    {
        embd3d_real_t z = -1, w = 1;
        embd3d_real_t zc = z * m[10] + w * m[14];
        embd3d_real_t wc = z * m[11];
        CHECK_NEAR(zc / wc, -1.0, 1e-3);
    }

    /* rotation: +90 deg around Y takes +x to -z (right handed) */
    embd3d_mat4_rotation_y(m, 90);
    v = (embd3d_vec3_t){ 1, 0, 0 };
    embd3d_mat4_transform_point(&v, m);
    CHECK_NEAR(v.x, 0.0, 1e-5);
    CHECK_NEAR(v.z, -1.0, 1e-5);

    /* rotation around an arbitrary axis == the axis specific version */
    embd3d_vec3_t yaxis = { 0, 1, 0 };
    embd3d_mat4_rotation(n, &yaxis, 37);
    embd3d_mat4_rotation_y(p, 37);
    for (int i = 0; i < 16; i++) {
        CHECK_NEAR(n[i], p[i], 1e-5);
    }

    /* look_at: a camera 6 units in front of the origin looks down -z */
    embd3d_vec3_t eye = { 0, 0, 6 }, at = { 0, 0, 0 }, up = { 0, 1, 0 };
    embd3d_mat4_t view;
    embd3d_mat4_look_at(view, &eye, &at, &up);
    v = (embd3d_vec3_t){ 0, 0, 0 };
    embd3d_mat4_transform_point(&v, view);
    CHECK_NEAR(v.x, 0.0, 1e-5);
    CHECK_NEAR(v.y, 0.0, 1e-5);
    CHECK_NEAR(v.z, -6.0, 1e-5);
}

/* ------------------------------------------------------------- raster --- */
static embd3d_screen_vertex_t sv(int x, int y, int z, embd3d_color_t c) {
    embd3d_screen_vertex_t v;
    v.x = (int32_t)x * EMBD3D_SUBPIXEL;
    v.y = (int32_t)y * EMBD3D_SUBPIXEL;
    v.depth = (embd3d_depth_t)z;
    v.color = c;
    return v;
}

static void test_raster_area(void) {
    reset_fb();
    embd3d_screen_vertex_t a = sv(10, 10, Z_MID, TEST_WHITE);
    embd3d_screen_vertex_t b = sv(110, 10, Z_MID, TEST_WHITE);
    embd3d_screen_vertex_t c = sv(10, 110, Z_MID, TEST_WHITE);
    embd3d_raster_triangle(&g_fb, &a, &b, &c, EMBD3D_CULL_NONE);

    long n = lit_pixels();
    /* 100x100 right triangle = 5000 px, plus the shared edges */
    CHECK(n > 4900 && n < 5300);

    /* the hypotenuse must not be filled: (100,100) is outside */
    CHECK(g_color[100 * FBW + 100] == 0);
    CHECK(g_color[20 * FBW + 20] != 0);

    /* Two triangles sharing an edge must leave no crack along it: a strict
     * point-in-triangle test leaves a one pixel hole on every shared edge. */
    reset_fb();
    embd3d_screen_vertex_t q0 = sv(10, 10, Z_MID, TEST_WHITE);
    embd3d_screen_vertex_t q1 = sv(110, 10, Z_MID, TEST_WHITE);
    embd3d_screen_vertex_t q2 = sv(110, 110, Z_MID, TEST_WHITE);
    embd3d_screen_vertex_t q3 = sv(10, 110, Z_MID, TEST_WHITE);
    embd3d_raster_triangle(&g_fb, &q0, &q1, &q2, EMBD3D_CULL_NONE);
    embd3d_raster_triangle(&g_fb, &q0, &q2, &q3, EMBD3D_CULL_NONE);
    CHECK(lit_pixels() == 101 * 101);      /* the whole quad, no holes */

    /* degenerate triangle draws nothing */
    reset_fb();
    embd3d_screen_vertex_t d = sv(50, 50, Z_MID, TEST_WHITE);
    embd3d_raster_triangle(&g_fb, &d, &d, &d, EMBD3D_CULL_NONE);
    CHECK(lit_pixels() == 0);
}

static void test_raster_depth(void) {
    embd3d_color_t far_c = EMBD3D_RGB(255, 0, 0);
    embd3d_color_t near_c = EMBD3D_RGB(0, 0, 255);

    /* far first, then near: near must win */
    reset_fb();
    embd3d_screen_vertex_t a1 = sv(0, 0, Z_FAR, far_c), b1 = sv(100, 0, Z_FAR, far_c), c1 = sv(0, Z_FAR, 100, far_c);
    embd3d_raster_triangle(&g_fb, &a1, &b1, &c1, EMBD3D_CULL_NONE);
    embd3d_screen_vertex_t a2 = sv(0, 0, Z_NEAR, near_c), b2 = sv(100, 0, Z_NEAR, near_c), c2 = sv(0, Z_FAR, Z_NEAR, near_c);
    embd3d_raster_triangle(&g_fb, &a2, &b2, &c2, EMBD3D_CULL_NONE);
    CHECK(g_color[20 * FBW + 20] != far_c);

    /* near first, then far: near must survive */
    reset_fb();
    embd3d_raster_triangle(&g_fb, &a2, &b2, &c2, EMBD3D_CULL_NONE);
    embd3d_raster_triangle(&g_fb, &a1, &b1, &c1, EMBD3D_CULL_NONE);
    CHECK(g_color[20 * FBW + 20] != far_c);

    /* depth must be interpolated inside the triangle, not constant.
     * The p2 corner carries the largest key, so the key must grow as a
     * fragment moves towards it. */
    reset_fb();
    embd3d_screen_vertex_t p0 = sv(0, 0, Z_FAR, TEST_WHITE);
    embd3d_screen_vertex_t p1 = sv(100, 0, Z_FAR, TEST_WHITE);
    embd3d_screen_vertex_t p2 = sv(0, Z_FAR, Z_NEAR, TEST_WHITE);
    embd3d_raster_triangle(&g_fb, &p0, &p1, &p2, EMBD3D_CULL_NONE);
    embd3d_depth_t at_origin_corner = g_depth[2 * FBW + 2];
    embd3d_depth_t midpoint = g_depth[10 * FBW + 40];
    CHECK(midpoint > at_origin_corner);
    CHECK(at_origin_corner >= Z_FAR);    /* never below the smallest vertex key */
}

static void test_raster_cull(void) {
    /* clockwise in NDC (negative area in screen space) */
    embd3d_screen_vertex_t a = sv(10, 10, Z_MID, TEST_WHITE);
    embd3d_screen_vertex_t b = sv(110, 10, Z_MID, TEST_WHITE);
    embd3d_screen_vertex_t c = sv(10, 110, Z_MID, TEST_WHITE);

    reset_fb();
    embd3d_raster_triangle(&g_fb, &a, &b, &c, EMBD3D_CULL_BACK);
    long kept = lit_pixels();

    reset_fb();
    embd3d_raster_triangle(&g_fb, &a, &b, &c, EMBD3D_CULL_FRONT);
    long dropped = lit_pixels();

    CHECK((kept == 0) != (dropped == 0));   /* exactly one winding survives */
    CHECK(kept > 0 || dropped > 0);
}

static void test_raster_line(void) {
    /* vertical line: the old implementation drew nothing for dx == 0 */
    reset_fb();
    embd3d_raster_line(&g_fb, 10 * EMBD3D_SUBPIXEL, 10 * EMBD3D_SUBPIXEL, Z_NEAR,
                     10 * EMBD3D_SUBPIXEL, 60 * EMBD3D_SUBPIXEL, Z_NEAR, TEST_WHITE);
    CHECK(lit_pixels() >= 50);

    /* horizontal line */
    reset_fb();
    embd3d_raster_line(&g_fb, 10 * EMBD3D_SUBPIXEL, 30 * EMBD3D_SUBPIXEL, Z_NEAR,
                     60 * EMBD3D_SUBPIXEL, 30 * EMBD3D_SUBPIXEL, Z_NEAR, TEST_WHITE);
    CHECK(lit_pixels() >= 50);

    /* diagonal line stays inside its bounding box */
    reset_fb();
    embd3d_raster_line(&g_fb, 0, 0, Z_NEAR, 100 * EMBD3D_SUBPIXEL, 100 * EMBD3D_SUBPIXEL,
                     Z_NEAR, TEST_WHITE);
    CHECK(lit_pixels() >= 100);
    CHECK(lit_pixels() <= 101);
}

/* ------------------------------------------------------------ pipeline --- */
#if EMBD3D_ENABLE_FLOAT
static const embd3d_real_t cube_pos[] = {
    -1, -1, -1,  1, -1, -1,  1,  1, -1,  -1,  1, -1,
    -1, -1,  1,  1, -1,  1,  1,  1,  1,  -1,  1,  1,
};
static const embd3d_index_t cube_idx[] = {
    4, 5, 6,  4, 6, 7,  1, 0, 3,  1, 3, 2,  5, 1, 2,  5, 2, 6,
    0, 4, 7,  0, 7, 3,  7, 6, 2,  7, 2, 3,  0, 1, 5,  0, 5, 4,
};

static void test_pipeline(void) {
    embd3d_mesh_t mesh;
    mesh.positions = cube_pos;
    mesh.indices = cube_idx;
    mesh.vertex_count = 8;
    mesh.triangle_count = 12;
    mesh.color = EMBD3D_RGB(200, 200, 200);

    embd3d_render_ctx_t ctx;
    embd3d_render_ctx_init(&ctx, 0.5f, 30.0f);

    embd3d_mat4_t proj, view, vp, ry, model, mvp;
    embd3d_mat4_perspective(proj, 45, 1, 0.5f, 30);
    embd3d_vec3_t eye = { 0, 0, 6 }, at = { 0, 0, 0 }, up = { 0, 1, 0 };
    embd3d_mat4_look_at(view, &eye, &at, &up);
    embd3d_mat4_mul(vp, view, proj);
    embd3d_mat4_rotation_y(ry, 35);
    embd3d_mat4_copy(model, ry);
    embd3d_mat4_mul(mvp, model, vp);

    reset_fb();
    embd3d_draw_mesh(&g_fb, &mesh, mvp, model, &ctx);
    CHECK(lit_pixels() > 500);                 /* the cube covers a good area */
    CHECK(g_color[64 * FBW + 64] != 0);        /* centre is inside the cube */
    CHECK(g_color[2 * FBW + 2] == 0);          /* corner is background */

    /* back face culling must reduce (but not remove) the work */
    long all = lit_pixels();
    ctx.flags = EMBD3D_FLAG_CULL_BACK;
    reset_fb();
    embd3d_draw_mesh(&g_fb, &mesh, mvp, model, &ctx);
    long culled = lit_pixels();
    CHECK(culled > 0);
    CHECK(culled <= all);

    /* wireframe still produces pixels */
    ctx.flags = EMBD3D_FLAG_WIREFRAME;
    reset_fb();
    embd3d_draw_mesh(&g_fb, &mesh, mvp, model, &ctx);
    CHECK(lit_pixels() > 100);
    CHECK(lit_pixels() < all);                 /* and far fewer than a solid fill */
}
#endif /* EMBD3D_ENABLE_FLOAT */

#if EMBD3D_ENABLE_FLOAT
static void test_clipping(void) {
    /* One vertex behind the eye: without near plane clipping this produced a
     * division by a negative w and a garbage triangle across the screen. */
    static const embd3d_real_t tri_pos[] = {
        0, 0, 1,        /* behind the camera */
        -2, -2, -5,
        2, -2, -5,
    };
    static const embd3d_index_t tri_idx[] = { 0, 1, 2 };
    embd3d_mesh_t mesh;
    mesh.positions = tri_pos;
    mesh.indices = tri_idx;
    mesh.vertex_count = 3;
    mesh.triangle_count = 1;
    mesh.color = EMBD3D_RGB(255, 255, 255);

    embd3d_render_ctx_t ctx;
    embd3d_render_ctx_init(&ctx, 0.5f, 30.0f);
    ctx.flags = 0;                 /* this test is about clipping, not culling */

    embd3d_mat4_t proj, view, vp, mvp;
    embd3d_mat4_perspective(proj, 45, 1, 0.5f, 30);
    embd3d_vec3_t eye = { 0, 0, 0 }, at = { 0, 0, -1 }, up = { 0, 1, 0 };
    embd3d_mat4_look_at(view, &eye, &at, &up);
    embd3d_mat4_mul(vp, view, proj);
    embd3d_mat4_copy(mvp, vp);

    reset_fb();
    embd3d_draw_mesh(&g_fb, &mesh, mvp, 0, &ctx);
    long n = lit_pixels();
    CHECK(n > 0);                              /* the visible part is drawn */
    CHECK(n < FBW * FBH);                      /* and it is clipped, not smeared */

    /* a triangle entirely behind the camera must draw nothing */
    static const embd3d_real_t back_pos[] = { 0, 0, 1, 1, 1, 2, -1, 1, 2 };
    mesh.positions = back_pos;
    reset_fb();
    embd3d_draw_mesh(&g_fb, &mesh, mvp, 0, &ctx);
    CHECK(lit_pixels() == 0);
}
#endif /* EMBD3D_ENABLE_FLOAT */

static void test_framebuffer(void) {
    embd3d_fb_clear(&g_fb, EMBD3D_RGB(10, 20, 30));
    for (long i = 0; i < (long)(FBW * FBH); i++) {
        CHECK(g_color[i] == EMBD3D_RGB(10, 20, 30));
    }
    for (long i = 0; i < (long)(FBW * FBH); i++) {
        CHECK(g_depth[i] == 0);
    }

#if EMBD3D_COLOR_FORMAT == 0
    /* RGB565 packing */
    CHECK(EMBD3D_RGB(0, 0, 0) == 0x0000);
    CHECK(EMBD3D_RGB(255, 255, 255) == TEST_WHITE);
    CHECK(EMBD3D_RGB(255, 0, 0) == 0xF800);
    CHECK(EMBD3D_RGB(0, 255, 0) == 0x07E0);
    CHECK(EMBD3D_RGB(0, 0, 255) == 0x001F);
#else
    /* L8 luminance */
    CHECK(EMBD3D_RGB(0, 0, 0) == 0);
    CHECK(EMBD3D_RGB(255, 255, 255) == 255);
    CHECK(EMBD3D_RGB(255, 0, 0) > 0);
    CHECK(EMBD3D_RGB(0, 255, 0) > EMBD3D_RGB(0, 0, 255));
#endif

    /* shade 256 is a no-op, shade 0 is black */
    CHECK(embd3d_color_shade(EMBD3D_RGB(255, 128, 64), 256) == EMBD3D_RGB(255, 128, 64));
    CHECK(embd3d_color_shade(EMBD3D_RGB(255, 255, 255), 0) == 0);
    CHECK(embd3d_color_shade(EMBD3D_RGB(255, 255, 255), 128) != 0);
}


/* ------------------------------------------------------- tiered paths --- */
#if EMBD3D_ENABLE_FLOAT   /* every helper below is only used by the float tests */
static embd3d_pixel_t g_saved[FBW * FBH];

static void save_image(void) {
    memcpy(g_saved, g_color, sizeof(g_saved));
}

static long distinct_colors(void) {
    embd3d_color_t seen[128];
    long n = 0;
    for (long i = 0; i < (long)(FBW * FBH); i++) {
        embd3d_color_t c = g_color[i];
        long k = 0;
        for (; k < n; k++) {
            if (seen[k] == c) {
                break;
            }
        }
        if (k == n && n < 128) {
            seen[n++] = c;
        }
    }
    return n;
}

static long image_diff(void) {
    long n = 0;
    for (long i = 0; i < (long)(FBW * FBH); i++) {
        if (g_color[i] != g_saved[i]) {
            n++;
        }
    }
    return n;
}

static void setup_cube_scene(embd3d_mesh_t *mesh, embd3d_mat4_t mvp, embd3d_mat4_t model,
                             embd3d_render_ctx_t *ctx) {
    mesh->positions = cube_pos;
    mesh->indices = cube_idx;
    mesh->vertex_count = 8;
    mesh->triangle_count = 12;
    mesh->color = EMBD3D_RGB(200, 200, 200);

    embd3d_render_ctx_init(ctx, (embd3d_real_t)0.5, (embd3d_real_t)30);
    ctx->flags = EMBD3D_FLAG_CULL_BACK | EMBD3D_FLAG_UNLIT;

    embd3d_mat4_t proj, view, ry, rx;
    embd3d_mat4_perspective(proj, 45, 1, (embd3d_real_t)0.5, 30);
    embd3d_vec3_t eye = { 0, 0, 6 }, at = { 0, 0, 0 }, up = { 0, 1, 0 };
    embd3d_mat4_look_at(view, &eye, &at, &up);
    embd3d_mat4_mul(mvp, view, proj);
    embd3d_mat4_rotation_y(ry, 35);
    embd3d_mat4_rotation_x(rx, 22);
    embd3d_mat4_mul(model, ry, rx);
    embd3d_mat4_mul(mvp, model, mvp);
}

#if EMBD3D_ENABLE_FIXED
static embd3d_fx_t g_cube_pos_fx[8 * 3];

static void build_fx_cube(const embd3d_mesh_t *mesh, embd3d_mesh_fx_t *out,
                          const embd3d_mat4_t mvp, embd3d_mat4_fx_t mvp_fx) {
    embd3d_mesh_positions_to_fx(g_cube_pos_fx, mesh->positions, mesh->vertex_count);
    out->positions = g_cube_pos_fx;
    out->indices = mesh->indices;
    out->vertex_count = mesh->vertex_count;
    out->triangle_count = mesh->triangle_count;
    out->color = mesh->color;
    embd3d_mat4_to_fx(mvp_fx, mvp);
}
#endif

/* Drawing the same mesh twice must produce identical bytes: this catches
 * uninitialised state in the pipeline, in the depth buffer and in the cache. */
static void test_determinism(void) {
    embd3d_mesh_t mesh;
    embd3d_mat4_t mvp, model;
    embd3d_render_ctx_t ctx;
    setup_cube_scene(&mesh, mvp, model, &ctx);
    ctx.flags |= EMBD3D_FLAG_DEPTH_TINT;

    reset_fb();
    embd3d_draw_mesh(&g_fb, &mesh, mvp, model, &ctx);
    long n = lit_pixels();
    save_image();

    reset_fb();
    embd3d_draw_mesh(&g_fb, &mesh, mvp, model, &ctx);
    CHECK(lit_pixels() == n);
    CHECK(image_diff() == 0);
}

#endif /* EMBD3D_ENABLE_FLOAT */

static void test_fixed_vs_float(void) {
#if EMBD3D_ENABLE_FIXED && EMBD3D_ENABLE_FLOAT
    embd3d_mesh_t mesh;
    embd3d_mat4_t mvp, model;
    embd3d_render_ctx_t ctx;
    embd3d_mesh_fx_t mesh_fx;
    embd3d_mat4_fx_t mvp_fx;
    setup_cube_scene(&mesh, mvp, model, &ctx);
    build_fx_cube(&mesh, &mesh_fx, mvp, mvp_fx);

    reset_fb();
    embd3d_draw_mesh(&g_fb, &mesh, mvp, 0, &ctx);
    long n_float = lit_pixels();
    save_image();

    reset_fb();
    embd3d_draw_mesh_fx(&g_fb, &mesh_fx, mvp_fx, &ctx);
    long n_fixed = lit_pixels();

    CHECK(n_float > 500);
    CHECK(n_fixed > 500);
    /* The two pipelines use different arithmetic; allow a couple of units of
     * Q16.16 rounding at the silhouette. */
    long d = (n_fixed > n_float) ? (n_fixed - n_float) : (n_float - n_fixed);
    CHECK(d * 100 <= n_float * 4);
#else
    printf("  (skipped: needs both pipelines)\n");
#endif
}

static void test_vertex_cache(void) {
#if EMBD3D_ENABLE_VERTEX_CACHE && EMBD3D_ENABLE_FLOAT
    embd3d_mesh_t mesh;
    embd3d_mat4_t mvp, model;
    embd3d_render_ctx_t ctx;
    setup_cube_scene(&mesh, mvp, model, &ctx);

    reset_fb();
    embd3d_draw_mesh(&g_fb, &mesh, mvp, 0, &ctx);
    save_image();
    long n_plain = lit_pixels();

    static uint8_t cache_mem[EMBD3D_VERTEX_CACHE_STRIDE * 64];
    embd3d_vertex_cache_t cache;
    embd3d_vertex_cache_init(&cache, cache_mem, 64);

    reset_fb();
    embd3d_draw_mesh_cached(&g_fb, &mesh, mvp, 0, &ctx, &cache);
    CHECK(lit_pixels() == n_plain);
    CHECK(image_diff() == 0);          /* bit identical, not just similar */

    /* a cache that is too small must silently fall back */
    embd3d_vertex_cache_init(&cache, cache_mem, 4);
    reset_fb();
    embd3d_draw_mesh_cached(&g_fb, &mesh, mvp, 0, &ctx, &cache);
    CHECK(lit_pixels() == n_plain);
    CHECK(image_diff() == 0);

    /* the same, through the integer pipeline */
#if EMBD3D_ENABLE_FIXED
    embd3d_mesh_fx_t mesh_fx;
    embd3d_mat4_fx_t mvp_fx;
    build_fx_cube(&mesh, &mesh_fx, mvp, mvp_fx);
    reset_fb();
    embd3d_draw_mesh_fx(&g_fb, &mesh_fx, mvp_fx, &ctx);
    save_image();
    long n_fx = lit_pixels();
    embd3d_vertex_cache_init(&cache, cache_mem, 64);
    reset_fb();
    embd3d_draw_mesh_fx_cached(&g_fb, &mesh_fx, mvp_fx, &ctx, &cache);
    CHECK(lit_pixels() == n_fx);
    CHECK(image_diff() == 0);
#endif
#else
    printf("  (skipped: vertex cache disabled)\n");
#endif
}

static void test_full_clip(void) {
#if EMBD3D_ENABLE_FULL_CLIP && EMBD3D_ENABLE_FLOAT
    /* One big triangle that pokes out of the right and bottom planes. */
    static const embd3d_real_t tri[] = {
        0, 0, -3,
        20, 0, -3,
        0, 20, -3,
    };
    static const embd3d_index_t idx[] = { 0, 1, 2 };
    embd3d_mesh_t mesh = { tri, idx, 3, 1, EMBD3D_RGB(255, 255, 255) };

    embd3d_render_ctx_t ctx;
    embd3d_render_ctx_init(&ctx, (embd3d_real_t)0.5, (embd3d_real_t)30);
    ctx.flags = 0;                       /* no culling: winding is arbitrary here */

    embd3d_mat4_t proj, view, mvp;
    embd3d_mat4_perspective(proj, 45, 1, (embd3d_real_t)0.5, 30);
    embd3d_vec3_t eye = { 0, 0, 0 }, at = { 0, 0, -1 }, up = { 0, 1, 0 };
    embd3d_mat4_look_at(view, &eye, &at, &up);
    embd3d_mat4_mul(mvp, view, proj);

    reset_fb();
    embd3d_draw_mesh(&g_fb, &mesh, mvp, 0, &ctx);      /* near plane only */
    long n_near = lit_pixels();
    save_image();

    ctx.flags = EMBD3D_FLAG_FULL_CLIP;
    reset_fb();
    embd3d_draw_mesh(&g_fb, &mesh, mvp, 0, &ctx);
    long n_full = lit_pixels();

    CHECK(n_near > 1000);
    /* Both must cover the same visible region: the bounding box clamp already
     * stops the near-only path from smearing, full clipping only saves work. */
    long d = (n_full > n_near) ? (n_full - n_near) : (n_near - n_full);
    CHECK(d * 100 <= n_near * 3);

    /* and the integer clipper must agree with the float one */
#if EMBD3D_ENABLE_FIXED
    static embd3d_fx_t tri_fx[9];
    embd3d_mesh_positions_to_fx(tri_fx, tri, 3);
    embd3d_mat4_fx_t mvp_fx;
    embd3d_mat4_to_fx(mvp_fx, mvp);
    embd3d_mesh_fx_t mesh_fx = { tri_fx, idx, 3, 1, EMBD3D_RGB(255, 255, 255) };
    reset_fb();
    embd3d_draw_mesh_fx(&g_fb, &mesh_fx, mvp_fx, &ctx);
    long n_fx = lit_pixels();
    d = (n_fx > n_full) ? (n_fx - n_full) : (n_full - n_fx);
    CHECK(d * 100 <= n_full * 4);
#endif
#else
    printf("  (skipped: full clipping disabled)\n");
#endif
}

static void test_tiers(void) {
    embd3d_render_ctx_t ctx;

    embd3d_render_ctx_init_tier(&ctx, EMBD3D_TIER_MINIMAL, (embd3d_real_t)0.5, 30);
    CHECK((ctx.flags & EMBD3D_FLAG_UNLIT) != 0);
    CHECK((ctx.flags & EMBD3D_FLAG_CULL_BACK) != 0);
    CHECK((ctx.flags & EMBD3D_FLAG_FULL_CLIP) == 0);
    CHECK((ctx.flags & EMBD3D_FLAG_DEPTH_TINT) != 0);   /* keeps the shape readable */

    embd3d_render_ctx_init_tier(&ctx, EMBD3D_TIER_BALANCED, (embd3d_real_t)0.5, 30);
    CHECK((ctx.flags & EMBD3D_FLAG_UNLIT) == 0);
    CHECK((ctx.flags & EMBD3D_FLAG_FULL_CLIP) == 0);
    CHECK((ctx.flags & EMBD3D_FLAG_DEPTH_TINT) == 0);   /* real lighting instead */

    embd3d_render_ctx_init_tier(&ctx, EMBD3D_TIER_QUALITY, (embd3d_real_t)0.5, 30);
    CHECK((ctx.flags & EMBD3D_FLAG_FULL_CLIP) != 0);

#if EMBD3D_ENABLE_FIXED && EMBD3D_ENABLE_FLOAT
    CHECK(embd3d_tier_precision(EMBD3D_TIER_MINIMAL) == EMBD3D_PRECISION_FIXED);
    CHECK(embd3d_tier_precision(EMBD3D_TIER_QUALITY) == EMBD3D_PRECISION_FLOAT);
#elif EMBD3D_ENABLE_FIXED
    CHECK(embd3d_tier_precision(EMBD3D_TIER_QUALITY) == EMBD3D_PRECISION_FIXED);
#else
    CHECK(embd3d_tier_precision(EMBD3D_TIER_MINIMAL) == EMBD3D_PRECISION_FLOAT);
#endif

    CHECK(strcmp(embd3d_tier_name(EMBD3D_TIER_QUALITY), "quality") == 0);

#if EMBD3D_ENABLE_FLOAT
    /* The unlit depth cue must actually vary with depth, otherwise a flat fill
     * would read as a flat shape. */
    embd3d_mesh_t mesh;
    embd3d_mat4_t mvp, model;
    setup_cube_scene(&mesh, mvp, model, &ctx);

    ctx.flags = EMBD3D_FLAG_CULL_BACK | EMBD3D_FLAG_UNLIT;
    reset_fb();
    embd3d_draw_mesh(&g_fb, &mesh, mvp, model, &ctx);
    long flat_colors = distinct_colors();

    ctx.flags |= EMBD3D_FLAG_DEPTH_TINT;
    reset_fb();
    embd3d_draw_mesh(&g_fb, &mesh, mvp, model, &ctx);
    long tinted_colors = distinct_colors();

    CHECK(flat_colors == 2);          /* background + one fill colour */
    CHECK(tinted_colors > flat_colors);
#endif
}

int main(void) {
    embd3d_fb_init(&g_fb, g_color, g_depth, FBW, FBH);

    printf("vec3...\n");     test_vec3();
    printf("mat4...\n");     test_mat4();
    printf("framebuffer...\n"); test_framebuffer();
    printf("raster area...\n");  test_raster_area();
    printf("raster depth...\n"); test_raster_depth();
    printf("raster cull...\n");  test_raster_cull();
    printf("raster line...\n");  test_raster_line();
#if EMBD3D_ENABLE_FLOAT
    printf("determinism...\n");  test_determinism();
    printf("pipeline...\n");     test_pipeline();
    printf("clipping...\n");     test_clipping();
#endif
    printf("fixed vs float...\n"); test_fixed_vs_float();
    printf("vertex cache...\n");    test_vertex_cache();
    printf("full clipping...\n");   test_full_clip();
    printf("tiers...\n");           test_tiers();

    if (g_failures != 0) {
        printf("\n%d check(s) FAILED\n", g_failures);
        return 1;
    }
    printf("\nall checks passed\n");
    return 0;
}
