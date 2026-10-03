#ifndef DOOM_LEVEL_H
#define DOOM_LEVEL_H
#include <stdbool.h>
#include <cairo.h>

bool level_init(const char *assets);
void level_tick(double seconds);
void level_draw(cairo_t *ctx, int x, int y, int width, int height);
#endif
