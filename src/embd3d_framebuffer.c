#include "embd3d_framebuffer.h"

#include <string.h>

void embd3d_fb_init(embd3d_framebuffer_t *fb, embd3d_pixel_t *color,
                  embd3d_depth_t *depth, uint16_t width, uint16_t height) {
    fb->color = color;
    fb->depth = depth;
    fb->width = width;
    fb->height = height;
}

void embd3d_fb_clear(embd3d_framebuffer_t *fb, embd3d_color_t color) {
    uint32_t count = (uint32_t)fb->width * (uint32_t)fb->height;

#if EMBD3D_COLOR_FORMAT == 0
    if (color == (embd3d_color_t)0) {
        memset(fb->color, 0, (size_t)count * sizeof(embd3d_pixel_t));
    } else
#endif
    {
        for (uint32_t i = 0; i < count; i++) {
            fb->color[i] = color;
        }
    }

    if (fb->depth != 0) {
        memset(fb->depth, 0, (size_t)count * sizeof(embd3d_depth_t));
    }
}

void embd3d_fb_export_rgb888(const embd3d_framebuffer_t *fb, uint8_t *dst) {
    uint32_t count = (uint32_t)fb->width * (uint32_t)fb->height;
    for (uint32_t i = 0; i < count; i++) {
        embd3d_color_t c = fb->color[i];
#if EMBD3D_COLOR_FORMAT == 0
        uint8_t r5 = (uint8_t)((c >> 11) & 0x1Fu);
        uint8_t g6 = (uint8_t)((c >> 5) & 0x3Fu);
        uint8_t b5 = (uint8_t)(c & 0x1Fu);
        dst[i * 3u + 0u] = (uint8_t)((r5 << 3) | (r5 >> 2));
        dst[i * 3u + 1u] = (uint8_t)((g6 << 2) | (g6 >> 4));
        dst[i * 3u + 2u] = (uint8_t)((b5 << 3) | (b5 >> 2));
#else
        dst[i * 3u + 0u] = (uint8_t)c;
        dst[i * 3u + 1u] = (uint8_t)c;
        dst[i * 3u + 2u] = (uint8_t)c;
#endif
    }
}
