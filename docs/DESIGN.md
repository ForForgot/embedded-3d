# Design notes and the optimisation study

This document records *why* the implementation looks the way it does: the
problem, the chosen solution, the alternatives, and what it measured.

All numbers come from a one-off A/B run made during development (GCC 13, x86-64
host, 1280 triangle sphere, 128x128 target, identical flags for both sides).
Host timings are not MCU timings; they are used to compare *algorithms*, not
absolute cost. The harness is not part of this repository, so the figures are
historical rather than reproducible here.

---

## 0. Constraints that drive everything

1. **No dynamic memory.** Target boards have tens of KiB of RAM. Every buffer is
   caller owned, every temporary is either on the stack or in a static.
2. **No dependency beyond math.h and string.h.**
3. **The inner loop must avoid division.** A soft-float divide is 100+ cycles on
   a Cortex-M0; a 128x128 frame is 16384 pixels.
4. **Keep the row-vector matrix convention** of 1.x so existing matrix literals
   keep working, even though column-vector is more common.

---

## 1. Matrix multiply: 70 ns -> 20 ns (-71%)

**1.x** built the product element by element through ol3d_matrix_multi_chunk(),
which looked up two constant tables (transpose4x4[], rowOf[]) per element and
walked a b[transpose4x4[column++]] strided access. Correct, but 16 table lookups,
16 function calls and a data dependent access pattern per matrix.

**embedded-3d** uses a straight 4x4 loop with row pointers so the compiler keeps
ar[0..3] in registers and can unroll/vectorise:

    for (r) { const real_t *ar = a + r*4;
              for (col) dst[r*4+col] = ar[0]*b[col] + ar[1]*b[4+col]
                                     + ar[2]*b[8+col] + ar[3]*b[12+col]; }

The product is written to a local and copied out, so dst may alias a or b. The
1.x helper functions used a second temporary for exactly this, and made the
transpose() bug possible by getting the argument order wrong.

Alternatives considered: storing matrices column major (rejected: breaks every
existing literal and the MATRIX_TRANSLATE-style macros); transpose-on-load
(rejected: an extra pass, and the multiply is not the bottleneck anyway).

---

## 2. Triangle fill: point-in-triangle per pixel -> incremental edge functions

**1.x** evaluated inTriangle() for every pixel of the bounding box: six
multiplies, six adds and four comparisons per pixel, all in double, recomputing
the same cross products for every pixel of every row.

**embedded-3d** uses the standard edge function formulation. For each edge,
E(x,y) = A*x + B*y + C is evaluated once at the top-left corner of the clamped
bounding box and then **incremented** by A << SUBPIXEL per pixel step and
B << SUBPIXEL per row. Inside is E0 >= 0 && E1 >= 0 && E2 >= 0 after normalising
the winding.

Per pixel the inner loop is now: three integer adds for coverage, one add for
depth, one compare per edge, one store. No multiplies, no divides, no floating
point at all.

Design details worth knowing:

* **Subpixel precision** (EMBD3D_SUBPIXEL_BITS, default 4) snaps vertices to 1/16
  px. Without it small rotations pop and thin triangles flicker.
* **int32 is enough** because projection clamps NDC x/y to +-4 (guard band),
  which bounds every coordinate the rasterizer sees.
* **The AABB is clamped to the framebuffer before any unsigned conversion.**
  1.x cast a possibly negative float to unsigned int for the loop bounds, which
  wrapped and either skipped the triangle or iterated billions of times.

Alternatives considered: scanline / trapezoid decomposition (2 to 4 edges active
per row, fewer edge evaluations per pixel, a further ~20% on wide triangles but
substantially more code and state); half-space evaluation at 4x4 block
granularity then per-pixel refine (good for SIMD, pointless on an 8-bit MCU).

---

## 3. Depth: 8 bit constant-per-triangle -> 16 bit interpolated 1/w

**1.x**: unsigned char z taken from the triangle's max NDC z, truncated by
COORD() (which maps NDC to 0..128, i.e. *screen* coordinates), compared with
z >= zbuffer. Two consequences, both verified by experiment: the whole
[znear, zfar] range collapses onto two values (127/128), and the compare
direction lets the *farther* fragment win.

**embedded-3d** stores 1/w, which is **linear in screen space**, so it can be
interpolated exactly with a plane equation:

    d(x,y) = (ad*x + bd*y + cd) / area        computed once per triangle, int64
    per pixel: dacc += dzdx                   int32, Q4.12 fixed point

Consequences: correct ordering, 1/4096 of a depth unit resolution, one integer
add per fragment. 1/w is also the quantity a later perspective-correct attribute
interpolation needs, so the plumbing is already right.

Alternatives considered:

| scheme | cost | why not |
|---|---|---|
| painter's sort per frame | O(n log n) + an index buffer | no memory for it, and it cycles badly |
| float z-buffer | 4 B/px | 2x memory, no benefit at 128 px |
| 8 bit key (available as EMBD3D_DEPTH_BITS=8) | 1 B/px | visible banding on long corridors |

Direction convention: bigger key = nearer, 0 = cleared. The test is therefore
"if (d <= zbuf) reject", and an equal-depth overlap (shared triangle edge) is
rejected instead of drawn twice, which is why the missing top-left rule is
harmless here.

---

## 4. Colour: RGB888 -> RGB565, and shading in Q8

1.x wrote 3 bytes per pixel (48 KiB for 128x128) for 6-bit channels: it packed
z*10 scaled values into unsigned char, so the image never exceeded 25% brightness
while the header comment described an RGB565 layout.

embedded-3d writes RGB565 (32 KiB) and shades with a Q8 multiplier:

    r5' = (r5 * shade + 128) >> 8            shade in 0..256

three shifts/masks per pixel, no division, and it composes with the existing
5/6/5 channel widths. L8 (EMBD3D_COLOR_FORMAT=1) drops to 16 KiB for mono panels.

embd3d_fb_export_rgb888() converts a finished frame for drivers that still want the
old 3-byte wire format, so the RAM saving costs one pass over the frame instead
of 16 KiB of always-resident buffer.

---

## 5. Clipping: none -> near plane only

1.x had no clipping at all. A vertex behind the eye produced w <= 0, a division,
and a triangle smeared across the screen (or an inf that makes the AABB loop run
for millions of iterations).

embedded-3d clips the triangle in clip space against z + w >= 0 (Sutherland-Hodgman, one
plane, at most a 4-gon, then a triangle fan). One plane is enough for this
projection: any vertex behind the eye also fails that test, so w = 0 cannot reach
the divider. A guard is kept anyway (w < 1e-6 -> 1e-6).

The other four planes are intentionally *not* clipped: the bounding box is
clamped to the framebuffer instead. That is correct output for far less code, but
it does feed off-screen fragments to the rasterizer. Measured on a scene that
fills the screen it costs little; for a camera flying through geometry it is the
next thing to fix.

---

## 6. Back face culling and flat shading

One signed area in screen space decides culling, and the same normalised area
feeds the edge functions, so culling costs nothing extra. The sign convention is
documented in embd3d_raster.h: EMBD3D_CULL_BACK drops triangles that are clockwise in
NDC. On the demo sphere roughly half the triangles are dropped before any
fragmentation happens.

The face normal is derived from two triangle edges (cross product, then
normalise). Normalising costs one sqrt per face, which buys rotation independent
lighting when the caller passes the model matrix; passing NULL skips it and lights
the mesh in its own space.

---

## 7. Scalar type: double -> float

1.x used double for vectors and float for matrices, mixing them in
ol3d_matrix_multi_v3. On a Cortex-M4F double is software emulated (about 50x a
single precision multiply); on AVR they are the same. embd3d_real_t is a typedef,
defaulting to float, so the choice is one macro away and no call site casts.

---

## 8. Lines

1.x stepped only along x and divided by dx, so vertical lines drew nothing and
near-vertical ones had gaps. embedded-3d runs a DDA along the major axis with 8 extra
fractional bits, interpolating depth along the way. Bresenham would save the
accumulators, but the difference is not measurable next to the fragment work.

---

## 9. What this refactor deliberately did not do

| idea | why not (yet) |
|---|---|
| scanline / trapezoid raster | ~20% of fill cost on wide triangles, at the cost of a much bigger state machine; the marginal win at 128 px does not justify the code |
| perspective-correct attributes | only matters with textures or Gouraud shading, and neither exists yet |
| tile based / dirty rectangles | for partial updates of a static scene; orthogonal to the pipeline |
| SIMD / DSP intrinsics | the pipeline is a good shape for it, but it would fork the codebase per target |
| integer per-face lighting | needs an integer sqrt per face; see section 11 |

The three big ones that used to be on this list - fixed point transform, shared
vertex cache and full frustum clipping - are now implemented as tiers; they are
sections 11 to 13.

---

## 10. Measurement method (and its limits)

* Both implementations were built into one binary, with the matrices built to the
  same intended field of view (the old setPerspective() was fed a pre-warped
  angle to compensate for its reciprocal bug, otherwise it would have rasterized
  a 3.7x larger object and looked artificially slow), and 300 frames of
  "clear + draw" were timed for each.
* Timing uses clock(), so resolution is coarse: the loops are sized to run for
  hundreds of milliseconds, and the published figures are medians of five runs.
* Fragment counts are printed next to the timings precisely so that drawing less
  cannot hide as a speed-up. They match within 4%.
* Both -O3 and -Os are reported because MCU toolchains usually default to size
  optimisation, and the ordering is stable across both (the gap widens at -Os,
  -66%).
---

## 11. Q16.16 pipeline: why, and when it wins

Everything before this section is a float pipeline with an integer rasterizer.
That split is right for a Cortex-M4F and wrong for an AVR, where a single
float multiply is a call into libgcc.

The cost model (order of magnitude, check your core's manual):

| operation | Cortex-M0 | Cortex-M4F | 8 bit AVR |
|---|---|---|---|
| 32 bit integer multiply | 1 cycle | 1 cycle | 2-8 cycles |
| 64 bit integer multiply | library call, ~20-40 | ~4 | ~30-60 |
| integer divide | 2-12 | 2-12 | ~100-200 |
| software float multiply | ~50-100 | 1 (FPU) | ~150-400 |
| software float divide | ~100-200 | ~14 (FPU) | ~500+ |
| software sqrt | ~500+ | ~14 (FPU) | ~1000+ |

A vertex transform is 16 multiply-adds, so on an M0 it costs roughly 16 soft
float multiplies (800-1600 cycles) or 16 fixed point ones (16 cycles plus the
64 bit widening). That is the entire reason the Q16.16 path exists.

### Representation

* int32, Q16.16: range +-32768, resolution 1/65536.
* Same row major / row-vector layout as the float matrix, so
  embd3d_mat4_to_fx() is one loop of 16 conversions per frame, not a new maths
  library.
* Mesh positions are converted once at load time (embd3d_mesh_positions_to_fx).

### The four operations that are not just "multiply and shift"

1. **Transform.** Four dot products per component, accumulated in int64, then
   shifted right by 16. The homogeneous 1 term becomes (m[12] << 16), so no
   special case is needed for the affine row.
2. **Reciprocal for the perspective divide.** One 64 bit division per vertex:
   invw = (1 << 32) / w, which is exactly Q16.16 of 1/w. There is no cheaper
   exact way, and it happens once per vertex, not per pixel.
3. **Projection.** n in [-1,1] becomes t in [0,1] with a half-scale and an add,
   then t is scaled by the frame width; the subpixel shift is folded into the
   final shift so no extra multiply is needed.
4. **Depth.** The key is d = znear * MAX / w, which is affine in 1/w and so
   still linear in screen space. The scale factor znear * MAX is kept as a plain
   integer, not Q16.16, because 65536 * 65535 does not fit in int32 - the reason
   the fixed path has its own depth_k_i field.

### What it costs in fidelity

The whole scene must fit in +-32768 with 1/65536 resolution. For a 128 px
display and a model of a few hundred units that is comfortable; for a large
world you would scale the scene into that range (which is what the demo does).

### Equivalence

For an unlit scene the Q16.16 and float pipelines produce **bit identical
frames** (verified: 0 of 16384 pixels differ). They disagree only when lighting
is on, because the integer pipeline has no lighting (below).

### Why the integer pipeline is unlit

Flat shading needs a *normalised* face normal, i.e. a square root and a divide
per face. In integer maths that is a Newton iteration for 1/sqrt plus a
division: roughly 200-300 cycles per face, or about 5 ms per frame for 320
faces at 16 MHz - a sixth of a 30 fps budget, spent on shading alone. On the
parts that need the Q16.16 pipeline that is not a good trade, so the integer
path draws the mesh colour and the tier is documented as unlit. If you need
shading there, bake it into per-face colours at load time or use the float
pipeline (it still beats 1.x even without an FPU, just by less).

---

## 12. Shared vertex cache

A closed triangle mesh visits each vertex about six times. Without a cache the
vertex stage runs six times as often as it needs to; at 128 px, where each
triangle covers only a handful of pixels, that is a large share of the frame.

Design:

* 28 bytes per vertex: 16 bytes of clip space, 8 bytes of screen position,
  2 bytes of depth, 2 bytes of validity stamp. Identical size for both
  pipelines, so one buffer size works for either.
* Caller owned (no malloc), sized by the caller; if it is smaller than the mesh
  the draw silently falls back to the uncached path.
* **Per call, not per frame.** Every draw call bumps a stamp, so a cached vertex
  is only reused by the call that produced it. That removes a whole class of
  bugs (stale entries after the camera or the mesh changes) at the cost of not
  amortising across frames. Cross-frame caching would need a dirty flag on the
  mesh and on the matrix; not worth it for the target frame rates.
* The stamp is 16 bit; on wrap (every 65535 calls) the stamps are wiped, and
  embd3d_vertex_cache_init() wipes them too, because the buffer may be recycled
  from the other pipeline, whose entry layout differs.

Measured (1280 triangles, 693 vertices, host -O3): the cache removes 11-14% of
a float frame and 21-24% of a Q16.16 frame. Note the direction - the cheaper the
per-vertex maths, the *less* the cache can buy; the integer pipeline gains more
here only because this host has an FPU that the float path leans on. On a part
without one the ordering flips.

Memory is the catch: 28 B x 693 = 19 KiB, which is more than the frame buffer on
a small MCU. That is why it is a QUALITY tier feature and compiled out with
EMBD3D_ENABLE_VERTEX_CACHE=0.

---

## 13. Full frustum clipping

The near plane is the only one that can produce broken geometry (a division by a
negative w), which is why it was the first thing added. The other four planes
only decide *how much work* reaches the rasterizer: without them an off-screen
triangle still walks its bounding box, which is clamped to the screen but still
costs the per-pixel edge tests over the full viewport.

EMBD3D_FLAG_FULL_CLIP clips the triangle against all six half spaces
(near/far/left/right/bottom/top) with Sutherland-Hodgman. A triangle clipped by
six planes yields at most a 9-gon, hence EMBD3D_MAX_CLIP_VERTS = 9.

Costs: two scratch polygons of 9 vertices (about 290 bytes of stack for the
float pipeline, 290 for the fixed one), and roughly 300 bytes of code. Both are
compile-time optional; on an AVR the near-plane-only path plus the bounding box
clamp is the right choice.

Measured on a scene that fills the screen the difference is in the noise - that
is expected, because nothing is being clipped. The win shows up when the camera
moves through geometry: without side clipping each partially visible triangle
walks the whole viewport.

---

## 14. Choosing a tier

The three presets exist so that the same source builds for very different
parts without an #ifdef maze at the call site.

| | MINIMAL | BALANCED | QUALITY |
|---|---|---|---|
| draw call | embd3d_draw_mesh_fx | embd3d_draw_mesh | embd3d_draw_mesh_cached |
| transform | Q16.16 | float | float |
| per-face lighting | no | yes | yes |
| clipping | near | near | 6 planes |
| depth bits | 8 | 16 | 16 |
| colour | RGB565 or L8 | RGB565 | RGB565 |
| vertex cache | no | no | yes |
| RAM at 128x128 | 24-32 KiB | 64 KiB | 64 KiB + 28 B/vertex |
| flash (host -Os) | 8.4 KiB | 9.9 KiB | 12.7 KiB |

How to pick, in order:

1. Does the part have an FPU? No -> MINIMAL. Yes -> BALANCED or better.
2. Do you have more than 64 KiB of RAM and more than 128 KiB of flash? If not
   drop to 8 bit depth and L8, or to MINIMAL.
3. Is the camera ever inside or crossing geometry? Then enable FULL_CLIP.
4. Is the mesh dense (many triangles per vertex) and do you have the RAM for the
   cache? Then use the cached draw.

Whatever you pick, measure on the part: the ratios in this document come from an
x86 host, and the only number that matters is your frame time on your MCU.
