#ifndef EMBD3D_FRAMEBUFFER_H
#define EMBD3D_FRAMEBUFFER_H

#include "embd3d_config.h"


#ifdef __cplusplus
extern "C" {
#endif

/* Pixel storage is caller owned: no malloc anywhere in this library. */
typedef struct {
    embd3d_pixel_t *color;   /* width * height */
    embd3d_depth_t *depth;   /* width * height, or NULL to disable the z-test */
    uint16_t width;
    uint16_t height;
} embd3d_framebuffer_t;

void embd3d_fb_init(embd3d_framebuffer_t *fb, embd3d_pixel_t *color,
                  embd3d_depth_t *depth, uint16_t width, uint16_t height);

/* Clears color to the given value and depth to 0 (= infinitely far). */
void embd3d_fb_clear(embd3d_framebuffer_t *fb, embd3d_color_t color);

/* shade is Q8: 0 = black, 256 = unchanged. */
static inline embd3d_color_t embd3d_color_shade(embd3d_color_t c, uint16_t shade) {
#if EMBD3D_COLOR_FORMAT == 0
    uint32_t r = ((uint32_t)c >> 11) & 0x1Fu;
    uint32_t g = ((uint32_t)c >> 5) & 0x3Fu;
    uint32_t b = (uint32_t)c & 0x1Fu;
    uint32_t rr = (r * shade + 128u) >> 8;
    uint32_t gg = (g * shade + 128u) >> 8;
    uint32_t bb = (b * shade + 128u) >> 8;
    if (rr > 31u) rr = 31u;
    if (gg > 63u) gg = 63u;
    if (bb > 31u) bb = 31u;
    return (embd3d_color_t)((rr << 11) | (gg << 5) | bb);
#else
    uint32_t v = ((uint32_t)c * shade + 128u) >> 8;
    if (v > 255u) v = 255u;
    return (embd3d_color_t)v;
#endif
}

/* Expands the frame to packed RGB888, for display drivers whose wire
 * format is 3 bytes per pixel (the original library's buffer format). */

void embd3d_fb_export_rgb888(const embd3d_framebuffer_t *fb, uint8_t *dst);

#ifdef __cplusplus
}
#endif


#endif /* EMBD3D_FRAMEBUFFER_H */
