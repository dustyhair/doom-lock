#include <stdlib.h>
#include <cairo.h>
#include <ev.h>
#include <xcb/shape.h>
#include "doom.h"
#include "melt.h"
#include "randr.h"
#include "unlock_indicator.h"
#include "xcb.h"

#define MELT_COLUMNS 160
#define MELT_HEIGHT 200

typedef struct {
    int x, y, width, height, position;
} melt_column_t;

static cairo_surface_t *snapshot, *falling_frame;
static melt_column_t *columns;
static xcb_rectangle_t *rectangles;
static int column_count;
static bool active, reveal, shape_available;
static ev_timer timer;
static double started;
static int ticks_drawn;
extern struct ev_loop *main_loop;
extern xcb_window_t win;
extern uint32_t last_resolution[2];

void melt_cancel(void) {
    if (!active) {
        return;
    }
    ev_timer_stop(main_loop, &timer);
    active = false;
    cairo_surface_destroy(snapshot);
    cairo_surface_destroy(falling_frame);
    snapshot = NULL;
    falling_frame = NULL;
    free(columns);
    free(rectangles);
    columns = NULL;
    rectangles = NULL;
    /* A resize can interrupt an authenticated melt after the monster timer
     * stops. Finish unlocking rather than leave a stopped animation locked. */
    if (reveal && doom_is_authenticated()) {
        ev_break(main_loop, EVBREAK_ALL);
    }
}

static void tick(EV_P_ ev_timer *watcher, int events) {
    bool done = true;
    int elapsed_ticks = (ev_now(loop) - started) * 35;
    if (elapsed_ticks <= ticks_drawn) {
        return;
    }
    /* Preserve the wipe's duration if a slower display skips render frames. */
    for (; ticks_drawn < elapsed_ticks; ticks_drawn++) {
        done = true;
        for (int i = 0; i < column_count; i++) {
            int *position = &columns[i].position;
            if (*position < 0) {
                ++*position;
            } else if (*position < MELT_HEIGHT) {
                int step = *position < 16 ? *position + 1 : 8;
                *position += step;
                if (*position > MELT_HEIGHT) {
                    *position = MELT_HEIGHT;
                }
            }
            if (*position < MELT_HEIGHT) {
                done = false;
            }
        }
        if (done) {
            break;
        }
    }
    redraw_screen();
    if (done) {
        melt_cancel();
    }
}

bool melt_begin(bool reveal_desktop) {
    if (!doom_enabled() || (reveal_desktop && !doom_is_authenticated())) {
        return false;
    }
    melt_cancel();
    snapshot = capture_lock_frame();
    if (!snapshot) {
        return false;
    }
    falling_frame = cairo_image_surface_create(CAIRO_FORMAT_ARGB32,
                                               last_resolution[0], last_resolution[1]);
    int monitors = xr_screens > 0 ? xr_screens : 1;
    columns = calloc(monitors * MELT_COLUMNS, sizeof(*columns));
    rectangles = calloc(monitors * MELT_COLUMNS, sizeof(*rectangles));
    if (!columns || !rectangles || cairo_surface_status(falling_frame) != CAIRO_STATUS_SUCCESS) {
        cairo_surface_destroy(snapshot);
        cairo_surface_destroy(falling_frame);
        snapshot = NULL;
        falling_frame = NULL;
        free(columns);
        free(rectangles);
        columns = NULL;
        rectangles = NULL;
        return false;
    }
    column_count = 0;
    for (int monitor = 0; monitor < monitors; monitor++) {
        Rect region = xr_screens > 0 ? xr_resolutions[monitor]
                                     : (Rect){0, 0, last_resolution[0], last_resolution[1]};
        int delay = -(rand() % 16);
        for (int i = 0; i < MELT_COLUMNS; i++) {
            if (i) {
                delay += rand() % 3 - 1;
            }
            if (delay > 0) {
                delay = 0;
            }
            if (delay < -15) {
                delay = -15;
            }
            int left = region.x + i * region.width / MELT_COLUMNS;
            int right = region.x + (i + 1) * region.width / MELT_COLUMNS;
            columns[column_count++] = (melt_column_t){left, region.y,
                                                      right - left, region.height, delay};
        }
    }
    const xcb_query_extension_reply_t *extension = xcb_get_extension_data(conn, &xcb_shape_id);
    shape_available = extension && extension->present;
    reveal = reveal_desktop;
    active = true;
    started = ev_now(main_loop);
    ticks_drawn = 0;
    ev_timer_init(&timer, tick, 1.0 / 35.0, 1.0 / 35.0);
    ev_timer_start(main_loop, &timer);
    return true;
}

void melt_draw(cairo_t *ctx) {
    if (!active) {
        return;
    }
    /* Compose strips in memory and upload one frame. Drawing each strip
     * directly on XCB would upload the full snapshot repeatedly. */
    cairo_t *frame_ctx = cairo_create(falling_frame);
    cairo_set_operator(frame_ctx, CAIRO_OPERATOR_CLEAR);
    cairo_paint(frame_ctx);
    cairo_set_operator(frame_ctx, CAIRO_OPERATOR_OVER);
    if (reveal) {
        /* With SHAPE, exposed pixels show the live desktop. If the extension
         * is unavailable, stay opaque and melt to black before unlocking. */
        cairo_set_source_rgb(frame_ctx, 0, 0, 0);
        cairo_paint(frame_ctx);
    }
    for (int i = 0; i < column_count; i++) {
        melt_column_t *column = &columns[i];
        int drop = column->position > 0 ? column->position * column->height / MELT_HEIGHT : 0;
        if (drop >= column->height || column->width == 0) {
            continue;
        }
        cairo_save(frame_ctx);
        cairo_rectangle(frame_ctx, column->x, column->y + drop, column->width, column->height - drop);
        cairo_clip(frame_ctx);
        cairo_set_source_surface(frame_ctx, snapshot, 0, drop);
        cairo_pattern_set_filter(cairo_get_source(frame_ctx), CAIRO_FILTER_NEAREST);
        cairo_paint(frame_ctx);
        cairo_restore(frame_ctx);
    }
    cairo_destroy(frame_ctx);
    cairo_set_source_surface(ctx, falling_frame, 0, 0);
    cairo_paint(ctx);
}

void melt_shape(void) {
    /* This is the only path which uncovers the desktop. PAM success sets
     * authenticated before the death animation can start this transition. */
    if (!active || !reveal || !shape_available || !doom_is_authenticated()) {
        return;
    }
    int count = 0;
    for (int i = 0; i < column_count; i++) {
        melt_column_t *column = &columns[i];
        int drop = column->position > 0 ? column->position * column->height / MELT_HEIGHT : 0;
        if (drop >= column->height || column->width == 0) {
            continue;
        }
        rectangles[count++] = (xcb_rectangle_t){column->x, column->y + drop,
                                                column->width, column->height - drop};
    }
    xcb_shape_rectangles(conn, XCB_SHAPE_SO_SET, XCB_SHAPE_SK_BOUNDING,
                         XCB_CLIP_ORDERING_UNSORTED, win, 0, 0, count, rectangles);
}
