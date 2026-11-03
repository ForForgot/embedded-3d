# embd3d_spin

A spinning quad on a 128x128 RGB OLED, over SPI. The smallest end to end use of
the library: fill a framebuffer, draw a mesh, hand the bytes to the panel.

## What the sketch assumes

- The panel takes **3 bytes per pixel** (RGB888), which is why the sketch keeps
  `g_rgb` and calls `embd3d_fb_export_rgb888()`. If your driver wants RGB565,
  feed it `g_color` directly and drop `g_rgb` - that saves 48 KiB.
- `OLED_init()` and `OLED_fill()` are yours to provide; the sketch only declares
  them.
- RAM: 128x128 RGB565 colour (32 KiB) + 16 bit depth (32 KiB) + the RGB888 export
  buffer (48 KiB) is **112 KiB**, which is fine on an ESP32 or an M4 with 128 KiB+
  and impossible on an AVR. For a small part, rebuild with
  `EMBD3D_DEPTH_BITS=8`, `EMBD3D_COLOR_FORMAT=1` and the integer pipeline
  (`EMBD3D_ENABLE_FLOAT=0`, `embd3d_draw_mesh_fx`) - see the tier table in the
  top level README - and stream the export in horizontal strips instead of
  keeping a full RGB888 frame.

## Bring-up checklist

1. **It compiles but shows nothing.** Check that `OLED_fill()` is actually called
   with `g_rgb` and that the panel was initialised for the right colour order.
2. **Upside down.** The library maps NDC y up to screen y down, so an inverted
   picture means the panel driver rows go the other way; flip in `OLED_fill()`.
3. **Only the top half.** `OLED_fill()` is probably reading 128x128 bytes instead
   of 128x128x3.
4. **Everything is one flat colour.** That is `OL3D_TIER_MINIMAL` doing what it
   says: unlit. Use `OL3D_TIER_BALANCED` for per face shading.
5. **Torn or slow.** Raise the SPI clock, or drop to 8 bit depth and L8 colour.

## Self test

Render one frame and print a checksum over the framebuffer, then compare it with
the same frame rendered on a desktop with `embd3d_demo`. Identical checksums
mean the pipeline is computing the same image on both - the remaining problems
are then in the driver, not the rasterizer.

    uint32_t h = 2166136261u;
    for (uint32_t i = 0; i < sizeof(g_color); i++) {
        h = (h ^ ((const uint8_t *)g_color)[i]) * 16777619u;   /* FNV-1a */
    }
    Serial.println(h, HEX);

## Honest status

This sketch has been compiled in CI (as C++, with stub `Arduino.h`/`SPI.h`), but
**it has never been flashed to a board by the library author** - there was no
panel to test against. If you run it, a report either way is welcome.
