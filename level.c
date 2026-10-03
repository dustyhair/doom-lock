#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <cairo.h>
#include "level.h"

#define MAP_SIZE 25
#define VIEW_WIDTH 416
#define WALK_SPEED 0.34
#define TURN_SECONDS 1.25
#define FOV_PLANE 0.72
#define CELL_UNITS 128.0
#define EYE_HEIGHT (41.0 / CELL_UNITS)
#define ACTOR_DISTANCE 2.4
#define TRAIL_LENGTH 512

typedef struct {
    cairo_surface_t *surface;
    uint32_t *pixels;
    int width, height, stride;
} texture_t;

typedef struct {
    const char *map, *wall, *detail, *floor, *ceiling;
    int light;
    bool slime_room;
} style_t;

/* Wall/floor/ceiling combinations from actual DOOM2.WAD sectors and their
 * sidedefs. One style covers the whole maze, preserving the level's palette. */
static const style_t styles[] = {
    {"MAP01", "TEKGREN2", "TEKGREN5", "FLOOR3_3", "GRNLITE1", 160, false},
    {"MAP02", "STONE4", "PIPEWAL1", "FLAT5_4", "FLAT5_4", 144, false},
    {"MAP05", "BIGBRIK1", "BIGBRIK2", "FLAT1", "FLAT10", 144, false},
    {"MAP14", "BSTONE1", "BSTONE2", "FLOOR5_4", "FLAT1_2", 144, false},
    {"MAP24", "SKIN2", "TANROCK5", "FLOOR7_1", "CEIL5_1", 160, true},
};
static const style_t *style;
static texture_t wall_texture, detail_texture, floor_texture, ceiling_texture, slime[3];
static const int dx[] = {1, 0, -1, 0}, dy[] = {0, 1, 0, -1};
static unsigned char maze[MAP_SIZE][MAP_SIZE];
static unsigned char rooms[MAP_SIZE][MAP_SIZE];
static unsigned visits[MAP_SIZE][MAP_SIZE];
static int cell_x = 3, cell_y = 3, direction, next_direction;
static double camera_x = 3.5, camera_y = 3.5, angle = M_PI;
static double progress, turning, old_angle, turn_angle, world_time;
static unsigned frame_number, rendered_frame = (unsigned)-1;
static cairo_surface_t *view;
typedef struct { double x, y; } point_t;
static point_t trail[TRAIL_LENGTH], focused_actor;
static int trail_end, trail_count;
static bool actor_focused;
static double wall_depth[VIEW_WIDTH];

static void record_position(double x, double y) {
    trail_end = (trail_end + 1) % TRAIL_LENGTH;
    trail[trail_end] = (point_t){x, y};
    if (trail_count < TRAIL_LENGTH) trail_count++;
}

static point_t actor_position(void) {
    if (actor_focused) return focused_actor;
    point_t position = trail[trail_end];
    double remaining = ACTOR_DISTANCE;
    for (int i = 1; i < trail_count; i++) {
        point_t previous = trail[(trail_end - i + TRAIL_LENGTH) % TRAIL_LENGTH];
        double length = hypot(previous.x - position.x, previous.y - position.y);
        if (length >= remaining) {
            double t = remaining / length;
            return (point_t){position.x + (previous.x - position.x) * t,
                             position.y + (previous.y - position.y) * t};
        }
        remaining -= length;
        position = previous;
    }
    return position;
}

typedef struct {
    int x, y, sx, sy;
    bool side;
    double distance;
} hit_t;

static hit_t cast(double ray_x, double ray_y);

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
    /* The route is the direction of travel. Look back along it so the monster
     * approaches while the camera retreats, including through corners. */
    turn_angle = next_direction * M_PI / 2 + M_PI - angle;
    while (turn_angle > M_PI) turn_angle -= 2 * M_PI;
    while (turn_angle < -M_PI) turn_angle += 2 * M_PI;
    turning = fabs(turn_angle) > 0.01 ? 0 : TURN_SECONDS;
    progress = 0;
}

bool level_init(const char *assets) {
    style = &styles[rand() % (sizeof(styles) / sizeof(styles[0]))];
    if (!load_texture(&wall_texture, assets, style->wall) ||
        !load_texture(&detail_texture, assets, style->detail) ||
        !load_texture(&floor_texture, assets, style->floor) ||
        !load_texture(&ceiling_texture, assets, style->ceiling)) return false;
    if (style->slime_room) {
        const char *names[] = {"NUKAGE1", "NUKAGE2", "NUKAGE3"};
        for (int i = 0; i < 3; i++) if (!load_texture(&slime[i], assets, names[i])) return false;
    }
    memset(maze, 1, sizeof(maze));
    carve(1, 1);
    /* Rooms and a few connected shortcuts prevent a corridor-only tour. */
    for (int center_y = 3; center_y < MAP_SIZE - 3; center_y += 8)
        for (int center_x = 3; center_x < MAP_SIZE - 3; center_x += 8)
            for (int y = center_y - 2; y <= center_y + 2; y++)
                for (int x = center_x - 2; x <= center_x + 2; x++) {
                    maze[y][x] = 0;
                    rooms[y][x] = 1 + (center_y - 3) / 8 * 3 + (center_x - 3) / 8;
                }
    for (int y = 1; y < MAP_SIZE - 1; y++)
        for (int x = 1; x < MAP_SIZE - 1; x++)
            if (maze[y][x] && rand() % 12 == 0 &&
                ((!solid(x - 1, y) && !solid(x + 1, y)) ||
                 (!solid(x, y - 1) && !solid(x, y + 1)))) maze[y][x] = 0;
    visits[cell_y][cell_x] = 1;
    /* Seed the pursuer behind the camera inside the starting room. */
    record_position(camera_x - ACTOR_DISTANCE, camera_y);
    record_position(camera_x, camera_y);
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
            angle = direction * M_PI / 2 + M_PI;
        }
        return;
    }
    direction = next_direction;
    progress = fmin(1, progress + seconds * WALK_SPEED);
    camera_x = cell_x + 0.5 + dx[direction] * progress;
    camera_y = cell_y + 0.5 + dy[direction] * progress;
    record_position(camera_x, camera_y);
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

static uint32_t shade(uint32_t pixel, double light) {
    double r = (pixel >> 16) & 255, g = (pixel >> 8) & 255, b = pixel & 255;
    r *= light;
    g *= light;
    b *= light;
    return 0xff000000 | ((uint32_t)fmin(255, r) << 16) |
           ((uint32_t)fmin(255, g) << 8) | (uint32_t)fmin(255, b);
}

static double light_at(double distance) {
    return 0.28 + (style->light / 255.0 + 0.18 - 0.28) / (1 + distance * 0.10);
}

static texture_t *wall_at(int x, int y, int inside_x, int inside_y, bool side) {
    if (solid(inside_x, inside_y)) return &wall_texture;
    int room = rooms[inside_y][inside_x];
    if (room == 0) return &wall_texture;
    /* Keep room boundaries continuous. The middle of a room wall can carry a
     * matching panel, rather than changing material on every grid square. */
    int center_x = 3 + (room - 1) % 3 * 8;
    int center_y = 3 + (room - 1) / 3 * 8;
    if ((style->slime_room && room == 5) || (side ? x == center_x : y == center_y))
        return &detail_texture;
    return &wall_texture;
}

static hit_t cast(double ray_x, double ray_y) {
    hit_t hit = {.x = (int)camera_x, .y = (int)camera_y,
                 .sx = ray_x < 0 ? -1 : 1, .sy = ray_y < 0 ? -1 : 1};
    double delta_x = ray_x == 0 ? 1e20 : fabs(1 / ray_x);
    double delta_y = ray_y == 0 ? 1e20 : fabs(1 / ray_y);
    double side_x = (ray_x < 0 ? camera_x - hit.x : hit.x + 1 - camera_x) * delta_x;
    double side_y = (ray_y < 0 ? camera_y - hit.y : hit.y + 1 - camera_y) * delta_y;
    for (int steps = 0; steps < MAP_SIZE * 2; steps++) {
        if (side_x < side_y) { side_x += delta_x; hit.x += hit.sx; hit.side = false; }
        else { side_y += delta_y; hit.y += hit.sy; hit.side = true; }
        if (solid(hit.x, hit.y)) break;
    }
    hit.distance = fmax(0.08, hit.side ? side_y - delta_y : side_x - delta_x);
    return hit;
}

static int view_height(int width, int height) {
    int logical_height = VIEW_WIDTH * (double)height / width;
    if (logical_height < 96) logical_height = 96;
    if (logical_height > 512) logical_height = 512;
    return logical_height;
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
    double plane_x = -forward_y * FOV_PLANE, plane_y = forward_x * FOV_PLANE;
    double focal = VIEW_WIDTH / (2 * FOV_PLANE);
    int horizon = height / 2;
    for (int y = 0; y < height; y++) {
        bool floor_side = y >= horizon;
        double distance = focal * (floor_side ? EYE_HEIGHT : 1 - EYE_HEIGHT) /
                          (abs(y - horizon) + 0.5);
        double wx = camera_x + distance * (forward_x - plane_x);
        double wy = camera_y + distance * (forward_y - plane_y);
        double step_x = distance * plane_x * 2 / VIEW_WIDTH;
        double step_y = distance * plane_y * 2 / VIEW_WIDTH;
        double light = light_at(distance);
        for (int x = 0; x < VIEW_WIDTH; x++, wx += step_x, wy += step_y) {
            texture_t *texture = floor_side ? &floor_texture : &ceiling_texture;
            /* MAP24's slime is a contained basin with a dry border in one
             * room, using that map's rock walls and ceiling. */
            if (floor_side && style->slime_room && wx >= 10 && wx < 13 && wy >= 10 && wy < 13)
                texture = &slime[(int)(world_time * 2) % 3];
            /* Doom flats repeat every 64 units; maze cells are 128 units. */
            uint32_t pixel = sample(texture, wx * CELL_UNITS / 64, wy * CELL_UNITS / 64);
            pixels[y * stride + x] = shade(pixel, light);
        }
    }
    for (int x = 0; x < VIEW_WIDTH; x++) {
        double screen_x = 2.0 * x / VIEW_WIDTH - 1;
        double ray_x = forward_x + plane_x * screen_x;
        double ray_y = forward_y + plane_y * screen_x;
        hit_t hit = cast(ray_x, ray_y);
        double distance = hit.distance;
        wall_depth[x] = distance;
        double wall_height = focal / distance;
        double top = horizon - wall_height * (1 - EYE_HEIGHT), bottom = horizon + wall_height * EYE_HEIGHT;
        texture_t *texture = wall_at(hit.x, hit.y, hit.side ? hit.x : hit.x - hit.sx,
                                     hit.side ? hit.y - hit.sy : hit.y, hit.side);
        double u = hit.side ? camera_x + distance * ray_x : camera_y + distance * ray_y;
        if ((!hit.side && ray_x > 0) || (hit.side && ray_y < 0)) u = -u;
        double light = (hit.side ? 0.82 : 1) * light_at(distance);
        for (int y = top < 0 ? 0 : top; y < height && y <= bottom; y++) {
            uint32_t pixel = sample(texture, u * CELL_UNITS / texture->width,
                                    (y - top) / wall_height * CELL_UNITS / texture->height);
            pixels[y * stride + x] = shade(pixel, light);
        }
    }
    cairo_surface_mark_dirty(view);
    rendered_frame = frame_number;
}

void level_draw(cairo_t *ctx, int x, int y, int width, int height) {
    if (width <= 0 || height <= 0) return;
    int logical_height = view_height(width, height);
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

level_actor_t level_actor_projection(int width, int height) {
    level_actor_t result = {0};
    if (width <= 0 || height <= 0) return result;
    point_t position = actor_position();
    double vx = position.x - camera_x, vy = position.y - camera_y;
    double forward_x = cos(angle), forward_y = sin(angle);
    result.depth = vx * forward_x + vy * forward_y;
    if (result.depth < 0.08) return result;
    double focal_x = width / (2 * FOV_PLANE);
    double focal_y = VIEW_WIDTH / (2 * FOV_PLANE) * height / view_height(width, height);
    result.x = width * 0.5 + (vy * forward_x - vx * forward_y) * focal_x / result.depth;
    result.floor_y = height * 0.5 + focal_y * EYE_HEIGHT / result.depth;
    result.scale_x = focal_x / (CELL_UNITS * result.depth);
    result.scale_y = focal_y / (CELL_UNITS * result.depth);
    result.visible = result.x > -width * 0.25 && result.x < width * 1.25;
    return result;
}

void level_actor_clip(cairo_t *ctx, int x, int y, int width, int height, double depth) {
    if (depth < 0.08 || width <= 0 || height <= 0) {
        cairo_rectangle(ctx, 0, 0, 0, 0);
        cairo_clip(ctx);
        return;
    }
    render(view_height(width, height));
    double focal_y = VIEW_WIDTH / (2 * FOV_PLANE) * height / view_height(width, height);
    double top = fmax(0, height * 0.5 - focal_y * (1 - EYE_HEIGHT) / depth);
    for (int column = 0; column < VIEW_WIDTH;) {
        if (wall_depth[column] < depth) { column++; continue; }
        int start = column++;
        while (column < VIEW_WIDTH && wall_depth[column] >= depth) column++;
        cairo_rectangle(ctx, x + (double)start * width / VIEW_WIDTH, y + top,
                        (double)(column - start) * width / VIEW_WIDTH, height - top);
    }
    cairo_clip(ctx);
}

void level_actor_focus(void) {
    point_t position = actor_position();
    double vx = position.x - camera_x, vy = position.y - camera_y;
    double distance = hypot(vx, vy);
    if (distance < 0.6 || cast(vx / distance, vy / distance).distance < distance) {
        /* A corner can hide the pursuer. Finish bringing it around that corner
         * for the kill, then freeze its position with the authenticated scene. */
        point_t previous = {camera_x, camera_y};
        double path_length = 0;
        for (int i = 1; i < trail_count; i++) {
            point_t candidate = trail[(trail_end - i + TRAIL_LENGTH) % TRAIL_LENGTH];
            path_length += hypot(candidate.x - previous.x, candidate.y - previous.y);
            previous = candidate;
            if (path_length > ACTOR_DISTANCE) break;
            double cx = candidate.x - camera_x, cy = candidate.y - camera_y;
            double length = hypot(cx, cy);
            if (length >= 0.6 && cast(cx / length, cy / length).distance >= length) position = candidate;
        }
    }
    focused_actor = position;
    actor_focused = true;
    vx = position.x - camera_x;
    vy = position.y - camera_y;
    /* Aim the view at the body if it has left the field of view during a turn. */
    if (vx * cos(angle) + vy * sin(angle) <= 0 ||
        fabs(vy * cos(angle) - vx * sin(angle)) >
        (vx * cos(angle) + vy * sin(angle)) * FOV_PLANE * 0.8)
        angle = atan2(vy, vx);
    frame_number++;
}
