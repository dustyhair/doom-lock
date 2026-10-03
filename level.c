#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <cairo.h>
#include "level.h"

#define MAP_SIZE 25
#define VIEW_WIDTH 416
#define WALL_COUNT 7
#define FLAT_COUNT 10
#define WALK_SPEED 0.34
#define TURN_SECONDS 1.25

typedef struct {
    cairo_surface_t *surface;
    uint32_t *pixels;
    int width, height, stride;
} texture_t;

static texture_t walls[WALL_COUNT], flats[FLAT_COUNT];
static const char *wall_names[] = {"STARTAN3", "TEKWALL4", "COMPTALL", "COMPBLUE", "STONE3", "BRICK7", "METAL2"};
static const char *flat_names[] = {"FLOOR0_1", "FLOOR4_8", "CEIL3_5", "NUKAGE1", "NUKAGE2", "NUKAGE3", "LAVA1", "LAVA2", "LAVA3", "LAVA4"};
static const int dx[] = {1, 0, -1, 0}, dy[] = {0, 1, 0, -1};
static unsigned char maze[MAP_SIZE][MAP_SIZE];
static unsigned visits[MAP_SIZE][MAP_SIZE];
static int cell_x = 3, cell_y = 3, direction, next_direction;
static double camera_x = 3.5, camera_y = 3.5, angle;
static double progress, turning, old_angle, turn_angle, world_time;
static unsigned frame_number, rendered_frame = (unsigned)-1;
static cairo_surface_t *view;

static bool load_texture(texture_t *texture, const char *assets, const char *name) {
    char path[4096];
    snprintf(path, sizeof(path), "%s/level/%s.png", assets, name);
    texture->surface = cairo_image_surface_create_from_png(path);
    if (cairo_surface_status(texture->surface) != CAIRO_STATUS_SUCCESS) return false;
    texture->width = cairo_image_surface_get_width(texture->surface);
    texture->height = cairo_image_surface_get_height(texture->surface);
    texture->stride = cairo_image_surface_get_stride(texture->surface) / 4;
    texture->pixels = (uint32_t *)cairo_image_surface_get_data(texture->surface);
    return texture->width > 0 && texture->height > 0;
}

static bool solid(int x, int y) {
    return x < 0 || y < 0 || x >= MAP_SIZE || y >= MAP_SIZE || maze[y][x] != 0;
}

static int sector(int x, int y) {
    return ((x / 4 + y / 4) % 3 + 3) % 3;
}

static void carve(int x, int y) {
    maze[y][x] = 0;
    int order[] = {0, 1, 2, 3};
    for (int i = 3; i > 0; i--) {
        int j = rand() % (i + 1), swap = order[i];
        order[i] = order[j];
        order[j] = swap;
    }
    for (int i = 0; i < 4; i++) {
        int nx = x + dx[order[i]] * 2, ny = y + dy[order[i]] * 2;
        if (nx <= 0 || ny <= 0 || nx >= MAP_SIZE - 1 || ny >= MAP_SIZE - 1 || !maze[ny][nx]) continue;
        maze[y + dy[order[i]]][x + dx[order[i]]] = 0;
        carve(nx, ny);
    }
}

static void choose_route(void) {
    unsigned best = (unsigned)-1;
    next_direction = direction;
    for (int candidate = 0; candidate < 4; candidate++) {
        int nx = cell_x + dx[candidate], ny = cell_y + dy[candidate];
        if (solid(nx, ny)) continue;
        unsigned score = visits[ny][nx] * 3 + rand() % 3;
        if (candidate != direction) score += 2;
        if (candidate == (direction + 2) % 4) score += 6;
        if (score < best) {
            best = score;
            next_direction = candidate;
        }
    }
    old_angle = angle;
    turn_angle = next_direction * M_PI / 2 - angle;
    while (turn_angle > M_PI) turn_angle -= 2 * M_PI;
    while (turn_angle < -M_PI) turn_angle += 2 * M_PI;
    turning = fabs(turn_angle) > 0.01 ? 0 : TURN_SECONDS;
    progress = 0;
}

bool level_init(const char *assets) {
    for (int i = 0; i < WALL_COUNT; i++) if (!load_texture(&walls[i], assets, wall_names[i])) return false;
    for (int i = 0; i < FLAT_COUNT; i++) if (!load_texture(&flats[i], assets, flat_names[i])) return false;
    memset(maze, 1, sizeof(maze));
    carve(1, 1);
    /* Rooms and a few connected shortcuts prevent a corridor-only tour. */
    for (int center_y = 3; center_y < MAP_SIZE - 3; center_y += 8)
        for (int center_x = 3; center_x < MAP_SIZE - 3; center_x += 8)
            for (int y = center_y - 2; y <= center_y + 2; y++)
                for (int x = center_x - 2; x <= center_x + 2; x++) maze[y][x] = 0;
    for (int y = 1; y < MAP_SIZE - 1; y++)
        for (int x = 1; x < MAP_SIZE - 1; x++)
            if (maze[y][x] && rand() % 12 == 0 &&
                ((!solid(x - 1, y) && !solid(x + 1, y)) ||
                 (!solid(x, y - 1) && !solid(x, y + 1)))) maze[y][x] = 0;
    visits[cell_y][cell_x] = 1;
    choose_route();
    return true;
}

void level_tick(double seconds) {
    world_time += seconds;
    frame_number++;
    if (turning < TURN_SECONDS) {
        turning = fmin(TURN_SECONDS, turning + seconds);
        double t = turning / TURN_SECONDS;
        angle = old_angle + turn_angle * t * t * (3 - 2 * t);
        if (turning == TURN_SECONDS) {
            direction = next_direction;
            angle = direction * M_PI / 2;
        }
        return;
    }
    direction = next_direction;
    progress = fmin(1, progress + seconds * WALK_SPEED);
    camera_x = cell_x + 0.5 + dx[direction] * progress;
    camera_y = cell_y + 0.5 + dy[direction] * progress;
    if (progress == 1) {
        cell_x += dx[direction];
        cell_y += dy[direction];
        visits[cell_y][cell_x]++;
        choose_route();
    }
}

static uint32_t sample(texture_t *texture, double u, double v) {
    u -= floor(u);
    v -= floor(v);
    int x = (int)(u * texture->width), y = (int)(v * texture->height);
    return texture->pixels[y * texture->stride + x];
}

static uint32_t shade(uint32_t pixel, double light, int theme, bool glow) {
    double r = (pixel >> 16) & 255, g = (pixel >> 8) & 255, b = pixel & 255;
    r *= light;
    g *= light;
    b *= light;
    if (theme == 0) { g += 5; b += 10; }
    if (theme == 1) g += glow ? 18 : 7;
    if (theme == 2) { r += glow ? 22 : 10; g += 3; }
    return 0xff000000 | ((uint32_t)fmin(255, r) << 16) |
           ((uint32_t)fmin(255, g) << 8) | (uint32_t)fmin(255, b);
}

static void render(int height) {
    if (!view || cairo_image_surface_get_height(view) != height) {
        if (view) cairo_surface_destroy(view);
        view = cairo_image_surface_create(CAIRO_FORMAT_RGB24, VIEW_WIDTH, height);
        rendered_frame = (unsigned)-1;
    }
    if (cairo_surface_status(view) != CAIRO_STATUS_SUCCESS || rendered_frame == frame_number) return;
    cairo_surface_flush(view);
    uint32_t *pixels = (uint32_t *)cairo_image_surface_get_data(view);
    int stride = cairo_image_surface_get_stride(view) / 4;
    double forward_x = cos(angle), forward_y = sin(angle);
    double plane_x = -forward_y * 0.72, plane_y = forward_x * 0.72;
    int horizon = height / 2;
    for (int y = 0; y < height; y++) {
        double distance = height * 0.5 / (abs(y - horizon) + 0.5);
        double wx = camera_x + distance * (forward_x - plane_x);
        double wy = camera_y + distance * (forward_y - plane_y);
        double step_x = distance * plane_x * 2 / VIEW_WIDTH;
        double step_y = distance * plane_y * 2 / VIEW_WIDTH;
        double light = 0.36 + 0.64 / (1 + distance * 0.10);
        for (int x = 0; x < VIEW_WIDTH; x++, wx += step_x, wy += step_y) {
            int theme = sector((int)floor(wx), (int)floor(wy));
            bool floor_side = y >= horizon;
            int flat = floor_side ? (theme == 1 ? 3 + (int)(world_time * 2) % 3 :
                                    theme == 2 ? 6 + (int)(world_time * 2) % 4 : 1) : 2;
            uint32_t pixel = sample(&flats[flat], wx, wy);
            pixels[y * stride + x] = shade(pixel, light, theme, floor_side && theme != 0);
        }
    }
    for (int x = 0; x < VIEW_WIDTH; x++) {
        double screen_x = 2.0 * x / VIEW_WIDTH - 1;
        double ray_x = forward_x + plane_x * screen_x;
        double ray_y = forward_y + plane_y * screen_x;
        int mx = (int)camera_x, my = (int)camera_y;
        double delta_x = ray_x == 0 ? 1e20 : fabs(1 / ray_x);
        double delta_y = ray_y == 0 ? 1e20 : fabs(1 / ray_y);
        int sx = ray_x < 0 ? -1 : 1, sy = ray_y < 0 ? -1 : 1;
        double side_x = (ray_x < 0 ? camera_x - mx : mx + 1 - camera_x) * delta_x;
        double side_y = (ray_y < 0 ? camera_y - my : my + 1 - camera_y) * delta_y;
        bool side = false;
        for (int steps = 0; steps < MAP_SIZE * 2; steps++) {
            if (side_x < side_y) { side_x += delta_x; mx += sx; side = false; }
            else { side_y += delta_y; my += sy; side = true; }
            if (solid(mx, my)) break;
        }
        double distance = fmax(0.08, side ? side_y - delta_y : side_x - delta_x);
        int wall_height = height / distance;
        int top = horizon - wall_height / 2, bottom = horizon + wall_height / 2;
        int theme = sector(mx, my);
        int pattern = (mx * 7 + my * 11) % 4;
        int material = theme == 0 ? (pattern == 0 ? 3 : pattern == 1 ? 2 : 1) :
                       theme == 1 ? (pattern == 0 ? 4 : 0) : (pattern == 0 ? 6 : 5);
        double u = side ? camera_x + distance * ray_x : camera_y + distance * ray_y;
        if ((!side && ray_x > 0) || (side && ray_y < 0)) u = -u;
        double light = (side ? 0.78 : 1) * (0.42 + 0.58 / (1 + distance * 0.11));
        for (int y = top < 0 ? 0 : top; y < height && y <= bottom; y++) {
            uint32_t pixel = sample(&walls[material], u, (double)(y - top) / wall_height);
            pixels[y * stride + x] = shade(pixel, light, theme, material == 2 || material == 3);
        }
    }
    cairo_surface_mark_dirty(view);
    rendered_frame = frame_number;
}

void level_draw(cairo_t *ctx, int x, int y, int width, int height) {
    if (width <= 0 || height <= 0) return;
    int logical_height = VIEW_WIDTH * (double)height / width;
    if (logical_height < 96) logical_height = 96;
    if (logical_height > 512) logical_height = 512;
    render(logical_height);
    if (!view || cairo_surface_status(view) != CAIRO_STATUS_SUCCESS) return;
    cairo_save(ctx);
    cairo_rectangle(ctx, x, y, width, height);
    cairo_clip(ctx);
    cairo_translate(ctx, x, y);
    cairo_scale(ctx, (double)width / VIEW_WIDTH, (double)height / logical_height);
    cairo_set_source_surface(ctx, view, 0, 0);
    cairo_pattern_set_filter(cairo_get_source(ctx), CAIRO_FILTER_NEAREST);
    cairo_paint(ctx);
    cairo_restore(ctx);
}
