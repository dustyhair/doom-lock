#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>
#include <cairo.h>
#include <ev.h>
#include "doom.h"
#include "melt.h"
#include "level.h"
#include "unlock_indicator.h"

#define MAX_FRAMES 10
#define BFG_TRAVEL_TICKS 6
#define BFG_BLAST_TICKS 4
#define BFG_SETTLE_TICKS 2
typedef struct {
    const char *id, *name;
    int count, walk_count;
    int origin_x, origin_y;
    cairo_surface_t *idle, *pain, *walk[6], *death[MAX_FRAMES];
} monster_t;

static monster_t monsters[] = {
    {"zombieman", "ZOMBIEMAN", 5, 4},
    {"shotgun-guy", "SHOTGUN GUY", 5, 4},
    {"imp", "IMP", 5, 4},
    {"demon", "DEMON", 6, 4},
    {"cacodemon", "CACODEMON", 6, 1},
    {"baron", "BARON OF HELL", 7, 4},
    {"hell-knight", "HELL KNIGHT", 7, 4},
    {"revenant", "REVENANT", 6, 6},
    {"mancubus", "MANCUBUS", 10, 6},
    {"chaingunner", "CHAINGUNNER", 7, 4},
    {"arachnotron", "ARACHNOTRON", 7, 6},
    {"lost-soul", "LOST SOUL", 6, 2},
    {"pain-elemental", "PAIN ELEMENTAL", 6, 3},
    {"cyberdemon", "CYBERDEMON", 9, 4},
};
static const int monster_count = sizeof(monsters) / sizeof(monsters[0]);
static bool enabled, authenticated, failure;
static double hit_until, flash_until;
static unsigned walk_tick;
static cairo_surface_t *player_dead;
static cairo_surface_t *bfg_blast[4];
static cairo_surface_t *bfg_projectile[2], *bfg_target;
static double bfg_target_x, bfg_target_y;
static bool bfg_kill;
static int bfg_tick;
static int current, frame = -1, corpse_ticks;
static ev_timer animation;
extern auth_state_t auth_state;
extern char *modifier_string;
extern int input_position;
extern bool password_verifying;
extern char fingerprint_status[160];

bool doom_init(void) {
    const char *folder = getenv("DOOM_LOCK_ASSETS");
    if (!folder) return false;
    char path[4096];
    snprintf(path, sizeof(path), "%s/player-dead.png", folder);
    player_dead = cairo_image_surface_create_from_png(path);
    if (cairo_surface_status(player_dead) != CAIRO_STATUS_SUCCESS) return false;
    for (int i = 0; i < 4; i++) {
        snprintf(path, sizeof(path), "%s/bfg-%02d.png", folder, i);
        bfg_blast[i] = cairo_image_surface_create_from_png(path);
        if (cairo_surface_status(bfg_blast[i]) != CAIRO_STATUS_SUCCESS) return false;
    }
    for (int i = 0; i < 2; i++) {
        snprintf(path, sizeof(path), "%s/bfg-projectile-%02d.png", folder, i);
        bfg_projectile[i] = cairo_image_surface_create_from_png(path);
        if (cairo_surface_status(bfg_projectile[i]) != CAIRO_STATUS_SUCCESS) return false;
    }
    for (int i = 0; i < monster_count; i++) {
        snprintf(path, sizeof(path), "%s/%s/origin.txt", folder, monsters[i].id);
        FILE *origin = fopen(path, "r");
        if (!origin) return false;
        int fields = fscanf(origin, "%d %d", &monsters[i].origin_x, &monsters[i].origin_y);
        fclose(origin);
        if (fields != 2 || monsters[i].origin_x < 0 || monsters[i].origin_x > 256 ||
            monsters[i].origin_y < 0 || monsters[i].origin_y > 192) return false;
        snprintf(path, sizeof(path), "%s/%s/idle.png", folder, monsters[i].id);
        monsters[i].idle = cairo_image_surface_create_from_png(path);
        if (cairo_surface_status(monsters[i].idle) != CAIRO_STATUS_SUCCESS) return false;
        snprintf(path, sizeof(path), "%s/%s/pain.png", folder, monsters[i].id);
        monsters[i].pain = cairo_image_surface_create_from_png(path);
        if (cairo_surface_status(monsters[i].pain) != CAIRO_STATUS_SUCCESS) return false;
        for (int j = 0; j < monsters[i].walk_count; j++) {
            snprintf(path, sizeof(path), "%s/%s/walk-%02d.png", folder, monsters[i].id, j);
            monsters[i].walk[j] = cairo_image_surface_create_from_png(path);
            if (cairo_surface_status(monsters[i].walk[j]) != CAIRO_STATUS_SUCCESS) return false;
        }
        for (int j = 0; j < monsters[i].count; j++) {
            snprintf(path, sizeof(path), "%s/%s/death-%02d.png", folder, monsters[i].id, j);
            monsters[i].death[j] = cairo_image_surface_create_from_png(path);
            if (cairo_surface_status(monsters[i].death[j]) != CAIRO_STATUS_SUCCESS) return false;
        }
    }
    if (!level_init(folder)) return false;
    current = rand() % monster_count;
    enabled = true;
    return true;
}

bool doom_enabled(void) { return enabled; }
bool doom_is_authenticated(void) { return authenticated; }

static void choose_monster(void) {
    current = (current + 1 + rand() % (monster_count - 1)) % monster_count;
    frame = -1;
    corpse_ticks = 0;
}

bool doom_failure_visible(void) { return failure; }
void doom_denied(void) {
    melt_begin(false);
    failure = true;
}
void doom_verifying(void) {
    if (failure) {
        melt_cancel();
        choose_monster();
    }
    failure = false;
}
void doom_shot(void) {
    if (!enabled || authenticated) return;
    doom_verifying();
    hit_until = ev_time() + 0.23;
    flash_until = ev_time() + 0.12;
}

void doom_authenticated(bool fingerprint) {
    melt_cancel();
    level_actor_focus();
    if (frame >= monsters[current].count - 1) choose_monster();
    if (frame < 0) frame = 0;
    authenticated = true;
    failure = false;
    corpse_ticks = 0;
    bfg_kill = fingerprint;
    if (fingerprint) {
        /* Aim at the visible body, excluding transparent canvas around it. */
        monster_t *monster = &monsters[current];
        bfg_target = monster->walk[(walk_tick / 2) % monster->walk_count];
        int w = cairo_image_surface_get_width(bfg_target);
        int h = cairo_image_surface_get_height(bfg_target);
        int stride = cairo_image_surface_get_stride(bfg_target);
        unsigned char *data = cairo_image_surface_get_data(bfg_target);
        int left = w, right = 0, upper = h, lower = 0;
        for (int y = 0; y < h; y++) {
            uint32_t *row = (uint32_t *)(data + y * stride);
            for (int x = 0; x < w; x++) {
                if (!(row[x] >> 24)) continue;
                if (x < left) left = x;
                if (x > right) right = x;
                if (y < upper) upper = y;
                if (y > lower) lower = y;
            }
        }
        bfg_target_x = (left + right + 1) / 2.0;
        bfg_target_y = (upper + lower + 1) / 2.0;
        bfg_tick = 0;
    }
}

static void tick(EV_P_ ev_timer *watcher, int events) {
    if (failure) return;
    if (!authenticated) level_tick(0.14);
    walk_tick++;
    if (bfg_kill) {
        bfg_tick++;
        if (bfg_tick == BFG_TRAVEL_TICKS) {
            frame = 0;
        } else if (bfg_tick > BFG_TRAVEL_TICKS) {
            frame += 2;
            if (frame >= monsters[current].count - 1) {
                frame = monsters[current].count - 1;
                /* Render the clear scene before the melt captures it, even
                 * when a monster has a short death animation. */
                if (++corpse_ticks >= 3 &&
                    bfg_tick >= BFG_TRAVEL_TICKS + BFG_BLAST_TICKS + BFG_SETTLE_TICKS) {
                    if (melt_begin(true)) ev_timer_stop(loop, watcher);
                    else ev_break(loop, EVBREAK_ALL);
                    return;
                }
            }
        }
        redraw_screen();
        return;
    }
    if (frame >= 0) {
        if (frame < monsters[current].count - 1) {
            frame++;
        } else if (++corpse_ticks >= (authenticated ? 2 : 7)) {
            if (authenticated) {
                if (melt_begin(true)) ev_timer_stop(loop, watcher);
                else ev_break(loop, EVBREAK_ALL);
                return;
            }
            choose_monster();
        }
    }
    redraw_screen();
}

void doom_start(struct ev_loop *loop) {
    if (!enabled) return;
    ev_timer_init(&animation, tick, 0.14, 0.14);
    ev_timer_start(loop, &animation);
}

static void centered_text(cairo_t *ctx, const char *text, double center, double y,
                          double size, double r, double g, double b) {
    cairo_text_extents_t extents;
    cairo_set_font_size(ctx, size);
    cairo_text_extents(ctx, text, &extents);
    cairo_set_source_rgb(ctx, r, g, b);
    cairo_move_to(ctx, center - extents.width / 2 - extents.x_bearing, y);
    cairo_show_text(ctx, text);
}

void doom_draw(cairo_t *ctx, int x, int y, int width, int height) {
    monster_t *monster = &monsters[current];
    level_actor_t actor = level_actor_projection(width, height);
    double scale_x = actor.scale_x, scale_y = actor.scale_y;
    bool floating = current == 4 || current == 11 || current == 12;
    double lift = floating ? 16 : 0;
    if (authenticated && !(bfg_kill && bfg_tick < BFG_TRAVEL_TICKS))
        lift *= 1 - (double)frame / (monster->count - 1);
    double left = x + actor.x - monster->origin_x * scale_x;
    double top = y + actor.floor_y - (monster->origin_y + lift) * scale_y;
    double center = x + width / 2.0;
    cairo_save(ctx);
    cairo_select_font_face(ctx, "monospace", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
    if (failure) {
        cairo_set_source_rgb(ctx, 0.18, 0.015, 0.01);
        cairo_rectangle(ctx, x, y, width, height);
        cairo_fill(ctx);
        double middle = y + height / 2.0;
        centered_text(ctx, "YOU DIED", center, middle - 130, width < 700 ? 40 : 64, 0.85, 0.08, 0.04);
        int face_width = cairo_image_surface_get_width(player_dead);
        int face_height = cairo_image_surface_get_height(player_dead);
        cairo_save(ctx);
        cairo_translate(ctx, center - face_width * 3, middle - face_height * 3);
        cairo_scale(ctx, 6, 6);
        cairo_set_source_surface(ctx, player_dead, 0, 0);
        cairo_pattern_set_filter(cairo_get_source(ctx), CAIRO_FILTER_NEAREST);
        cairo_paint(ctx);
        cairo_restore(ctx);
        centered_text(ctx, "ACCESS DENIED", center, middle + 145, 20, 0.8, 0.6, 0.5);
        centered_text(ctx, "TYPE TO TRY AGAIN / OR SCAN FINGER", center, middle + 185, 14, 0.65, 0.45, 0.35);
        if (fingerprint_status[0])
            centered_text(ctx, fingerprint_status, center, middle + 220, 13, 0.7, 0.6, 0.5);
        cairo_restore(ctx);
        return;
    }
    if (!authenticated && ev_time() < flash_until) {
        cairo_set_source_rgba(ctx, 0.9, 0.06, 0.02, 0.25);
        cairo_rectangle(ctx, x, y, width, height);
        cairo_fill(ctx);
    }
    int impact_frame = bfg_tick - BFG_TRAVEL_TICKS;
    if (bfg_kill && impact_frame >= 0 && impact_frame < BFG_BLAST_TICKS) {
        double opacity = 0.78 * (1 - (double)impact_frame / BFG_BLAST_TICKS);
        cairo_set_source_rgba(ctx, 0.03, 0.9, 0.035, opacity);
        cairo_rectangle(ctx, x, y, width, height);
        cairo_fill(ctx);
    }
    double bob = !authenticated && floating ? sin(walk_tick * 0.4) * 2 * scale_y : 0;
    cairo_surface_t *sprite = authenticated ? monster->death[frame]
        : ev_time() < hit_until ? monster->pain
        : monster->walk[(walk_tick / 2) % monster->walk_count];
    if (bfg_kill && impact_frame < 0) sprite = bfg_target;
    else if (bfg_kill && impact_frame == 0) sprite = monster->pain;
    if (actor.visible) {
        cairo_save(ctx);
        level_actor_clip(ctx, x, y, width, height, actor.depth);
        cairo_translate(ctx, left, top + bob);
        cairo_scale(ctx, scale_x, scale_y);
        cairo_set_source_surface(ctx, sprite, 0, 0);
        cairo_pattern_set_filter(cairo_get_source(ctx), CAIRO_FILTER_NEAREST);
        cairo_paint(ctx);
        cairo_restore(ctx);
    }
    if (bfg_kill && impact_frame < BFG_BLAST_TICKS && actor.visible) {
        double target_x = left + bfg_target_x * scale_x;
        double target_y = top + bfg_target_y * scale_y;
        cairo_surface_t *effect;
        double effect_x, effect_y;
        if (impact_frame < 0) {
            /* The projectile reaches the living monster before the impact. */
            double progress = (double)bfg_tick / (BFG_TRAVEL_TICKS - 1);
            double origin_x = x + width * 0.20;
            double origin_y = y + height - 90;
            effect_x = origin_x + (target_x - origin_x) * progress;
            effect_y = origin_y + (target_y - origin_y) * progress;
            effect = bfg_projectile[bfg_tick % 2];
        } else {
            effect_x = target_x;
            effect_y = target_y;
            effect = bfg_blast[impact_frame];
        }
        int effect_width = cairo_image_surface_get_width(effect);
        int effect_height = cairo_image_surface_get_height(effect);
        cairo_save(ctx);
        cairo_translate(ctx, effect_x - effect_width * scale_x / 2,
                        effect_y - effect_height * scale_y / 2);
        cairo_scale(ctx, scale_x, scale_y);
        cairo_set_source_surface(ctx, effect, 0, 0);
        cairo_pattern_set_filter(cairo_get_source(ctx), CAIRO_FILTER_NEAREST);
        cairo_paint(ctx);
        cairo_restore(ctx);
    }
    cairo_set_source_rgba(ctx, 0.02, 0.025, 0.03, 0.88);
    cairo_rectangle(ctx, x, y + height - 100, width, 100);
    cairo_fill(ctx);
    centered_text(ctx, monster->name, center, y + height - 80, 14, 0.85, 0.65, 0.38);
    const char *status = "SCAN FINGER OR TYPE PASSWORD + ENTER";
    if (authenticated) status = bfg_kill ? "BFG 9000 / ACCESS GRANTED" : "ACCESS GRANTED";
    else if (auth_state == STATE_AUTH_VERIFY)
        status = password_verifying ? "VERIFYING PASSWORD" : "VERIFYING / SCAN FINGER";
    else if (auth_state == STATE_AUTH_WRONG) status = "ACCESS DENIED / TRY AGAIN";
    else if (auth_state == STATE_AUTH_LOCK) status = "LOCKING";
    else if (auth_state == STATE_I3LOCK_LOCK_FAILED) status = "COULD NOT LOCK";
    if (!authenticated && !failure && input_position > 0)
        status = "PASSWORD ENTERED / PRESS ENTER";
    centered_text(ctx, status, center, y + height - 55, 15,
                  authenticated ? 0.35 : 0.80, authenticated ? 0.90 : 0.85, 0.70);
    if (!authenticated && fingerprint_status[0])
        centered_text(ctx, fingerprint_status, center, y + height - 30, 13, 0.7, 0.8, 0.7);
    if (modifier_string)
        centered_text(ctx, modifier_string, center, y + height - 10, 11, 0.9, 0.55, 0.25);
    cairo_restore(ctx);
}
