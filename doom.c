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
#include "assets.h"
#include "hud.h"
#include "unlock_indicator.h"

#define MAX_FRAMES 10
#define BFG_TRAVEL_TICKS 6
#define BFG_BLAST_TICKS 4
#define BFG_SETTLE_TICKS 2
#define SCENE_FRAME_SECONDS (1.0 / 30.0)
typedef struct {
    const char *id, *name;
    int count, walk_count;
    bool floating;
    int origin_x, origin_y;
    cairo_surface_t *idle, *pain, *walk[6], *death[MAX_FRAMES];
} monster_t;

/* Keep catalogue edits within the fixed frame arrays at compile time. */
#define MONSTER(id, label, prefix, death, walk, pain, floating)               \
    _Static_assert(sizeof(death) > 2 && sizeof(death) - 1 <= MAX_FRAMES &&    \
                       sizeof(walk) > 1 && sizeof(walk) - 1 <= 6 &&           \
                       sizeof(pain) == 2 && sizeof(prefix) == 5 &&            \
                       sizeof(death) + sizeof(walk) + sizeof(pain) - 2 <= 20, \
                   "Invalid monster catalogue frames: " id);
#include "monsters.def"
#undef MONSTER

static monster_t monsters[] = {
#define MONSTER(id, label, prefix, death, walk, pain, floating) {id, label, sizeof(death) - 1, sizeof(walk) - 1, floating},
#include "monsters.def"
#undef MONSTER
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
static ev_timer scene_animation;
static double scene_updated;

bool doom_init(void) {
    const char *folder = getenv("DOOM_LOCK_ASSETS");
    const char *wad_path = getenv("DOOM_LOCK_WAD");
    if ((!folder || !folder[0]) && (!wad_path || !wad_path[0])) {
        return false;
    }
    if (!assets_open(wad_path)) {
        return false;
    }
    bool loaded = false;
    char path[64];
    player_dead = assets_image(folder, "player-dead.png");
    if (!player_dead) {
        goto done;
    }
    for (int i = 0; i < 4; i++) {
        snprintf(path, sizeof(path), "bfg-%02d.png", i);
        bfg_blast[i] = assets_image(folder, path);
        if (!bfg_blast[i]) {
            goto done;
        }
    }
    for (int i = 0; i < 2; i++) {
        snprintf(path, sizeof(path), "bfg-projectile-%02d.png", i);
        bfg_projectile[i] = assets_image(folder, path);
        if (!bfg_projectile[i]) {
            goto done;
        }
    }
    for (int i = 0; i < monster_count; i++) {
        if (!assets_origin(folder, monsters[i].id, &monsters[i].origin_x, &monsters[i].origin_y)) {
            goto done;
        }
        snprintf(path, sizeof(path), "%s/idle.png", monsters[i].id);
        monsters[i].idle = assets_image(folder, path);
        if (!monsters[i].idle) {
            goto done;
        }
        snprintf(path, sizeof(path), "%s/pain.png", monsters[i].id);
        monsters[i].pain = assets_image(folder, path);
        if (!monsters[i].pain) {
            goto done;
        }
        for (int j = 0; j < monsters[i].walk_count; j++) {
            snprintf(path, sizeof(path), "%s/walk-%02d.png", monsters[i].id, j);
            monsters[i].walk[j] = assets_image(folder, path);
            if (!monsters[i].walk[j]) {
                goto done;
            }
        }
        for (int j = 0; j < monsters[i].count; j++) {
            snprintf(path, sizeof(path), "%s/death-%02d.png", monsters[i].id, j);
            monsters[i].death[j] = assets_image(folder, path);
            if (!monsters[i].death[j]) {
                goto done;
            }
        }
    }
    hud_init(folder);
    if (!level_init(folder)) {
        goto done;
    }
    current = rand() % monster_count;
    enabled = true;
    loaded = true;
done:
    /* Cairo surfaces own the decoded pixels; no WAD I/O occurs while locked. */
    assets_close();
    if (!loaded) {
        doom_close();
    }
    return loaded;
}

void doom_close(void) {
    if (enabled) {
        ev_timer_stop(EV_DEFAULT, &animation);
        ev_timer_stop(EV_DEFAULT, &scene_animation);
    }
    for (int i = 0; i < monster_count; i++) {
        monster_t *monster = &monsters[i];
        cairo_surface_destroy(monster->idle);
        cairo_surface_destroy(monster->pain);
        monster->idle = monster->pain = NULL;
        for (int j = 0; j < monster->walk_count; j++) {
            cairo_surface_destroy(monster->walk[j]);
            monster->walk[j] = NULL;
        }
        for (int j = 0; j < monster->count; j++) {
            cairo_surface_destroy(monster->death[j]);
            monster->death[j] = NULL;
        }
    }
    cairo_surface_destroy(player_dead);
    player_dead = NULL;
    for (int i = 0; i < 4; i++) {
        cairo_surface_destroy(bfg_blast[i]);
        bfg_blast[i] = NULL;
    }
    for (int i = 0; i < 2; i++) {
        cairo_surface_destroy(bfg_projectile[i]);
        bfg_projectile[i] = NULL;
    }
    bfg_target = NULL; /* Borrowed from the monster's walk frames. */
    hud_close();
    level_close();
    enabled = false;
}

bool doom_enabled(void) {
    return enabled;
}
bool doom_is_authenticated(void) {
    return authenticated;
}

static void choose_monster(void) {
    current = (current + 1 + rand() % (monster_count - 1)) % monster_count;
    frame = -1;
    corpse_ticks = 0;
}

bool doom_failure_visible(void) {
    return failure;
}
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
    if (!enabled || authenticated) {
        return;
    }
    doom_verifying();
    hit_until = ev_time() + 0.23;
    flash_until = ev_time() + 0.12;
}

void doom_authenticated(bool fingerprint) {
    melt_cancel();
    level_actor_focus();
    if (frame >= monsters[current].count - 1) {
        choose_monster();
    }
    if (frame < 0) {
        frame = 0;
    }
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
                if (!(row[x] >> 24)) {
                    continue;
                }
                if (x < left) {
                    left = x;
                }
                if (x > right) {
                    right = x;
                }
                if (y < upper) {
                    upper = y;
                }
                if (y > lower) {
                    lower = y;
                }
            }
        }
        bfg_target_x = (left + right + 1) / 2.0;
        bfg_target_y = (upper + lower + 1) / 2.0;
        bfg_tick = 0;
    }
}

static void tick(EV_P_ ev_timer *watcher, int events) {
    if (failure) {
        return;
    }
    walk_tick++;
    /* The camera renders independently of Doom's slower sprite frames. */
    if (!authenticated) {
        return;
    }
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
                    if (melt_begin(true)) {
                        ev_timer_stop(loop, watcher);
                    } else {
                        ev_break(loop, EVBREAK_ALL);
                    }
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
        } else if (++corpse_ticks >= 2) {
            if (melt_begin(true)) {
                ev_timer_stop(loop, watcher);
            } else {
                ev_break(loop, EVBREAK_ALL);
            }
            return;
        }
    }
    redraw_screen();
}

static void scene_tick(EV_P_ ev_timer *watcher, int events) {
    double now = ev_now(loop);
    double elapsed = now - scene_updated;
    scene_updated = now;
    if (failure || authenticated) {
        return;
    }
    level_tick(elapsed);
    redraw_screen();
}

void doom_start(struct ev_loop *loop) {
    if (!enabled) {
        return;
    }
    scene_updated = ev_now(loop);
    ev_timer_init(&scene_animation, scene_tick, SCENE_FRAME_SECONDS, SCENE_FRAME_SECONDS);
    ev_timer_start(loop, &scene_animation);
    ev_timer_init(&animation, tick, 0.14, 0.14);
    ev_timer_start(loop, &animation);
}

static void centered_text(cairo_t *ctx, const char *text, double center, double y,
                          double size, hud_color_t color) {
    double x1, y1, x2, y2;
    cairo_clip_extents(ctx, &x1, &y1, &x2, &y2);
    hud_text(ctx, text, center, y, size, fmax(1, x2 - x1 - 64), color);
}

void doom_draw(cairo_t *ctx, int x, int y, int width, int height, const doom_ui_t *ui) {
    monster_t *monster = &monsters[current];
    level_actor_t actor = level_actor_projection(width, height);
    double scale_x = actor.scale_x, scale_y = actor.scale_y;
    bool floating = monster->floating;
    double lift = floating ? 16 : 0;
    if (authenticated && !(bfg_kill && bfg_tick < BFG_TRAVEL_TICKS)) {
        lift *= 1 - (double)frame / (monster->count - 1);
    }
    double left = x + actor.x - monster->origin_x * scale_x;
    double top = y + actor.floor_y - (monster->origin_y + lift) * scale_y;
    double center = x + width / 2.0;
    cairo_save(ctx);
    cairo_rectangle(ctx, x, y, width, height);
    cairo_clip(ctx);
    cairo_select_font_face(ctx, "monospace", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
    if (failure) {
        cairo_set_source_rgb(ctx, 0.18, 0.015, 0.01);
        cairo_rectangle(ctx, x, y, width, height);
        cairo_fill(ctx);
        double middle = y + height / 2.0;
        centered_text(ctx, "YOU DIED", center, middle - 130, width < 700 ? 40 : 64, HUD_RED);
        int face_width = cairo_image_surface_get_width(player_dead);
        int face_height = cairo_image_surface_get_height(player_dead);
        cairo_save(ctx);
        cairo_translate(ctx, center - face_width * 3, middle - face_height * 3);
        cairo_scale(ctx, 6, 6);
        cairo_set_source_surface(ctx, player_dead, 0, 0);
        cairo_pattern_set_filter(cairo_get_source(ctx), CAIRO_FILTER_NEAREST);
        cairo_paint(ctx);
        cairo_restore(ctx);
        centered_text(ctx, "ACCESS DENIED", center, middle + 145, 20, HUD_GOLD);
        centered_text(ctx, "TYPE TO TRY AGAIN / OR SCAN FINGER", center, middle + 185, 14, HUD_GOLD);
        if (ui->fingerprint_notice[0]) {
            centered_text(ctx, ui->fingerprint_notice, center, middle + 220, 13, HUD_GOLD);
        }
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
    cairo_surface_t *sprite = authenticated           ? monster->death[frame]
                              : ev_time() < hit_until ? monster->pain
                                                      : monster->walk[(walk_tick / 2) % monster->walk_count];
    if (bfg_kill && impact_frame < 0) {
        sprite = bfg_target;
    } else if (bfg_kill && impact_frame == 0) {
        sprite = monster->pain;
    }
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
    bool password_dialog = !authenticated && (ui->password_entered || ui->password_pending);
    double panel_width = fmin(width - 32, 720);
    double panel_height = password_dialog ? 168 : 116;
    double panel_x = center - panel_width / 2;
    double panel_y = y + height - panel_height - 16;
    hud_panel(ctx, panel_x, panel_y, panel_width, panel_height);
    double text_width = panel_width - 40;
    hud_text(ctx, monster->name, center, panel_y + 28, 14, text_width, HUD_GOLD);
    if (password_dialog) {
        hud_text(ctx, "ENTER PASSWORD", center, panel_y + 53, 21, text_width, HUD_RED);
        /* Fixed mask: the renderer never receives password bytes or length. */
        double field_width = fmin(panel_width - 64, 320);
        cairo_set_source_rgb(ctx, 0.015, 0.01, 0.005);
        cairo_rectangle(ctx, center - field_width / 2, panel_y + 62, field_width, 36);
        cairo_fill(ctx);
        hud_text(ctx, ui->password_pending ? "CHECKING..." : "********", center,
                 panel_y + 87, 21, field_width - 40, HUD_GOLD);
        if (!ui->password_pending && fmod(ev_time(), 1.0) < 0.5) {
            hud_text(ctx, "_", center + field_width / 2 - 28, panel_y + 87, 21, 24, HUD_GOLD);
        }
        hud_text(ctx, ui->password_pending ? "VERIFYING PASSWORD" : "ENTER TO UNLOCK / ESC TO CLEAR",
                 center, panel_y + 121, 14, text_width, HUD_RED);
        hud_text(ctx, ui->fingerprint_notice, center, panel_y + 147, 14, text_width, HUD_GOLD);
    } else {
        const char *status = authenticated
                                 ? (bfg_kill ? "BFG 9000 / ACCESS GRANTED" : "ACCESS GRANTED")
                                 : ui->status;
        hud_text(ctx, status, center, panel_y + 59, 21, text_width, authenticated ? HUD_GREEN : HUD_RED);
        if (!authenticated) {
            hud_text(ctx, ui->fingerprint_notice, center, panel_y + 86, 14, text_width, HUD_GOLD);
        }
    }
    if (ui->modifiers) {
        hud_text(ctx, ui->modifiers, center, panel_y + panel_height - 8, 7, text_width, HUD_GOLD);
    }
    cairo_restore(ctx);
}
