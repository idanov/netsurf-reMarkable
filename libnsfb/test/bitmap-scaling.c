/* RGB565 bitmap regression tests; no display or image decoder required.
 * Licensed under the MIT License, like libnsfb (see ../COPYING).
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "libnsfb.h"
#include "libnsfb_plot.h"
#include "nsfb.h"
#include "plot.h"

extern const nsfb_plotter_fns_t _nsfb_16bpp_plotters;

struct testcase {
        const char *name;
        int source_width, source_height, source_stride;
        nsfb_bbox_t destination;
        nsfb_bbox_t clip; /* all zeros means the whole surface */
        bool corner; /* black image with a white/transparent top-left pixel */
};

static uint16_t rgb565(nsfb_colour_t colour)
{
        unsigned int red = colour & 255;
        unsigned int green = (colour >> 8) & 255;
        unsigned int blue = (colour >> 16) & 255;
        return (uint16_t)((red / 8) * 2048 + (green / 4) * 32 + blue / 8);
}

static uint16_t over_white(nsfb_colour_t colour, bool alpha)
{
        unsigned int opacity = colour >> 24;
        unsigned int red, green, blue;
        if (!alpha || opacity == 255)
                return rgb565(colour);
        if (opacity == 0)
                return UINT16_MAX;

        /* RGB565 white expands to (248, 252, 248); blending uses /256. */
        red = ((colour & 255) * opacity + 248 * (256 - opacity)) / 256;
        green = (((colour >> 8) & 255) * opacity + 252 * (256 - opacity)) / 256;
        blue = (((colour >> 16) & 255) * opacity + 248 * (256 - opacity)) / 256;
        return rgb565(red | (green << 8) | (blue << 16));
}

static bool run_case(const struct testcase *test, int orientation, bool alpha)
{
        /* Non-square physical surface, with padding after each RGB565 row. */
        enum { PHYSICAL_WIDTH = 144, PHYSICAL_HEIGHT = 160, ROW_PIXELS = 152 };
        const size_t words = ROW_PIXELS * PHYSICAL_HEIGHT;
        uint16_t *actual = malloc(words * sizeof(*actual));
        uint16_t *expected = malloc(words * sizeof(*expected));
        nsfb_colour_t *source = malloc(test->source_stride *
                        test->source_height * sizeof(*source));
        nsfb_t fb = {0};
        int x, y;
        size_t i, mismatches = 0, visible = 0, expected_visible = 0;
        int width = test->destination.x1 - test->destination.x0;
        int height = test->destination.y1 - test->destination.y0;
        bool ok;

        if (actual == NULL || expected == NULL || source == NULL) {
                fprintf(stderr, "Allocation failed\n");
                exit(EXIT_FAILURE);
        }
        for (i = 0; i < words; i++)
                actual[i] = expected[i] = UINT16_MAX;
        for (y = 0; y < test->source_height; y++) {
                for (x = 0; x < test->source_stride; x++) {
                        nsfb_colour_t colour = 0xffff00ff; /* padding sentinel */
                        if (x < test->source_width) {
                                if (test->corner) {
                                        colour = (x == 0 && y == 0) ?
                                                (alpha ? 0 : 0xffffffff) : 0xff000000;
                                } else {
                                        unsigned int a = 255;
                                        if (alpha && (x + y) % 3 != 0)
                                                a = ((x + y) % 3 == 1) ? 0 : 128;
                                        colour = ((x * 37 + y * 11) & 255) |
                                                (((x * 13 + y * 43) & 255) << 8) |
                                                (((x * 53 + y * 7) & 255) << 16) |
                                                (a << 24);
                                }
                        }
                        source[y * test->source_stride + x] = colour;
                }
        }

        fb.width = orientation ? PHYSICAL_HEIGHT : PHYSICAL_WIDTH;
        fb.height = orientation ? PHYSICAL_WIDTH : PHYSICAL_HEIGHT;
        fb.ptr = (uint8_t *)actual;
        fb.linelen = fb.phys_linelen = ROW_PIXELS * sizeof(*actual);
        fb.phys_width = PHYSICAL_WIDTH;
        fb.phys_height = PHYSICAL_HEIGHT;
        fb.orientation = orientation;
        fb.format = NSFB_FMT_RGB565;
        fb.bpp = 16;
        fb.clip = test->clip;
        if (fb.clip.x1 == 0 && fb.clip.y1 == 0)
                fb.clip = (nsfb_bbox_t){0, 0, fb.width, fb.height};

        /* Direct nearest-neighbour reference, independent of remainder stepping.
         * Check the entire physical buffer, including untouched areas/padding.
         */
        for (y = fb.clip.y0; y < fb.clip.y1; y++) {
                for (x = fb.clip.x0; x < fb.clip.x1; x++) {
                        int sx, sy, px, py;
                        if (x < test->destination.x0 || x >= test->destination.x1 ||
                            y < test->destination.y0 || y >= test->destination.y1)
                                continue;
                        sx = (x - test->destination.x0) * test->source_width / width;
                        sy = (y - test->destination.y0) * test->source_height / height;
                        px = orientation ? PHYSICAL_WIDTH - 1 - y : x;
                        py = orientation ? x : y;
                        expected[py * ROW_PIXELS + px] = over_white(
                                        source[sy * test->source_stride + sx], alpha);
                }
        }

        ok = _nsfb_16bpp_plotters.bitmap(&fb, &test->destination, source,
                        test->source_width, test->source_height,
                        test->source_stride, alpha);
        for (i = 0; i < words; i++) {
                mismatches += actual[i] != expected[i];
                visible += actual[i] != UINT16_MAX;
                expected_visible += expected[i] != UINT16_MAX;
        }
        printf("%s %-20s %-9s %-6s mismatches=%zu visible=%zu/%zu\n",
                        ok && mismatches == 0 ? "PASS" : "FAIL", test->name,
                        orientation ? "landscape" : "portrait",
                        alpha ? "alpha" : "opaque", mismatches,
                        visible, expected_visible);
        free(source);
        free(expected);
        free(actual);
        return ok && mismatches == 0;
}

int main(void)
{
        const struct testcase cases[] = {
                {"icon-50-to-80", 50, 50, 50, {7, 11, 87, 91}, {0}, true},
                {"icon-native-50", 50, 50, 50, {7, 11, 57, 61}, {0}, true},
                {"icon-native-120", 120, 120, 120, {7, 11, 127, 131}, {0}, true},
                {"upscale-pattern", 50, 50, 50, {7, 11, 87, 91}, {0}, false},
                {"upscale-padded", 50, 50, 57, {7, 11, 87, 91}, {0}, false},
                {"downscale-padded", 83, 61, 89, {7, 11, 44, 40}, {0}, false},
                {"integer-downscale", 80, 60, 87, {7, 11, 47, 41}, {0}, false},
                {"integer-upscale", 25, 17, 31, {7, 11, 82, 62}, {0}, false},
                {"horizontal-only", 23, 19, 29, {7, 11, 48, 30}, {0}, false},
                {"vertical-only", 23, 19, 29, {7, 11, 30, 42}, {0}, false},
                {"clipped-inset", 37, 29, 43, {3, 5, 86, 72}, {12, 16, 70, 58}, false},
                {"clipped-negative", 37, 29, 43, {-9, -7, 74, 60}, {0}, false},
                {"fully-clipped", 50, 50, 57, {-90, -90, -10, -10}, {0}, false},
                {"native-padded", 23, 19, 29, {7, 11, 30, 30}, {0}, false},
        };
        size_t i;
        int orientation, alpha, failed = 0, total = 0;
        for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
                for (orientation = 0; orientation < 2; orientation++) {
                        for (alpha = 0; alpha < 2; alpha++) {
                                failed += !run_case(&cases[i], orientation, alpha != 0);
                                total++;
                        }
                }
        }
        printf("%d/%d cases passed\n", total - failed, total);
        return failed ? EXIT_FAILURE : EXIT_SUCCESS;
}
