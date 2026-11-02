#include "embd3d_raster.h"

#define S_BITS  EMBD3D_SUBPIXEL_BITS
#define D_FRAC  EMBD3D_DEPTH_FRAC_BITS
#define D_ONE   (1 << D_FRAC)

/* Floor division by a power of two that is correct for negative operands and
 * does not rely on implementation-defined arithmetic shifts. */
static inline int32_t floor_div_pow2(int32_t v, uint32_t s) {
    if (v >= 0) {
        return v >> s;
    }
    return -((-v + (int32_t)((1u << s) - 1u)) >> s);
}

static inline void embd3d_plot(embd3d_framebuffer_t *fb, uint16_t x, uint16_t y,
                             embd3d_depth_t d, embd3d_color_t c) {
    uint32_t idx = (uint32_t)y * (uint32_t)fb->width + (uint32_t)x;
    if (fb->depth != 0) {
        /* 1/w key: strictly greater means strictly nearer, so an equal-depth
         * overlap (shared edges) is rejected instead of being drawn twice. */
        if (d <= fb->depth[idx]) {
            return;
        }
        fb->depth[idx] = d;
    }
    fb->color[idx] = c;
}

void embd3d_raster_triangle(embd3d_framebuffer_t *fb,
                          const embd3d_screen_vertex_t *a,
                          const embd3d_screen_vertex_t *b,
                          const embd3d_screen_vertex_t *c,
                          embd3d_cull_t cull) {
    int32_t ax = a->x, ay = a->y;
    int32_t bx = b->x, by = b->y;
    int32_t cx = c->x, cy = c->y;
    int32_t da = (int32_t)a->depth;
    int32_t db = (int32_t)b->depth;
    int32_t dc = (int32_t)c->depth;
    embd3d_color_t color = a->color;

    /* Twice the signed area in subpixel units. */
    int32_t area = (bx - ax) * (cy - ay) - (by - ay) * (cx - ax);
    if (area == 0) {
        return;                       /* degenerate: zero pixels */
    }
    if ((cull == EMBD3D_CULL_BACK && area > 0) ||
        (cull == EMBD3D_CULL_FRONT && area < 0)) {
        return;
    }
    if (area < 0) {                   /* normalise the winding to area > 0 */
        int32_t t;
        t = bx; bx = cx; cx = t;
        t = by; by = cy; cy = t;
        t = db; db = dc; dc = t;
        area = -area;
    }

    /* Edge functions, written as A*x + B*y + C so the inner loop only adds.
     * The coefficients live in subpixel units, so one *pixel* step adds
     * A << EMBD3D_SUBPIXEL_BITS. */
    const int32_t sx = (int32_t)EMBD3D_SUBPIXEL;
    const int32_t a0 = -(by - ay), b0 = (bx - ax);
    const int32_t a1 = -(cy - by), b1 = (cx - bx);
    const int32_t a2 = -(ay - cy), b2 = (ax - cx);
    const int32_t c0 = (by - ay) * ax - (bx - ax) * ay;
    const int32_t c1 = (cy - by) * bx - (cx - bx) * by;
    const int32_t c2 = (ay - cy) * cx - (ax - cx) * cy;
    const int32_t step_x0 = a0 * sx, step_y0 = b0 * sx;
    const int32_t step_x1 = a1 * sx, step_y1 = b1 * sx;
    const int32_t step_x2 = a2 * sx, step_y2 = b2 * sx;

    /* Clamp the bounding box to the screen *before* any unsigned conversion,
     * which is where the old implementation wrapped around on off-screen
     * geometry. */
    int32_t minx = ax < bx ? (ax < cx ? ax : cx) : (bx < cx ? bx : cx);
    int32_t maxx = ax > bx ? (ax > cx ? ax : cx) : (bx > cx ? bx : cx);
    int32_t miny = ay < by ? (ay < cy ? ay : cy) : (by < cy ? by : cy);
    int32_t maxy = ay > by ? (ay > cy ? ay : cy) : (by > cy ? by : cy);

    int32_t xmin = floor_div_pow2(minx, S_BITS);
    int32_t xmax = floor_div_pow2(maxx, S_BITS);
    int32_t ymin = floor_div_pow2(miny, S_BITS);
    int32_t ymax = floor_div_pow2(maxy, S_BITS);

    if (xmin < 0) xmin = 0;
    if (ymin < 0) ymin = 0;
    if (xmax > (int32_t)fb->width - 1) xmax = (int32_t)fb->width - 1;
    if (ymax > (int32_t)fb->height - 1) ymax = (int32_t)fb->height - 1;
    if (xmin > xmax || ymin > ymax) {
        return;
    }

    /* Depth plane: d(x,y) = (ad*x + bd*y + cd) / area.  1/w is linear in
     * screen space, so a linear interpolation of the key is exact. */
    int64_t ad = (int64_t)(by - cy) * da + (int64_t)(cy - ay) * db +
                 (int64_t)(ay - by) * dc;
    int64_t bd = (int64_t)(cx - bx) * da + (int64_t)(ax - cx) * db +
                 (int64_t)(bx - ax) * dc;
    int64_t cd = ((int64_t)bx * cy - (int64_t)cx * by) * da +
                 ((int64_t)cx * ay - (int64_t)ax * cy) * db +
                 ((int64_t)ax * by - (int64_t)bx * ay) * dc;

    /* Per pixel step, i.e. per EMBD3D_SUBPIXEL subpixel units. */
    int64_t gx = (ad << (D_FRAC + S_BITS)) / area;
    int64_t gy = (bd << (D_FRAC + S_BITS)) / area;
    if (gx > (int64_t)0x0FFFFFFF) gx = (int64_t)0x0FFFFFFF;
    if (gx < -(int64_t)0x0FFFFFFF) gx = -(int64_t)0x0FFFFFFF;
    if (gy > (int64_t)0x0FFFFFFF) gy = (int64_t)0x0FFFFFFF;
    if (gy < -(int64_t)0x0FFFFFFF) gy = -(int64_t)0x0FFFFFFF;

    int32_t px = xmin << S_BITS;
    int32_t py = ymin << S_BITS;

    int32_t e0 = a0 * px + b0 * py + c0;
    int32_t e1 = a1 * px + b1 * py + c1;
    int32_t e2 = a2 * px + b2 * py + c2;

    int64_t base64 = ((ad * px + bd * py + cd) << D_FRAC) / area;
    int32_t drow = (int32_t)base64;
    int32_t dzdx = (int32_t)gx;
    int32_t dzdy = (int32_t)gy;

    for (int32_t y = ymin; y <= ymax; y++) {
        int32_t r0 = e0, r1 = e1, r2 = e2;
        int32_t dacc = drow;

        for (int32_t x = xmin; x <= xmax; x++) {
            if (r0 >= 0 && r1 >= 0 && r2 >= 0) {
                int32_t d = dacc >> D_FRAC;
                if (d < 0) {
                    d = 0;
                } else if (d > (int32_t)EMBD3D_DEPTH_MAX) {
                    d = (int32_t)EMBD3D_DEPTH_MAX;
                }
                embd3d_plot(fb, (uint16_t)x, (uint16_t)y, (embd3d_depth_t)d, color);
            }
            r0 += step_x0; r1 += step_x1; r2 += step_x2;
            dacc += dzdx;
        }

        e0 += step_y0; e1 += step_y1; e2 += step_y2;
        drow += dzdy;
    }
}

void embd3d_raster_line(embd3d_framebuffer_t *fb,
                      int32_t x0, int32_t y0, embd3d_depth_t z0,
                      int32_t x1, int32_t y1, embd3d_depth_t z1,
                      embd3d_color_t color) {
    int32_t dx = x1 - x0;
    int32_t dy = y1 - y0;
    int32_t adx = dx < 0 ? -dx : dx;
    int32_t ady = dy < 0 ? -dy : dy;
    int32_t steps = adx > ady ? adx : ady;

    if (steps == 0) {
        int32_t px = floor_div_pow2(x0, S_BITS);
        int32_t py = floor_div_pow2(y0, S_BITS);
        if (px >= 0 && py >= 0 && px < (int32_t)fb->width && py < (int32_t)fb->height) {
            embd3d_plot(fb, (uint16_t)px, (uint16_t)py, z0, color);
        }
        return;
    }

    /* DDA over the major axis, 8 extra fractional bits.  Handles every slope,
     * unlike the old x-only stepping version which dropped vertical edges. */
    int32_t xs = (dx << 8) / steps;
    int32_t ys = (dy << 8) / steps;
    int32_t zs = (((int32_t)z1 - (int32_t)z0) << 8) / steps;

    int32_t x = x0 << 8;
    int32_t y = y0 << 8;
    int32_t z = (int32_t)z0 << 8;
    int32_t w = (int32_t)fb->width;
    int32_t h = (int32_t)fb->height;

    for (int32_t i = 0; i <= steps; i++) {
        int32_t px = floor_div_pow2(floor_div_pow2(x, 8), S_BITS);
        int32_t py = floor_div_pow2(floor_div_pow2(y, 8), S_BITS);
        if (px >= 0 && py >= 0 && px < w && py < h) {
            int32_t d = z >> 8;
            if (d < 0) {
                d = 0;
            } else if (d > (int32_t)EMBD3D_DEPTH_MAX) {
                d = (int32_t)EMBD3D_DEPTH_MAX;
            }
            embd3d_plot(fb, (uint16_t)px, (uint16_t)py, (embd3d_depth_t)d, color);
        }
        x += xs; y += ys; z += zs;
    }
}

void embd3d_raster_triangle_wire(embd3d_framebuffer_t *fb,
                               const embd3d_screen_vertex_t *a,
                               const embd3d_screen_vertex_t *b,
                               const embd3d_screen_vertex_t *c) {
    embd3d_raster_line(fb, a->x, a->y, a->depth, b->x, b->y, b->depth, a->color);
    embd3d_raster_line(fb, b->x, b->y, b->depth, c->x, c->y, c->depth, a->color);
    embd3d_raster_line(fb, c->x, c->y, c->depth, a->x, a->y, a->depth, a->color);
}
