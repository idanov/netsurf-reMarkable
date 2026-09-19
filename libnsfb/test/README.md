# Bitmap scaling regression

From the repository root, run:

```sh
make test-bitmap
```

This builds the actual RGB565 plotter (`src/plot/16bpp.c`, including `common.c`)
and its clipping/palette helpers with a native C compiler. No display service,
PNG decoder, cross-compilation toolchain, or tablet is needed. Test inputs model
decoded bitmap pixels. AddressSanitizer and UndefinedBehaviorSanitizer are enabled
by default; `HOST_CC` and `TEST_CFLAGS` can override the compiler and flags.

The 56 cases compare every word in an in-memory framebuffer against a direct
nearest-neighbour reference (`source_x = destination_offset_x * source_width /
destination_width`, likewise for y). They exercise both orientations and opaque/
alpha paths, including fully transparent and partially transparent pixels:

- 50 × 50 enlarged to 80 × 80, plus native-size 50 × 50 and 120 × 120 controls.
- Patterned pixels, padded source rows, and padded physical framebuffer rows.
- Enlargement, reduction, integer ratios, and scaling on only one axis.
- Clipping at all four sides, negative destination origins, and fully hidden images.
- Unmodified pixels outside the destination/clip, including framebuffer padding.

The synthetic icon is black except for one white (opaque test) or transparent
(alpha test) top-left pixel, drawn over white. Scaling it to 80 × 80 should leave
four white pixels and draw 6,396 black pixels.

## Diagnosis and minimal correction

`bitmap_scaled` computes integer quotients and remainders using destination
width/height. For 50 → 80, the integer step is zero and the remainder is 50.
The old loops compared the accumulator to 65,536, so neither coordinate advanced
over the 80-pixel output. All pixels sampled the source corner; with a transparent
corner, nothing was drawn. Native-size bitmaps take a different path.

Use `width` and `height` as the horizontal/vertical remainder thresholds and
subtractions. On a vertical carry, advance by `bmp_stride`, not `bmp_width`,
because source rows may include padding. Apply these substitutions in all four
portrait/landscape × opaque/alpha loops; no resampling or rotation redesign is
needed.

Observed results with this harness:

| Implementation | Passing cases | 50 → 80 visible pixels, each orientation/alpha mode |
| --- | ---: | ---: |
| Original | 20/56 | 0 |
| Destination thresholds only | 32/56 | 6,396; padded-row cases still fail |
| Destination thresholds and source stride | 56/56 | 6,396 |

The native 120 × 120 control draws 14,399 black pixels both before and after.
These are renderer tests, not a physical-tablet or end-to-end PNG loading test.
