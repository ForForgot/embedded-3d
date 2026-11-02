#ifndef EMBD3D_RENDER_H
#define EMBD3D_RENDER_H

#include "embd3d_fixed.h"
#include "embd3d_framebuffer.h"
#include "embd3d_mat4.h"
#include "embd3d_mesh.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ *
 * Tiers
 *
 * A tier is a named preset of render flags, not a separate code path:
 * pick the tier that matches the SoC and then call the matching draw
 * interface (embd3d_draw_mesh for float, embd3d_draw_mesh_fx for integer).
 *
 *   MINIMAL  : unlit + culled, integer pipeline, 8 bit depth.  AVR, M0.
 *   BALANCED : lit + culled, either pipeline, 16 bit depth.  M3, M4, ESP32.
 *   QUALITY  : lit + culled + full frustum clipping + vertex cache.  M4F/M7.
 * ------------------------------------------------------------------ */
typedef enum {
    EMBD3D_TIER_MINIMAL = 0,
    EMBD3D_TIER_BALANCED = 1,
    EMBD3D_TIER_QUALITY = 2
} embd3d_tier_t;

typedef enum {
    EMBD3D_PRECISION_FLOAT = 0,
    EMBD3D_PRECISION_FIXED = 1
} embd3d_precision_t;

/* Per-frame render state. */
typedef struct {
    embd3d_real_t znear;
    embd3d_real_t zfar;
    embd3d_real_t depth_k;     /* znear * EMBD3D_DEPTH_MAX (float pipeline) */
    int32_t depth_k_i;       /* the same value as a plain integer (fixed pipeline) */
    embd3d_real_t ambient;     /* 0..1 */
    embd3d_vec3_t light_dir;   /* normalised */
    uint8_t flags;
} embd3d_render_ctx_t;

#define EMBD3D_FLAG_CULL_BACK 0x01u   /* discard clockwise (in NDC) triangles */
#define EMBD3D_FLAG_WIREFRAME 0x02u   /* draw edges instead of filling */
#define EMBD3D_FLAG_FULL_CLIP 0x04u   /* clip all 6 planes, not just near */
#define EMBD3D_FLAG_UNLIT     0x08u   /* skip per-face lighting entirely */
/* Depth cue for unlit geometry: shades each face by its mean depth (nearer is
 * brighter).  Integer only, a handful of ops per face, and it restores the
 * shape readability that a flat fill loses. */
#define EMBD3D_FLAG_DEPTH_TINT 0x10u

void embd3d_render_ctx_init(embd3d_render_ctx_t *ctx, embd3d_real_t znear, embd3d_real_t zfar);
void embd3d_render_ctx_init_tier(embd3d_render_ctx_t *ctx, embd3d_tier_t tier,
                               embd3d_real_t znear, embd3d_real_t zfar);
const char *embd3d_tier_name(embd3d_tier_t tier);
/* Which draw interface the tier is meant to be used with on this build. */
embd3d_precision_t embd3d_tier_precision(embd3d_tier_t tier);

/* ------------------------------------------------------------------ *
 * Shared vertex cache
 *
 * Optional.  Without it every vertex shared by N triangles is transformed N
 * times; with it, once per draw call.  The buffer is caller owned:
 *
 *     static uint8_t cache_mem[EMBD3D_VERTEX_CACHE_STRIDE * MAX_VERTS];
 *     embd3d_vertex_cache_t cache;
 *     embd3d_vertex_cache_init(&cache, cache_mem, MAX_VERTS);
 *
 * The cache is only valid inside one draw call; the entry layout depends on
 * the pipeline (float or fixed) that filled it.
 * ------------------------------------------------------------------ */
#define EMBD3D_VERTEX_CACHE_STRIDE 28u

typedef struct {
    void *data;
    uint16_t capacity;
    uint16_t stamp;
} embd3d_vertex_cache_t;

void embd3d_vertex_cache_init(embd3d_vertex_cache_t *cache, void *data, uint16_t capacity);

/* Draws an indexed triangle list.
 *
 * mvp   : model * view * projection, row vector convention (v' = v * mvp)
 * model : optional.  It is only used to rotate the face normal into world
 *         space so the light does not follow the object; pass NULL to light
 *         the mesh in its own space and skip 9 multiplies per face.
 *         Declared as an array on purpose: pass `model`, not `&model`.
 */
void embd3d_draw_mesh(embd3d_framebuffer_t *fb, const embd3d_mesh_t *mesh,
                    const embd3d_mat4_t mvp, const embd3d_mat4_t model,
                    const embd3d_render_ctx_t *ctx);

/* Same, reusing transformed vertices across the triangles of this mesh.
 * Falls back to the uncached path if the cache is NULL or too small. */
void embd3d_draw_mesh_cached(embd3d_framebuffer_t *fb, const embd3d_mesh_t *mesh,
                           const embd3d_mat4_t mvp, const embd3d_mat4_t model,
                           const embd3d_render_ctx_t *ctx,
                           embd3d_vertex_cache_t *cache);

#if EMBD3D_ENABLE_FIXED
/* Integer pipeline: no libm, no soft float in the transform, no float in the
 * clipping or the rasterizer.  Lighting still uses float once per face unless
 * EMBD3D_FLAG_UNLIT is set. */
void embd3d_draw_mesh_fx(embd3d_framebuffer_t *fb, const embd3d_mesh_fx_t *mesh,
                       const embd3d_mat4_fx_t mvp, const embd3d_render_ctx_t *ctx);

void embd3d_draw_mesh_fx_cached(embd3d_framebuffer_t *fb, const embd3d_mesh_fx_t *mesh,
                              const embd3d_mat4_fx_t mvp, const embd3d_render_ctx_t *ctx,
                              embd3d_vertex_cache_t *cache);
#endif

#ifdef __cplusplus
}
#endif

#endif /* EMBD3D_RENDER_H */
