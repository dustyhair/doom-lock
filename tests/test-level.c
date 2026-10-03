/* Run a long maze tour under address/undefined-behavior sanitizers. */
#include <stdlib.h>
#include <cairo.h>
#include "level.h"

int main(int argc, char **argv) {
    if (argc != 3) return 2;
    srand(strtoul(argv[2], NULL, 10));
    if (!level_init(argv[1])) return 3;
    cairo_surface_t *surface = cairo_image_surface_create(CAIRO_FORMAT_RGB24, 1280, 1024);
    cairo_t *ctx = cairo_create(surface);
    for (int i = 0; i < 10000; i++) {
        level_tick(0.14);
        if (i % 100 == 0) {
            /* Exercise different viewport shapes as the tour visits rooms,
             * dead ends, corners, and shortcuts over 23 simulated minutes. */
            level_draw(ctx, 0, 0, i % 200 ? 1280 : 1024, 768);
            if (cairo_status(ctx) != CAIRO_STATUS_SUCCESS) return 4;
        }
    }
    cairo_destroy(ctx);
    cairo_surface_destroy(surface);
    return 0;
}
