/* Check the native WAD decoder against independently extracted PNG pixels. */
#include <assert.h>
#include <string.h>
#include "../assets.c"

int main(int argc, char **argv) {
    if (argc != 3 && argc != 4) {
        return 2;
    }
    bool valid = assets_open(argv[2]);
    if (strcmp(argv[1], "--validate") == 0) {
        assets_close();
        return valid ? 0 : 1;
    }
    if (!valid || argc != 4) {
        return 1;
    }
#if DOOM_WAD_ASSETS
    size_t count = image_count;
    image_t retained[sizeof(images) / sizeof(images[0])];
    memcpy(retained, images, sizeof(retained));
    for (size_t i = 0; i < count; i++) {
        retained[i].surface = cairo_surface_reference(retained[i].surface);
    }
    for (size_t i = 0; i < sizeof(sprites) / sizeof(sprites[0]); i++) {
        char path[4096];
        snprintf(path, sizeof(path), "%s/%s/origin.txt", argv[3], sprites[i].id);
        FILE *file = fopen(path, "r");
        assert(file);
        int x, y;
        assert(fscanf(file, "%d %d", &x, &y) == 2);
        fclose(file);
        assert(x == sprites[i].origin_x && y == sprites[i].origin_y);
    }
    /* Decoded surfaces must remain valid after the WAD buffer is released. */
    assets_close();
    for (size_t i = 0; i < count; i++) {
        cairo_surface_t *native = retained[i].surface;
        cairo_surface_t *png = assets_image(argv[3], retained[i].name);
        assert(png);
        int width = cairo_image_surface_get_width(native), height = cairo_image_surface_get_height(native);
        assert(width == cairo_image_surface_get_width(png) && height == cairo_image_surface_get_height(png));
        /* Normalize RGB PNGs to ARGB before comparing their unused high byte. */
        cairo_surface_t *expected = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, width, height);
        cairo_t *ctx = cairo_create(expected);
        cairo_set_source_surface(ctx, png, 0, 0);
        cairo_paint(ctx);
        assert(cairo_status(ctx) == CAIRO_STATUS_SUCCESS);
        cairo_destroy(ctx);
        cairo_surface_flush(expected);
        cairo_surface_flush(native);
        unsigned char *a = cairo_image_surface_get_data(native), *b = cairo_image_surface_get_data(expected);
        for (int y = 0; y < height; y++) {
            if (memcmp(a + y * cairo_image_surface_get_stride(native),
                       b + y * cairo_image_surface_get_stride(expected), width * 4) != 0) {
                fprintf(stderr, "Pixel mismatch: %s, row %d\n", retained[i].name, y);
                return 3;
            }
        }
        cairo_surface_destroy(expected);
        cairo_surface_destroy(png);
        cairo_surface_destroy(native);
    }
    printf("PASS: %zu images and all 14 origins match PNG extraction; surfaces survive WAD release\n", count);
    cairo_debug_reset_static_data();
    return 0;
#else
    return 1;
#endif
}
