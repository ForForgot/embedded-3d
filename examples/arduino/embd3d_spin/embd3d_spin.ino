/* embd3d on an Arduino + 128x128 RGB OLED.
 *
 * The library renders into an RGB565 buffer; if your driver still expects the
 * original 3-bytes-per-pixel layout, expand the frame with
 * embd3d_fb_export_rgb888() before pushing it over SPI.
 */
#include <Arduino.h>
#include <SPI.h>

#include <embd3d.h>

#define W 128
#define H 128

static embd3d_pixel_t g_color[W * H];
static embd3d_depth_t g_depth[W * H];   /* 32 KiB; EMBD3D_DEPTH_BITS=8 halves it */
static unsigned char g_rgb[W * H * 3];

static embd3d_framebuffer_t g_fb;
static embd3d_render_ctx_t g_ctx;
static embd3d_mesh_t g_mesh;

/* A quad; replace with your own mesh (see examples/demo for a cube/sphere). */
static const embd3d_real_t quad_pos[] = {
    -1, -1, 0,
     1, -1, 0,
     1,  1, 0,
    -1,  1, 0,
};
static const embd3d_index_t quad_idx[] = { 0, 1, 2, 0, 2, 3 };

void OLED_init(void);
void OLED_fill(unsigned char *buf);

void setup(void) {
    SPI.begin();
    SPI.setClockDivider(SPI_CLOCK_DIV2);
    OLED_init();

    embd3d_fb_init(&g_fb, g_color, g_depth, W, H);

    /* A tier is just a preset of render flags.  On an AVR you would instead
     * build with EMBD3D_ENABLE_FLOAT=0 and call embd3d_draw_mesh_fx() with a
     * Q16.16 mesh - see docs/DESIGN.md section 14. */
    embd3d_render_ctx_init_tier(&g_ctx, EMBD3D_TIER_BALANCED, 0.5f, 30.0f);

    g_mesh.positions = quad_pos;
    g_mesh.indices = quad_idx;
    g_mesh.vertex_count = 4;
    g_mesh.triangle_count = 2;
    g_mesh.color = EMBD3D_RGB(90, 170, 255);
}

void loop(void) {
    static float yaw = 0.0f;

    embd3d_mat4_t proj, view, vp, model, mvp;
    embd3d_mat4_perspective(proj, 45.0f, 1.0f, 0.5f, 30.0f);
    const embd3d_vec3_t eye = { 0, 0, 6 }, at = { 0, 0, 0 }, up = { 0, 1, 0 };
    embd3d_mat4_look_at(view, &eye, &at, &up);
    embd3d_mat4_mul(vp, view, proj);

    embd3d_mat4_rotation_y(model, yaw);
    embd3d_mat4_mul(mvp, model, vp);
    yaw += 8.0f;

    embd3d_fb_clear(&g_fb, EMBD3D_RGB(0, 0, 0));
    embd3d_draw_mesh(&g_fb, &g_mesh, mvp, &model, &g_ctx);

    embd3d_fb_export_rgb888(&g_fb, g_rgb);
    OLED_fill(g_rgb);
}
