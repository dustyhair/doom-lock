#ifndef DOOM_MELT_H
#define DOOM_MELT_H
#include <stdbool.h>
#include <cairo.h>

bool melt_begin(bool reveal_desktop);
void melt_cancel(void);
void melt_draw(cairo_t *ctx);
void melt_shape(void);
#endif
