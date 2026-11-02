#ifndef EMBD3D_RASTER_H
#define EMBD3D_RASTER_H

#include "embd3d_framebuffer.h"


#ifdef __cplusplus
extern "C" {
#endif

/* A vertex that has already been projected to screen space.
 * x/y are fixed point: EMBD3D_SUBPIXEL units per pixel.
 * depth is the 1/w key: bigger == nearer. */
typedef struct {
    int32_t x;
    int32_t y;
    embd3d_depth_t depth;
    embd3d_color_t color;
} embd3d_screen_vertex_t;

typedef enum {
    EMBD3D_CULL_NONE = 0,
    /* drops triangles that are clockwise in NDC (the usual "back facing"
     * winding for a right handed, y-up, looking-down--z pipeline) */
    EMBD3D_CULL_BACK = 1,
    EMBD3D_CULL_FRONT = 2
} embd3d_cull_t;

/* Fills the triangle with a flat color, using integer edge functions with
 * incremental stepping and a linearly interpolated depth plane. */
void embd3d_raster_triangle(embd3d_framebuffer_t *fb,
                          const embd3d_screen_vertex_t *a,
                          const embd3d_screen_vertex_t *b,
                          const embd3d_screen_vertex_t *c,
                          embd3d_cull_t cull);

void embd3d_raster_triangle_wire(embd3d_framebuffer_t *fb,
                               const embd3d_screen_vertex_t *a,
                               const embd3d_screen_vertex_t *b,
                               const embd3d_screen_vertex_t *c);

/* Depth interpolated line, works for every slope (horizontal, vertical and
 * diagonal).  All coordinates are fixed point as above. */
void embd3d_raster_line(embd3d_framebuffer_t *fb,
                      int32_t x0, int32_t y0, embd3d_depth_t z0,
                      int32_t x1, int32_t y1, embd3d_depth_t z1,
                      embd3d_color_t color);

#ifdef __cplusplus
}
#endif


#endif /* EMBD3D_RASTER_H */
