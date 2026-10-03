/* Test only: inspect the lock's bounding shape and whether grabs remain held. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <xcb/xcb.h>
#include <xcb/shape.h>

int main(int argc, char **argv) {
    if (argc != 2) return 2;
    xcb_connection_t *connection = xcb_connect(NULL, NULL);
    if (xcb_connection_has_error(connection)) return 2;
    xcb_screen_t *screen = xcb_setup_roots_iterator(xcb_get_setup(connection)).data;
    if (strcmp(argv[1], "--desktop") == 0) {
        /* A real window works as a desktop fixture with or without Picom.
         * Root background colors alone are not rendered by every compositor. */
        xcb_window_t desktop = xcb_generate_id(connection);
        uint32_t color = 0x245780;
        xcb_create_window(connection, XCB_COPY_FROM_PARENT, desktop, screen->root,
            0, 0, screen->width_in_pixels, screen->height_in_pixels, 0,
            XCB_WINDOW_CLASS_INPUT_OUTPUT, XCB_COPY_FROM_PARENT, XCB_CW_BACK_PIXEL, &color);
        xcb_map_window(connection, desktop);
        xcb_flush(connection);
        printf("%u\n", desktop);
        fflush(stdout);
        char line[32];
        while (fgets(line, sizeof(line), stdin)) {
            color = strtoul(line, NULL, 16);
            xcb_change_window_attributes(connection, desktop, XCB_CW_BACK_PIXEL, &color);
            xcb_clear_area(connection, 0, desktop, 0, 0, 0, 0);
            xcb_flush(connection);
        }
        xcb_disconnect(connection);
        return 0;
    }
    xcb_window_t window = strtoul(argv[1], NULL, 0);
    xcb_shape_get_rectangles_reply_t *shape = xcb_shape_get_rectangles_reply(connection,
        xcb_shape_get_rectangles(connection, window, XCB_SHAPE_SK_BOUNDING), NULL);
    if (!shape) return 2;
    int count = xcb_shape_get_rectangles_rectangles_length(shape);
    xcb_rectangle_t *rectangles = xcb_shape_get_rectangles_rectangles(shape);
    unsigned long area = 0;
    for (int i = 0; i < count; i++) area += (unsigned long)rectangles[i].width * rectangles[i].height;
    xcb_grab_keyboard_reply_t *keyboard = xcb_grab_keyboard_reply(connection,
        xcb_grab_keyboard(connection, 0, screen->root, XCB_CURRENT_TIME,
                          XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC), NULL);
    xcb_grab_pointer_reply_t *pointer = xcb_grab_pointer_reply(connection,
        xcb_grab_pointer(connection, 0, screen->root, 0, XCB_GRAB_MODE_ASYNC,
                         XCB_GRAB_MODE_ASYNC, XCB_NONE, XCB_NONE, XCB_CURRENT_TIME), NULL);
    if (!keyboard || !pointer) return 2;
    printf("%d %lu %u %u\n", count, area, keyboard->status, pointer->status);
    free(shape);
    free(keyboard);
    free(pointer);
    xcb_disconnect(connection);
    return 0;
}
