/* Check the Xinerama fallback against a real isolated X server. */
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <xcb/xcb.h>
#include "randr.h"

xcb_connection_t *conn;
bool debug_mode;

static void check_monitors(void) {
    assert(xr_screens == 2);
    assert(xr_resolutions[0].x == 0 && xr_resolutions[0].y == 0);
    assert(xr_resolutions[0].width == 1280 && xr_resolutions[0].height == 1024);
    assert(xr_resolutions[1].x == 0 && xr_resolutions[1].y == 0);
    assert(xr_resolutions[1].width == 640 && xr_resolutions[1].height == 480);
}

int main(void) {
    conn = xcb_connect(NULL, NULL);
    assert(!xcb_connection_has_error(conn));
    xcb_screen_t *screen = xcb_setup_roots_iterator(xcb_get_setup(conn)).data;
    randr_init(NULL, screen->root);
    assert(xr_screens == 0);
    randr_query(screen->root);
    check_monitors();
    /* A query must use the new reply count even when the previous count is
     * larger, rather than reading/writing beyond the two-screen allocation. */
    xr_screens = 3;
    randr_query(screen->root);
    check_monitors();
    free(xr_resolutions);
    xcb_disconnect(conn);
    puts("PASS: Xinerama initializes every monitor and uses the current reply count");
    return 0;
}
