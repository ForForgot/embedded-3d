/*
 * embd3d - tiny portable 3D rasterizer for micro controllers.
 * Compile-time configuration.  Every macro can be overridden from the
 * build system (-DEMBD3D_...) or from a project-wide config header.
 */
#ifndef EMBD3D_CONFIG_H
#define EMBD3D_CONFIG_H

#include <stdint.h>

/* ------------------------------------------------------------------ *
 * Scalar type
 *
 * The original library used "double" everywhere.  On every MCU without a
 * double-precision FPU that is a software emulation (Cortex-M4: ~50x slower
 * than single precision).  "float" is the right default; "double" stays
 * available for hosts that want it.
 * ------------------------------------------------------------------ */
#ifndef EMBD3D_REAL
#define EMBD3D_REAL float
#endif
typedef EMBD3D_REAL embd3d_real_t;

/* ------------------------------------------------------------------ *
 * Color format
 *
 *   0 : RGB565   - 2 bytes per pixel (default, 32 KiB for 128x128)
 *   1 : L8       - 1 byte  per pixel, 8 bit luminance
 *
 * RGB888 (3 bytes) is intentionally not offered: it costs 50% more RAM for
 * a precision that no 6-bit panel can show.  Use embd3d_fb_export_rgb888()
 * to feed a driver that really needs 3 bytes per pixel.
 * ------------------------------------------------------------------ */
#ifndef EMBD3D_COLOR_FORMAT
#define EMBD3D_COLOR_FORMAT 0
#endif

#if EMBD3D_COLOR_FORMAT == 0
typedef uint16_t embd3d_pixel_t;
/* r,g,b are 0..255 */
#define EMBD3D_RGB(r, g, b) \
    ((embd3d_pixel_t)((((uint16_t)((r) & 0xF8u)) << 8) | \
                    (((uint16_t)((g) & 0xFCu)) << 3) | \
                    ((uint16_t)((b) >> 3))))
#elif EMBD3D_COLOR_FORMAT == 1
typedef uint8_t embd3d_pixel_t;
#define EMBD3D_RGB(r, g, b) \
    ((embd3d_pixel_t)(((uint32_t)(r) * 77u + (uint32_t)(g) * 151u + \
                     (uint32_t)(b) * 28u) >> 8))
#else
#error "EMBD3D_COLOR_FORMAT must be 0 (RGB565) or 1 (L8)"
#endif

typedef embd3d_pixel_t embd3d_color_t;

/* ------------------------------------------------------------------ *
 * Depth buffer
 *
 *   16 : 2 bytes per pixel, 65536 levels (default)
 *    8 : 1 byte  per pixel, 256 levels, only for very tight RAM budgets
 *
 * Depth is stored as a 1/w key: bigger == closer, 0 == cleared/infinite.
 * ------------------------------------------------------------------ */
#ifndef EMBD3D_DEPTH_BITS
#define EMBD3D_DEPTH_BITS 16
#endif

#if EMBD3D_DEPTH_BITS == 16
typedef uint16_t embd3d_depth_t;
#define EMBD3D_DEPTH_MAX 65535u
#elif EMBD3D_DEPTH_BITS == 8
typedef uint8_t embd3d_depth_t;
#define EMBD3D_DEPTH_MAX 255u
#else
#error "EMBD3D_DEPTH_BITS must be 8 or 16"
#endif

/* ------------------------------------------------------------------ *
 * Mesh index type
 * ------------------------------------------------------------------ */
#ifndef EMBD3D_INDEX_TYPE
#define EMBD3D_INDEX_TYPE uint16_t
#endif
typedef EMBD3D_INDEX_TYPE embd3d_index_t;

/* ------------------------------------------------------------------ *
 * Rasterizer subpixel precision (bits).  4 gives 1/16 pixel snapping,
 * which is enough to hide jitter on a 128 px panel.
 *
 * Budget: edge functions are int32, and a projected vertex is clamped to
 * +-4 in NDC, so a coordinate can reach 4.5 * width * 2^bits.  The product
 * of two such coordinates must stay below 2^31:
 *
 *     4.5 * width * 2^bits  <  32768      (about)
 *
 * 4 bits is safe up to ~570 px wide, 5 bits up to ~285 px, 3 bits up to
 * ~1140 px.  Going over silently corrupts the edge functions and the
 * triangle stops being drawn, which is why the default is conservative.
 * ------------------------------------------------------------------ */
#ifndef EMBD3D_SUBPIXEL_BITS
#define EMBD3D_SUBPIXEL_BITS 4
#endif
#define EMBD3D_SUBPIXEL (1 << EMBD3D_SUBPIXEL_BITS)

/* Fractional bits used by the depth interpolator (Q(16-4).4 -> 4 integer
 * bits + 12 fractional bits).  Keep <= 12 so the accumulator stays int32. */
#define EMBD3D_DEPTH_FRAC_BITS 12

/* ================================================================== *
 * Feature tiers
 *
 * embd3d is meant to run from an 8 bit AVR up to a Cortex-M7.  The features
 * below are what differs between those targets, and every one of them is
 * compiled out when you set it to 0, so a minimal build carries no dead
 * code:
 *
 *   AVR / 8051, no FPU, 2-8 KiB RAM   -> FLOAT 0, FIXED 1, CACHE 0, FULLCLIP 0
 *   Cortex-M0/M3, no FPU, 32-64 KiB  -> FLOAT 0, FIXED 1, CACHE 0, FULLCLIP 1
 *   Cortex-M4F/M7, ESP32, RP2040     -> FLOAT 1, FIXED 0, CACHE 1, FULLCLIP 1
 *
 * Both precision paths can be enabled at once (the default) when you want to
 * pick per scene at run time; each draw call selects its interface.
 * ================================================================== */

/* Floating point vertex pipeline: embd3d_draw_mesh(). */
#ifndef EMBD3D_ENABLE_FLOAT
#define EMBD3D_ENABLE_FLOAT 1
#endif

/* Q16.16 fixed point vertex pipeline: embd3d_draw_mesh_fx().  No libm, no
 * soft float, at the price of a coarser range and precision. */
#ifndef EMBD3D_ENABLE_FIXED
#define EMBD3D_ENABLE_FIXED 1
#endif

/* Full 6 plane frustum clipping (EMBD3D_FLAG_FULL_CLIP).  Adds ~200 bytes of
 * stack and a few hundred bytes of code; without it only the near plane is
 * clipped and off-screen triangles are rejected by the bounding box. */
#ifndef EMBD3D_ENABLE_FULL_CLIP
#define EMBD3D_ENABLE_FULL_CLIP 1
#endif

/* Shared vertex cache (embd3d_draw_mesh_cached).  Costs 28 bytes per cached
 * vertex in a caller owned buffer. */
#ifndef EMBD3D_ENABLE_VERTEX_CACHE
#define EMBD3D_ENABLE_VERTEX_CACHE 1
#endif

/* Per face flat shading.  Set to 0 for a float-free, libm-free build: every
 * triangle then uses the mesh colour and EMBD3D_FLAG_UNLIT is implied. */
#ifndef EMBD3D_ENABLE_LIGHTING
#define EMBD3D_ENABLE_LIGHTING 1
#endif

/* Maximum vertices a clipped triangle can produce: 3 + 6 planes. */
#ifndef EMBD3D_MAX_CLIP_VERTS
#define EMBD3D_MAX_CLIP_VERTS 9
#endif

/* Runtime validation of mesh data: with this at 1 every triangle checks that
 * its three vertex indices are inside vertex_count before touching the
 * position array, and skips the triangle if they are not.  Costs one compare
 * per index in the draw loop, so it is off by default - turn it on while you
 * are building a mesh, and leave it off in the shipped firmware. */
#ifndef EMBD3D_VALIDATE_MESH
#define EMBD3D_VALIDATE_MESH 0
#endif

/* Q16.16 fixed point scalar used by the integer pipeline. */
typedef int32_t embd3d_fx_t;
#define EMBD3D_FX_ONE 65536
#define EMBD3D_FX_HALF 32768

#if (EMBD3D_ENABLE_FLOAT == 0) && (EMBD3D_ENABLE_FIXED == 0)
#error "at least one of EMBD3D_ENABLE_FLOAT / EMBD3D_ENABLE_FIXED must be 1"
#endif

#endif /* EMBD3D_CONFIG_H */
