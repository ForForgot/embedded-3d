#ifndef EMBD3D_MESH_H
#define EMBD3D_MESH_H

#include "embd3d_config.h"
#include "embd3d_vec3.h"

/* Indexed triangle list.
 *
 * Compared to the original "obj as C header" arrays this replaces the
 * 0-terminated long[] face list with an explicit count (no sentinel, no
 * out-of-bounds read) and 16 bit indices (a 3-arm MCU cannot address more
 * vertices than that anyway). */
typedef struct {
    const embd3d_real_t *positions;     /* 3 floats per vertex */
    const embd3d_index_t *indices;      /* 3 indices per triangle, 0 based */
    uint16_t vertex_count;
    uint16_t triangle_count;
    embd3d_color_t color;               /* base albedo */
} embd3d_mesh_t;

/* Same geometry with Q16.16 positions, for the integer pipeline.  Convert a
 * mesh once at load time with embd3d_mesh_positions_to_fx(). */
typedef struct {
    const embd3d_fx_t *positions;       /* 3 values per vertex */
    const embd3d_index_t *indices;
    uint16_t vertex_count;
    uint16_t triangle_count;
    embd3d_color_t color;
} embd3d_mesh_fx_t;

#endif /* EMBD3D_MESH_H */
