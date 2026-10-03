#ifndef DOOM_H
#define DOOM_H
#include <stdbool.h>
#include <cairo.h>
#include <ev.h>

typedef struct {
    const char *status;
    const char *fingerprint_notice;
    const char *modifiers;
} doom_ui_t;

/* One scene per process. The scene owns its Cairo surfaces and animation timers.
 * UI text is borrowed only during draw; authentication remains outside it. */
bool doom_init(void);
void doom_close(void);
bool doom_enabled(void);
void doom_start(struct ev_loop *loop);
void doom_shot(void);
void doom_verifying(void);
void doom_denied(void);
bool doom_failure_visible(void);
void doom_authenticated(bool fingerprint);
bool doom_is_authenticated(void);
void doom_draw(cairo_t *ctx, int x, int y, int width, int height, const doom_ui_t *ui);
#endif
