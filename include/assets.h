#ifndef DOOM_ASSETS_H
#define DOOM_ASSETS_H
#include <stdbool.h>
#include <cairo.h>

/* Returned surfaces belong to the caller. WAD data is only needed at startup. */
bool assets_open(const char *wad_path);
void assets_close(void);
cairo_surface_t *assets_image(const char *folder, const char *name);
bool assets_origin(const char *folder, const char *monster, int *x, int *y);
#endif
