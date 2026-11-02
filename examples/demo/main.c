/* embd3d demo - renders a mesh to a PPM image (headless) so the library can be
 * checked on a desktop without any display driver.
 *
 *   build: cmake -B build && cmake --build build
 *   run  : ./build/embd3d_demo out.ppm [options]
 *
 *   --sphere          draw the UV sphere instead of the cube
 *   --frames N        write N frames as out.ppm.00.ppm ...
 *   --fx              use the Q16.16 pipeline (embd3d_draw_mesh_fx)
 *   --unlit           flat mesh colour, no per face lighting
 *   --cache           reuse transformed vertices (embd3d_draw_mesh_cached)
 *   --tier NAME       minimal | balanced | quality
 *
 * On an MCU you only need the ~15 lines between embd3d_fb_clear() and
 * embd3d_draw_mesh(); argument parsing and the PPM writer are host only.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "embd3d.h"

#define FB_W 128
#define FB_H 128

static embd3d_pixel_t g_color[FB_W * FB_H];
static embd3d_depth_t g_depth[FB_W * FB_H];

/* ---------------------------------------------------------------- cube -- */
static const embd3d_real_t cube_pos[] = {
    -1, -1, -1,   1, -1, -1,   1,  1, -1,  -1,  1, -1,
    -1, -1,  1,   1, -1,  1,   1,  1,  1,  -1,  1,  1,
};
/* counter clockwise seen from outside => survives back face culling */
static const embd3d_index_t cube_idx[] = {
    4, 5, 6,  4, 6, 7,      /* +z */
    1, 0, 3,  1, 3, 2,      /* -z */
    5, 1, 2,  5, 2, 6,      /* +x */
    0, 4, 7,  0, 7, 3,      /* -x */
    7, 6, 2,  7, 2, 3,      /* +y */
    0, 1, 5,  0, 5, 4,      /* -y */
};

/* -------------------------------------------------------------- sphere -- */
/* Procedural UV sphere, generated at start up (no malloc: fixed scratch). */
#define SPH_SEG 16
#define SPH_RING 10
#define SPH_VERT ((SPH_SEG + 1) * (SPH_RING + 1))
#define SPH_TRI (SPH_SEG * SPH_RING * 2)

static embd3d_real_t sph_pos[SPH_VERT * 3];
static embd3d_index_t sph_idx[SPH_TRI * 3];

static void make_sphere(void) {
    const embd3d_real_t r = (embd3d_real_t)1.2;
    int v = 0;
    for (int i = 0; i <= SPH_RING; i++) {
        embd3d_real_t phi = (embd3d_real_t)EMBD3D_PI * (embd3d_real_t)i / (embd3d_real_t)SPH_RING;
        for (int j = 0; j <= SPH_SEG; j++) {
            embd3d_real_t th = (embd3d_real_t)2 * (embd3d_real_t)EMBD3D_PI *
                             (embd3d_real_t)j / (embd3d_real_t)SPH_SEG;
            sph_pos[v * 3 + 0] = r * (embd3d_real_t)sin((double)phi) * (embd3d_real_t)cos((double)th);
            sph_pos[v * 3 + 1] = r * (embd3d_real_t)cos((double)phi);
            sph_pos[v * 3 + 2] = r * (embd3d_real_t)sin((double)phi) * (embd3d_real_t)sin((double)th);
            v++;
        }
    }
    int t = 0;
    for (int i = 0; i < SPH_RING; i++) {
        for (int j = 0; j < SPH_SEG; j++) {
            embd3d_index_t a = (embd3d_index_t)(i * (SPH_SEG + 1) + j);
            embd3d_index_t b = (embd3d_index_t)(a + SPH_SEG + 1);
            sph_idx[t * 3 + 0] = a;
            sph_idx[t * 3 + 1] = b;
            sph_idx[t * 3 + 2] = (embd3d_index_t)(a + 1);
            t++;
            sph_idx[t * 3 + 0] = (embd3d_index_t)(a + 1);
            sph_idx[t * 3 + 1] = b;
            sph_idx[t * 3 + 2] = (embd3d_index_t)(b + 1);
            t++;
        }
    }
}

/* --------------------------------------------------------------- write -- */
static uint8_t g_rgb[FB_W * FB_H * 3];

static int write_ppm(const char *path, const embd3d_framebuffer_t *fb) {
    FILE *f = fopen(path, "wb");
    if (f == 0) {
        return 0;
    }
    fprintf(f, "P6\n%u %u\n255\n", (unsigned)fb->width, (unsigned)fb->height);
    embd3d_fb_export_rgb888(fb, g_rgb);
    fwrite(g_rgb, 1, (size_t)fb->width * (size_t)fb->height * 3u, f);
    fclose(f);
    return 1;
}

int main(int argc, char **argv) {
    const char *path = "embd3d_demo.ppm";
    int sphere = 0, frames = 1, use_fx = 0, use_cache = 0;
    embd3d_tier_t tier = EMBD3D_TIER_BALANCED;
    uint8_t extra_flags = 0;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--sphere") == 0) {
            sphere = 1;
        } else if (strcmp(argv[i], "--fx") == 0) {
            use_fx = 1;
        } else if (strcmp(argv[i], "--unlit") == 0) {
            extra_flags |= EMBD3D_FLAG_UNLIT;
        } else if (strcmp(argv[i], "--cache") == 0) {
            use_cache = 1;
        } else if (strcmp(argv[i], "--frames") == 0 && i + 1 < argc) {
            frames = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--tier") == 0 && i + 1 < argc) {
            const char *t = argv[++i];
            tier = (strcmp(t, "minimal") == 0) ? EMBD3D_TIER_MINIMAL
                 : (strcmp(t, "quality") == 0) ? EMBD3D_TIER_QUALITY
                                               : EMBD3D_TIER_BALANCED;
        } else {
            path = argv[i];
        }
    }
    if (frames < 1) {
        frames = 1;
    }
#if !EMBD3D_ENABLE_FLOAT
    use_fx = 1;                    /* integer-only build: the float path is absent */
#endif

    embd3d_mesh_t mesh;
    mesh.positions = sphere ? sph_pos : cube_pos;
    mesh.indices = sphere ? sph_idx : cube_idx;
    mesh.vertex_count = (uint16_t)(sphere ? SPH_VERT : 8);
    mesh.triangle_count = (uint16_t)(sphere ? SPH_TRI : 12);
    mesh.color = EMBD3D_RGB(90, 170, 255);
    if (sphere) {
        make_sphere();
    }

    embd3d_framebuffer_t fb;
    embd3d_fb_init(&fb, g_color, g_depth, FB_W, FB_H);

    /* The Q16.16 pipeline needs its mesh positions and matrix in fixed point;
     * converting the mesh once is a load-time operation, the matrix is
     * converted per frame. */
#if EMBD3D_ENABLE_FIXED
    embd3d_mesh_fx_t mesh_fx;
    embd3d_mat4_fx_t mvp_fx;
    static embd3d_fx_t pos_fx[SPH_VERT * 3];
    if (use_fx) {
        embd3d_mesh_positions_to_fx(pos_fx, mesh.positions, mesh.vertex_count);
        mesh_fx.positions = pos_fx;
        mesh_fx.indices = mesh.indices;
        mesh_fx.vertex_count = mesh.vertex_count;
        mesh_fx.triangle_count = mesh.triangle_count;
        mesh_fx.color = mesh.color;
    }
#else
    if (use_fx) {
        fprintf(stderr, "--fx needs a build with EMBD3D_ENABLE_FIXED=1\n");
        return 1;
    }
#endif

    embd3d_render_ctx_t ctx;
    embd3d_render_ctx_init_tier(&ctx, tier, (embd3d_real_t)0.5, (embd3d_real_t)30);
    ctx.flags |= extra_flags;

    embd3d_mat4_t proj, view, vp;
    embd3d_mat4_perspective(proj, (embd3d_real_t)45, (embd3d_real_t)FB_W / (embd3d_real_t)FB_H,
                          (embd3d_real_t)0.5, (embd3d_real_t)30);
    const embd3d_vec3_t eye = { 0, 0, 6 };
    const embd3d_vec3_t at = { 0, 0, 0 };
    const embd3d_vec3_t up = { 0, 1, 0 };
    embd3d_mat4_look_at(view, &eye, &at, &up);
    embd3d_mat4_mul(vp, view, proj);

    for (int f = 0; f < frames; f++) {
        embd3d_mat4_t rx, ry, model, mvp;
        embd3d_mat4_rotation_y(ry, (embd3d_real_t)35 + (embd3d_real_t)f * (embd3d_real_t)12);
        embd3d_mat4_rotation_x(rx, (embd3d_real_t)22);
        embd3d_mat4_mul(model, ry, rx);
        embd3d_mat4_mul(mvp, model, vp);

        embd3d_fb_clear(&fb, EMBD3D_RGB(18, 20, 28));
        static uint8_t cache_mem[EMBD3D_VERTEX_CACHE_STRIDE * SPH_VERT];
        static embd3d_vertex_cache_t cache;
        static int cache_ready = 0;
        if (use_cache && !cache_ready) {
            embd3d_vertex_cache_init(&cache, cache_mem, (uint16_t)SPH_VERT);
            cache_ready = 1;
        }
        int drawn = 0;
#if EMBD3D_ENABLE_FIXED
        if (use_fx) {
            embd3d_mat4_to_fx(mvp_fx, mvp);
            if (use_cache) {
                embd3d_draw_mesh_fx_cached(&fb, &mesh_fx, mvp_fx, &ctx, &cache);
            } else {
                embd3d_draw_mesh_fx(&fb, &mesh_fx, mvp_fx, &ctx);
            }
            drawn = 1;
        }
#endif
#if EMBD3D_ENABLE_FLOAT
        if (!drawn) {
            if (use_cache) {
                embd3d_draw_mesh_cached(&fb, &mesh, mvp, model, &ctx, &cache);
            } else {
                embd3d_draw_mesh(&fb, &mesh, mvp, model, &ctx);
            }
        }
#endif
        (void)drawn;

        if (frames == 1) {
            if (!write_ppm(path, &fb)) {
                fprintf(stderr, "cannot write %s\n", path);
                return 1;
            }
        } else {
            char name[256];
            snprintf(name, sizeof(name), "%s.%02d.ppm", path, f);
            if (!write_ppm(name, &fb)) {
                fprintf(stderr, "cannot write %s\n", name);
                return 1;
            }
        }
        printf("frame %d: %u triangles\n", f, (unsigned)mesh.triangle_count);
    }
    return 0;
}
