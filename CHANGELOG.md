# Changelog

All notable changes to this project are recorded here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/) and this project
adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [1.0.1] - 2026-11-03

### Fixed

- **Undefined behaviour in the rasterizer and in the fixed point path.** The
  depth plane setup, the line DDA, the Q16.16 vertex transform and both fixed
  point clippers shifted signed values left to scale them by a power of two,
  which is undefined in C. The output was correct on every compiler tested,
  because they all emit an arithmetic shift, but nothing guarantees it. All of
  them now multiply instead; the three reference frames are bit identical
  before and after.

### Added

- `EMBD3D_VALIDATE_MESH`: opt-in range check of mesh indices, so a mesh with a
  bad index skips the triangle instead of reading past the position array.
  Exercised by an 11th CTest configuration.
- ASan + UBSan CI job - it is what found the bug above.
- Cross builds in CI with `arm-none-eabi-gcc` (Cortex-M0/M3/M4F/M7) and
  `avr-gcc` (ATmega328P/2560). On a `v*` tag the libraries are attached to the
  release with checksums.
- `CHANGELOG.md`, issue and pull request templates.
- `examples/arduino/embd3d_spin/README.md`: what the sketch assumes, a bring-up
  checklist, a framebuffer checksum self test, and an honest note that it has not
  been run on hardware yet.
- Unit test for `embd3d_fb_export_rgb888()`.

## [1.0.0] - 2026-11-02

First release.

### Added

- `embd3d.h` umbrella header and a flat `src/` layout that the Arduino IDE picks up as-is.
- Float vertex pipeline (`embd3d_draw_mesh`) and Q16.16 integer pipeline
  (`embd3d_draw_mesh_fx`); for an unlit scene the two produce bit identical frames.
- Three tier presets (`embd3d_render_ctx_init_tier`): MINIMAL, BALANCED, QUALITY, plus
  `embd3d_tier_precision()` to pick the matching draw interface.
- Integer edge-function rasterizer with configurable subpixel precision.
- 16 bit `1/w` depth buffer, interpolated per fragment.
- Near plane clipping by default; the full 6 plane frustum behind `EMBD3D_FLAG_FULL_CLIP`.
- RGB565 (2 B/px) and L8 (1 B/px) frame buffers, plus `embd3d_fb_export_rgb888()` for
  drivers that want 3 bytes per pixel.
- Optional caller-owned vertex cache (`embd3d_draw_mesh_cached`, 28 B per vertex).
- Per-face flat shading, and `EMBD3D_FLAG_DEPTH_TINT` as a cheap depth cue for unlit builds.
- Compile-time switches: `EMBD3D_ENABLE_FLOAT`, `EMBD3D_ENABLE_FIXED`,
  `EMBD3D_ENABLE_LIGHTING`, `EMBD3D_ENABLE_FULL_CLIP`, `EMBD3D_ENABLE_VERTEX_CACHE`,
  `EMBD3D_DEPTH_BITS`, `EMBD3D_COLOR_FORMAT`, `EMBD3D_INDEX_TYPE`, `EMBD3D_SUBPIXEL_BITS`.
- Unit tests built once per tier configuration (10 CTest targets).
- CI on Linux, macOS and Windows, plus a `-Werror -Wpedantic` job and a C++ consumer
  link check.
- MIT licence.

### Notes

- A static archive is target specific. The release ships one per supported target; the
  source builds anywhere with CMake, and the tier is selected with `-DEMBD3D_...` flags.
- The performance figures in the README come from a measurement run during development
  and are not reproducible from the repository.
