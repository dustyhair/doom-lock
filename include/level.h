#ifndef DOOM_LEVEL_H
#define DOOM_LEVEL_H
#include <stdbool.h>
#include <cairo.h>

bool level_init(const char *assets);
void level_tick(double seconds);
void level_draw(cairo_t *ctx, int x, int y, int width, int height);
typedef struct {
    double x, floor_y, scale_x, scale_y, depth;
    bool visible;
} level_actor_t;
level_actor_t level_actor_projection(int width, int height);
void level_actor_clip(cairo_t *ctx, int x, int y, int width, int height, double depth);
void level_actor_focus(void);
#endif
