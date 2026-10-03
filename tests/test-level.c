/* Run a long maze tour under address/undefined-behavior sanitizers. */
#include <stdlib.h>
#include <assert.h>
#include <cairo.h>
#include <fontconfig/fontconfig.h>
/* Inspect navigation state without adding a debug API to the locker. */
#include "../level.c"
#include "../doom.c"

auth_state_t auth_state = STATE_AUTH_IDLE;
char *modifier_string;
int input_position;
bool password_verifying;
char fingerprint_status[160];
void redraw_screen(void) {}
bool melt_begin(bool reveal_desktop) { return false; }
void melt_cancel(void) {}

static void check_monsters(cairo_t *ctx, cairo_surface_t *surface, const char *output) {
    int stride = cairo_image_surface_get_stride(surface);
    unsigned char *background = malloc(stride * 1024);
    assert(background);
    for (current = 0; current < monster_count; current++) {
        level_draw(ctx, 0, 0, 1280, 1024);
        cairo_surface_flush(surface);
        memcpy(background, cairo_image_surface_get_data(surface), stride * 1024);
        doom_draw(ctx, 0, 0, 1280, 1024);
        cairo_surface_flush(surface);
        assert(cairo_status(ctx) == CAIRO_STATUS_SUCCESS);
        /* The original floor anchor must put the living body in the level,
         * with visible pixels above the HUD for every monster. */
        assert(memcmp(background, cairo_image_surface_get_data(surface), stride * 900) != 0);
        if (current == 0 || current == 4 || current == 13) {
            char path[4096];
            snprintf(path, sizeof(path), "%s/monster-%s-%s.png", output, monsters[current].id, style->map);
            assert(cairo_surface_write_to_png(surface, path) == CAIRO_STATUS_SUCCESS);
        }
        authenticated = true;
        for (frame = 0; frame < monsters[current].count; frame++) {
            level_draw(ctx, 0, 0, 1280, 1024);
            doom_draw(ctx, 0, 0, 1280, 1024);
            assert(cairo_status(ctx) == CAIRO_STATUS_SUCCESS);
        }
        authenticated = false;
        frame = -1;
    }
    current = 0;
    free(background);
}

int main(int argc, char **argv) {
    if (argc != 5) return 2;
    srand(strtoul(argv[2], NULL, 10));
    setenv("DOOM_LOCK_ASSETS", argv[1], 1);
    if (!doom_init()) return 3;
    cairo_surface_t *surface = cairo_image_surface_create(CAIRO_FORMAT_RGB24, 1280, 1024);
    cairo_t *ctx = cairo_create(surface);
    double traveled = 0;
    for (int i = 0; i < 10000; i++) {
        double previous_x = camera_x, previous_y = camera_y;
        level_tick(0.14);
        double moved_x = camera_x - previous_x, moved_y = camera_y - previous_y;
        double distance = hypot(moved_x, moved_y);
        assert(!solid((int)camera_x, (int)camera_y));
        point_t npc = actor_position();
        assert(!solid((int)npc.x, (int)npc.y));
        assert(hypot(npc.x - camera_x, npc.y - camera_y) <= ACTOR_DISTANCE + 1e-8);
        assert(distance <= WALK_SPEED * 0.14 + 1e-8);
        if (distance > 1e-8) {
            /* Retreat along the viewing axis, never advance toward the NPC. */
            assert(moved_x * cos(angle) + moved_y * sin(angle) < -0.99 * distance);
            traveled += distance;
        }
        if (i == 21) {
            level_draw(ctx, 0, 0, 1280, 1024);
            if (cairo_surface_write_to_png(surface, argv[3]) != CAIRO_STATUS_SUCCESS) return 5;
            check_monsters(ctx, surface, argv[4]);
        }
        if (i % 100 == 0) {
            /* Exercise different viewport shapes as the tour visits rooms,
             * dead ends, corners, and shortcuts over 23 simulated minutes. */
            level_draw(ctx, 0, 0, i % 200 ? 1280 : 1024, 768);
            doom_draw(ctx, 0, 0, i % 200 ? 1280 : 1024, 768);
            level_actor_t actor = level_actor_projection(i % 200 ? 1280 : 1024, 768);
            if (actor.visible) {
                assert(actor.scale_x > 0 && actor.scale_y > 0);
                cairo_save(ctx);
                level_actor_clip(ctx, 0, 0, i % 200 ? 1280 : 1024, 768, actor.depth);
                cairo_restore(ctx);
            }
            double saved_angle = angle;
            level_actor_focus();
            actor = level_actor_projection(1280, 1024);
            assert(actor.visible && actor.depth >= 0.5);
            npc = actor_position();
            double nx = npc.x - camera_x, ny = npc.y - camera_y, length = hypot(nx, ny);
            assert(cast(nx / length, ny / length).distance >= length - 1e-8);
            actor_focused = false;
            angle = saved_angle;
            frame_number++;
            if (cairo_status(ctx) != CAIRO_STATUS_SUCCESS) return 4;
        }
    }
    assert(traveled > 200);
    puts(style->map);
    cairo_destroy(ctx);
    cairo_surface_destroy(surface);
    for (int i = 0; i < monster_count; i++) {
        cairo_surface_destroy(monsters[i].idle);
        cairo_surface_destroy(monsters[i].pain);
        for (int j = 0; j < monsters[i].walk_count; j++) cairo_surface_destroy(monsters[i].walk[j]);
        for (int j = 0; j < monsters[i].count; j++) cairo_surface_destroy(monsters[i].death[j]);
    }
    cairo_surface_destroy(player_dead);
    for (int i = 0; i < 4; i++) cairo_surface_destroy(bfg_blast[i]);
    for (int i = 0; i < 2; i++) cairo_surface_destroy(bfg_projectile[i]);
    cairo_surface_destroy(wall_texture.surface);
    cairo_surface_destroy(detail_texture.surface);
    cairo_surface_destroy(floor_texture.surface);
    cairo_surface_destroy(ceiling_texture.surface);
    if (style->slime_room) for (int i = 0; i < 3; i++) cairo_surface_destroy(slime[i].surface);
    cairo_surface_destroy(view);
    /* Release process-wide font caches so LeakSanitizer checks our allocations. */
    cairo_debug_reset_static_data();
    FcFini();
    return 0;
}
