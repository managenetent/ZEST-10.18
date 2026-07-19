/* gen_desktop_grid.c - standalone tool, no relation to any one game
 * project (mutaclsym/zoo_0000/egg-pals all draw entities on the SAME
 * real-desktop grid egg-pals' own system/egg_window.c defines -
 * GRID_CELL_PX=80 - so this tool is deliberately kept generic and
 * outside any single project's own tree).
 *
 * Draws an exact desktop-background grid image as a real PNG, using
 * stb_image_write.h (the same header this codebase family's
 * emoji_gen_atlas.c already uses - copied here unmodified, not
 * reinvented). Replaces the earlier "hand this text prompt to an image
 * generator" approach (still in z0.zoo_0000/dox/desk-grid-prompt.txt)
 * with deterministic, exact code: every grid line lands on precisely
 * the same pixel a real desktop-pet window would snap to, which no
 * image-generation model can be trusted to get pixel-exact.
 *
 * Usage:
 *   gen_desktop_grid.+x <out.png> <width> <height> <cell_px> \
 *       <line_r> <line_g> <line_b> <bg_r> <bg_g> <bg_b> [line_thickness_px]
 *
 * Example (this machine's real screen, confirmed via `xrandr`, at the
 * real GRID_CELL_PX=80 egg_window.c/zoo_window.c both use):
 *   ./gen_desktop_grid.+x wallpaper.png 2496 1664 80 60 60 70 30 30 38 1
 */
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "lib/stb_image_write.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv) {
    if (argc < 11) {
        fprintf(stderr,
            "Usage: %s <out.png> <width> <height> <cell_px> "
            "<line_r> <line_g> <line_b> <bg_r> <bg_g> <bg_b> [line_thickness_px]\n",
            argv[0]);
        return 1;
    }

    const char *out_path = argv[1];
    int width = atoi(argv[2]);
    int height = atoi(argv[3]);
    int cell_px = atoi(argv[4]);
    int line_r = atoi(argv[5]);
    int line_g = atoi(argv[6]);
    int line_b = atoi(argv[7]);
    int bg_r = atoi(argv[8]);
    int bg_g = atoi(argv[9]);
    int bg_b = atoi(argv[10]);
    int thickness = (argc >= 12) ? atoi(argv[11]) : 1;

    if (width <= 0 || height <= 0 || cell_px <= 0 || thickness <= 0) {
        fprintf(stderr, "width/height/cell_px/thickness must all be positive\n");
        return 1;
    }

    unsigned char *pixels = malloc((size_t)width * height * 3);
    if (!pixels) {
        fprintf(stderr, "out of memory (%dx%d)\n", width, height);
        return 1;
    }

    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            size_t idx = ((size_t)y * width + x) * 3;
            pixels[idx + 0] = (unsigned char)bg_r;
            pixels[idx + 1] = (unsigned char)bg_g;
            pixels[idx + 2] = (unsigned char)bg_b;
        }
    }

    /* Vertical lines at every multiple of cell_px - x=0, cell_px,
     * 2*cell_px, ... - same origin-at-(0,0) convention egg_window.c's
     * own grid math uses (grid_x * GRID_CELL_PX), so a real pet window
     * snapped to column N sits with its left edge exactly on one of
     * these lines. */
    for (int gx = 0; gx <= width; gx += cell_px) {
        for (int t = 0; t < thickness; t++) {
            int x = gx + t;
            if (x < 0 || x >= width) continue;
            for (int y = 0; y < height; y++) {
                size_t idx = ((size_t)y * width + x) * 3;
                pixels[idx + 0] = (unsigned char)line_r;
                pixels[idx + 1] = (unsigned char)line_g;
                pixels[idx + 2] = (unsigned char)line_b;
            }
        }
    }

    /* Horizontal lines, same logic. */
    for (int gy = 0; gy <= height; gy += cell_px) {
        for (int t = 0; t < thickness; t++) {
            int y = gy + t;
            if (y < 0 || y >= height) continue;
            for (int x = 0; x < width; x++) {
                size_t idx = ((size_t)y * width + x) * 3;
                pixels[idx + 0] = (unsigned char)line_r;
                pixels[idx + 1] = (unsigned char)line_g;
                pixels[idx + 2] = (unsigned char)line_b;
            }
        }
    }

    int ok = stbi_write_png(out_path, width, height, 3, pixels, width * 3);
    free(pixels);

    if (!ok) {
        fprintf(stderr, "stbi_write_png failed for %s\n", out_path);
        return 1;
    }

    int cols = width / cell_px;
    int rows = height / cell_px;
    printf("wrote %s (%dx%d), grid %dx%d cells at %dpx (unused margin: %dpx right, %dpx bottom)\n",
           out_path, width, height, cols, rows, cell_px,
           width - cols * cell_px, height - rows * cell_px);
    return 0;
}
