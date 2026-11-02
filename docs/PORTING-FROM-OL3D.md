# Migrating from ol3d 1.x

Header names, include layout and several argument orders changed. This is the
complete map.

## Files

| ol3d 1.x | embd3d |
|---|---|
| ol3d_core.h | embd3d.h (umbrella) |
| ol3d_vector.h | embd3d_vec3.h |
| ol3d_matrix.h | embd3d_mat4.h |
| ol3d_render.h | embd3d_framebuffer.h + embd3d_raster.h + embd3d_mesh.h + embd3d_render.h |

Everything moved from the repository root into src/, so -Isrc (or the Arduino
library layout) replaces the old flat include path.

The 1.x sources are **not** part of this repository; the upstream project is the
reference for them.

## Types

| ol3d 1.x | embd3d |
|---|---|
| ol3d_Vector3_t (double) | embd3d_vec3_t (embd3d_real_t, default float) |
| ol3d_matrix_t (float[16]) | embd3d_mat4_t (embd3d_real_t[16]) |
| ol3d_obj_face (long v1,v2,v3) | embd3d_index_t triples inside embd3d_mesh_t |
| long[] face array + 0 sentinel | embd3d_index_t[] + triangle_count |

## Functions

| ol3d 1.x | embd3d | note |
|---|---|---|
| ol3d_vector_dot(a,b) | embd3d_vec3_dot(a,b) | now a **real dot product**; the old component-wise behaviour is embd3d_vec3_hadamard() |
| ol3d_vector_length | embd3d_vec3_length | |
| ol3d_vector_normalize | embd3d_vec3_normalize | one division instead of three |
| ol3d_vector_add / subtract | embd3d_vec3_add / embd3d_vec3_sub | |
| ol3d_vector_multiply / divide | embd3d_vec3_scale | |
| ol3d_vector_cross | embd3d_vec3_cross | |
| ol3d_matrix_copy(a, b) | embd3d_mat4_copy(dst, src) | **argument order reversed**: destination first |
| ol3d_matrix_multiply(a,b,c) | embd3d_mat4_mul(dst,a,b) | destination first |
| ol3d_matrix_setUnit | embd3d_mat4_identity | |
| ol3d_matrix_setTranslate | embd3d_mat4_translation | |
| ol3d_matrix_setScale | embd3d_mat4_scaling | |
| ol3d_matrix_setRotate(m, a, M_AXIS_Y) | embd3d_mat4_rotation_y(m, a) | or embd3d_mat4_rotation(m, axis_vec, deg) |
| ol3d_matrix_setPerspective(m, fov, aspect, n, f) | embd3d_mat4_perspective(m, fov_deg, aspect, n, f) | fov in **degrees**, and now actually correct |
| ol3d_matrix_transpose(m) | embd3d_mat4_transpose(m) | fixed |
| ol3d_matrix_translate(m,x,y,z) | embd3d_mat4_mul_inplace(m, translation) | post multiply, same effect |
| ol3d_matrix_scale / ol3d_matrix_rotate | embd3d_mat4_mul_inplace with a built matrix | |
| ol3d_matrix_multi_v3(p, m) | embd3d_mat4_transform_point(p, m) | in place |
| ol3d_clean_buffer(buf) | embd3d_fb_clear(&fb, color) | also clears the depth buffer |
| ol3d_draw_Element(buf,f,v,n,len,mvp) | embd3d_draw_mesh(&fb,&mesh,mvp,model,&ctx) | f becomes uint16 indices without the sentinel; n and len are gone |
| ol3d_draw_Triangle(buf,a,b,c,color) | embd3d_raster_triangle(&fb,&a,&b,&c,cull) | takes embd3d_screen_vertex_t (already projected, fixed point) |
| ol3d_draw_Line(buf,color,start,end) | embd3d_raster_line(&fb,x0,y0,z0,x1,y1,z1,color) | vertical lines work now |
| ol3d_LineMode global | ctx.flags OR= EMBD3D_FLAG_WIREFRAME | |
| SCREEN_SIZE, PIXEL_SIZE macros | fb.width, fb.height | runtime size, no recompile |

## Behaviour changes you will notice

1. **The image is no longer upside down.** 1.x mapped NDC y straight to screen y;
   embd3d flips it, so a MATRIX_TRANSLATE(0, -80, 0) that used to centre a model now
   moves it down. Remove such compensating offsets.
2. **setPerspective() takes degrees and is correct.** Models look smaller: 1.x
   effectively multiplied the focal length by about 3.7 for fov = 60.
3. **Rotation matrices are the transpose of 1.x's.** 1.x stored the column-vector
   matrix in row-major order, which rotates the other way. If your scene spins
   backwards, negate the angle.
4. **Colours are RGB565 by default.** The old z*10 "shading" is gone; the pipeline
   does real flat shading from the face normal and ctx.light_dir. For the old
   rainbow look, compute the colour per face yourself and use embd3d_raster_triangle().
5. **Depth is a 1/w key, bigger = nearer.** Anything that wrote the depth buffer
   directly must follow that convention.

## Converting an "OBJ as C header"

1.x used 1-based long triples terminated by a zero. embd3d wants 0-based uint16_t
triples and an explicit count. Decrement every index, delete the trailing 0, and
put the triangle count into the mesh:

    sed -E 's/([0-9]+), *([0-9]+), *([0-9]+),/\1-1, \2-1, \3-1,/g'

Keep the vertex array as float and drop the duplicated vertex block the old
examples shipped: flat shading needs no per-face vertices, indices are enough.

## Bugs fixed on the way

* ol3d_matrix_transpose() copied an uninitialised temporary over its input.
* ol3d_matrix_setRotate(m, 0, axis) left the matrix untouched (uninitialised
  temp inside ol3d_matrix_rotate).
* ol3d_matrix_setPerspective() used tan where it needed 1/tan and never converted
  degrees to radians.
* ol3d_vector_dot() returned the Hadamard product.
* ol3d_draw_Line() never drew a vertical line (dx == 0 divide, empty loop).
* the bounding box was built with unsigned casts of possibly negative coordinates.
* the depth compare was inverted and 8-bit truncated, so occlusion was effectively
  random.
* no math.h include: it only built because compilers tolerated implicit
  declarations (GCC 14 rejects it).
* the documented mesh_f[] example had no 0 terminator, so ol3d_draw_Element()
  read past the end of the array.
## Additions specific to embedded-3d: tiers, integer pipeline, vertex cache

There is no 1.x equivalent for these; they are new surface area.

| new API | purpose |
|---|---|
| embd3d_render_ctx_init_tier(ctx, tier, n, f) | preset for MINIMAL / BALANCED / QUALITY |
| embd3d_tier_name(tier) / embd3d_tier_precision(tier) | introspect a preset |
| embd3d_draw_mesh_fx(fb, mesh_fx, mvp_fx, ctx) | Q16.16 pipeline, no float in transform/clip/raster |
| embd3d_draw_mesh_cached(...) | float pipeline + shared vertex cache |
| embd3d_draw_mesh_fx_cached(...) | integer pipeline + shared vertex cache |
| embd3d_mesh_fx_t | mesh with Q16.16 positions |
| embd3d_mat4_fx_t, embd3d_mat4_to_fx, embd3d_mat4_fx_mul | fixed point matrices |
| embd3d_mesh_positions_to_fx(dst, src, count) | one-time mesh conversion |
| embd3d_vertex_cache_t, embd3d_vertex_cache_init | caller owned vertex cache, 28 B/vertex |
| EMBD3D_FLAG_FULL_CLIP / EMBD3D_FLAG_UNLIT | new render flags |

Two things to know when porting from an earlier draft of this project:

1. The render context fields changed: depth_scale / depth_bias became depth_k
   (float) and depth_k_i (integer). Depth is now d = znear * MAX / w instead of
   a near/far linear remap; the ordering and the 0 = far convention are the
   same, so anything that only calls the API is unaffected.
2. The integer pipeline always draws the mesh colour. If you port a lit scene to
   MINIMAL, expect the shading to disappear; that is the documented trade-off,
   not a bug.
