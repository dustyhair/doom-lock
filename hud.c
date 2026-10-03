#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "assets.h"
#include "hud.h"

#define FONT_FIRST 33
#define FONT_LAST 95
#define FONT_COUNT (FONT_LAST - FONT_FIRST + 1)

static cairo_surface_t *glyphs[3][FONT_COUNT], *stone;
static bool bitmap_font;
static uint32_t rune_seed;
/* Original five-by-seven symbols, rather than extracted game artwork. */
static const uint8_t runes[][7] = {
    {4, 10, 17, 31, 17, 10, 4},
    {17, 17, 31, 4, 14, 4, 4},
    {14, 17, 14, 31, 4, 4, 14},
    {4, 14, 21, 31, 21, 14, 4},
    {17, 10, 4, 31, 4, 10, 17},
    {31, 17, 10, 4, 10, 17, 31},
    {4, 31, 4, 14, 21, 4, 4},
    {17, 31, 17, 10, 4, 14, 4},
    {4, 10, 31, 10, 17, 10, 4},
    {14, 4, 31, 21, 4, 21, 14},
    {17, 10, 31, 10, 17, 17, 10},
    {31, 10, 17, 10, 4, 10, 17},
    {4, 21, 31, 21, 14, 4, 14},
    {17, 31, 4, 10, 31, 10, 4},
    {10, 21, 31, 4, 21, 10, 17},
    {14, 17, 31, 17, 10, 4, 14},
};
static const double colors[3][3] = {{1.0, 0.16, 0.10}, {0.95, 0.73, 0.40}, {0.40, 0.90, 0.30}};

void hud_close(void) {
    for (int color = 0; color < 3; color++) {
        for (int i = 0; i < FONT_COUNT; i++) {
            cairo_surface_destroy(glyphs[color][i]);
            glyphs[color][i] = NULL;
        }
    }
    cairo_surface_destroy(stone);
    stone = NULL;
    bitmap_font = false;
}

static cairo_surface_t *tinted(cairo_surface_t *source, hud_color_t color) {
    int width = cairo_image_surface_get_width(source), height = cairo_image_surface_get_height(source);
    cairo_surface_t *result = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, width, height);
    if (cairo_surface_status(result) != CAIRO_STATUS_SUCCESS) {
        cairo_surface_destroy(result);
        return NULL;
    }
    cairo_surface_flush(source);
    unsigned char *src = cairo_image_surface_get_data(source);
    unsigned char *dst = cairo_image_surface_get_data(result);
    int source_stride = cairo_image_surface_get_stride(source), stride = cairo_image_surface_get_stride(result);
    for (int y = 0; y < height; y++) {
        uint32_t *row = (uint32_t *)(src + y * source_stride);
        uint32_t *out = (uint32_t *)(dst + y * stride);
        for (int x = 0; x < width; x++) {
            uint32_t pixel = row[x];
            double light = fmax((pixel >> 16) & 255, fmax((pixel >> 8) & 255, pixel & 255));
            out[x] = (pixel & UINT32_C(0xff000000)) |
                     (uint32_t)(light * colors[color][0]) << 16 |
                     (uint32_t)(light * colors[color][1]) << 8 |
                     (uint32_t)(light * colors[color][2]);
        }
    }
    cairo_surface_mark_dirty(result);
    return result;
}

void hud_init(const char *folder) {
    hud_close();
    /* Visual variation only; independent of both input and the maze's PRNG. */
    rune_seed = (uint32_t)time(NULL) ^ (uint32_t)getpid();
    stone = assets_image(folder, "ui/stone.png");
    bitmap_font = true;
    for (int i = 0; i < FONT_COUNT; i++) {
        char name[64];
        snprintf(name, sizeof(name), "ui/font-%03d.png", FONT_FIRST + i);
        glyphs[HUD_RED][i] = assets_image(folder, name);
        if (!glyphs[HUD_RED][i]) {
            bitmap_font = false;
            continue;
        }
        glyphs[HUD_GOLD][i] = tinted(glyphs[HUD_RED][i], HUD_GOLD);
        glyphs[HUD_GREEN][i] = tinted(glyphs[HUD_RED][i], HUD_GREEN);
        if (!glyphs[HUD_GOLD][i] || !glyphs[HUD_GREEN][i]) {
            bitmap_font = false;
        }
    }
}

static unsigned font_character(unsigned char character) {
    if (character >= 'a' && character <= 'z') {
        character -= 'a' - 'A';
    }
    return character >= FONT_FIRST && character <= FONT_LAST ? character : '?';
}

void hud_text(cairo_t *ctx, const char *text, double center, double baseline,
              double size, double max_width, hud_color_t color) {
    if (!text || !text[0] || size <= 0 || max_width <= 0) {
        return;
    }
    cairo_save(ctx);
    if (!bitmap_font) {
        cairo_select_font_face(ctx, "monospace", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
        cairo_set_font_size(ctx, size);
        cairo_text_extents_t extents;
        cairo_text_extents(ctx, text, &extents);
        if (extents.width > max_width) {
            cairo_set_font_size(ctx, size * max_width / extents.width);
            cairo_text_extents(ctx, text, &extents);
        }
        cairo_set_source_rgb(ctx, colors[color][0], colors[color][1], colors[color][2]);
        cairo_move_to(ctx, center - extents.width / 2 - extents.x_bearing, baseline);
        cairo_show_text(ctx, text);
    } else {
        double advance = 0;
        for (const unsigned char *p = (const unsigned char *)text; *p; p++) {
            advance += *p == ' ' ? 4 : cairo_image_surface_get_width(glyphs[color][font_character(*p) - FONT_FIRST]) + 1;
        }
        /* Original glyphs have a seven-pixel line height. Use integer scaling
         * whenever the viewport permits it, with nearest-neighbor sampling. */
        double scale = fmin(size / 7, max_width / advance);
        if (scale >= 1) {
            scale = floor(scale);
        }
        cairo_translate(ctx, round(center - advance * scale / 2), round(baseline - 7 * scale));
        cairo_scale(ctx, scale, scale);
        double position = 0;
        for (const unsigned char *p = (const unsigned char *)text; *p; p++) {
            if (*p == ' ') {
                position += 4;
                continue;
            }
            cairo_surface_t *glyph = glyphs[color][font_character(*p) - FONT_FIRST];
            cairo_set_source_surface(ctx, glyph, position, 0);
            cairo_pattern_set_filter(cairo_get_source(ctx), CAIRO_FILTER_NEAREST);
            cairo_rectangle(ctx, position, 0, cairo_image_surface_get_width(glyph),
                            cairo_image_surface_get_height(glyph));
            cairo_fill(ctx);
            position += cairo_image_surface_get_width(glyph) + 1;
        }
    }
    cairo_restore(ctx);
}

double hud_password(cairo_t *ctx, unsigned characters, double center,
                    double baseline, double size, double max_width) {
    if (!characters || size <= 0 || max_width <= 0) {
        return 0;
    }
    unsigned visible = characters < 32 ? characters : 32;
    double scale = fmin(size / 7, max_width / (visible * 6));
    if (scale >= 1) {
        scale = floor(scale);
    }
    double width = visible * 6 * scale;
    cairo_save(ctx);
    cairo_set_antialias(ctx, CAIRO_ANTIALIAS_NONE);
    cairo_translate(ctx, round(center - width / 2), round(baseline - 7 * scale));
    cairo_scale(ctx, scale, scale);
    for (unsigned i = 0; i < visible; i++) {
        uint32_t selection = rune_seed + UINT32_C(0x9e3779b9) * (characters - visible + i + 1);
        selection ^= selection >> 16;
        selection *= UINT32_C(0x85ebca6b);
        selection ^= selection >> 13;
        const uint8_t *glyph = runes[selection % (sizeof(runes) / sizeof(runes[0]))];
        /* A scrolling tail keeps long input readable and visibly changing. */
        const uint8_t ellipsis[7] = {0, 0, 0, 0, 0, 21, 0};
        if (characters > visible && i == 0) {
            glyph = ellipsis;
        }
        for (int shade = 0; shade < 2; shade++) {
            for (int y = 0; y < 7; y++) {
                double light = shade ? 1.0 - y * 0.045 : 0.28;
                cairo_set_source_rgb(ctx, colors[HUD_GOLD][0] * light,
                                     colors[HUD_GOLD][1] * light, colors[HUD_GOLD][2] * light);
                for (int x = 0; x < 5; x++) {
                    if (glyph[y] & (1 << (4 - x))) {
                        cairo_rectangle(ctx, i * 6 + x, y + (shade ? 0 : 1), 1, 1);
                    }
                }
                cairo_fill(ctx);
            }
        }
    }
    cairo_restore(ctx);
    return width;
}

void hud_panel(cairo_t *ctx, double x, double y, double width, double height) {
    cairo_save(ctx);
    cairo_set_source_rgba(ctx, 0, 0, 0, 0.65);
    cairo_rectangle(ctx, x + 6, y + 6, width, height);
    cairo_fill(ctx);
    cairo_rectangle(ctx, x, y, width, height);
    if (stone) {
        cairo_set_source_surface(ctx, stone, x, y);
        cairo_pattern_set_extend(cairo_get_source(ctx), CAIRO_EXTEND_REPEAT);
        cairo_pattern_set_filter(cairo_get_source(ctx), CAIRO_FILTER_NEAREST);
    } else {
        cairo_set_source_rgb(ctx, 0.19, 0.18, 0.16);
    }
    cairo_fill(ctx);
    cairo_set_source_rgb(ctx, 0.42, 0.40, 0.34);
    cairo_rectangle(ctx, x, y, width, 3);
    cairo_rectangle(ctx, x, y, 3, height);
    cairo_fill(ctx);
    cairo_set_source_rgb(ctx, 0.055, 0.045, 0.025);
    cairo_rectangle(ctx, x, y + height - 3, width, 3);
    cairo_rectangle(ctx, x + width - 3, y, 3, height);
    cairo_fill(ctx);
    cairo_set_source_rgba(ctx, 0.025, 0.015, 0.01, 0.92);
    cairo_rectangle(ctx, x + 8, y + 8, width - 16, height - 16);
    cairo_fill(ctx);
    cairo_set_source_rgb(ctx, 0.38, 0.035, 0.02);
    cairo_rectangle(ctx, x + 12, y + 12, width - 24, 2);
    cairo_fill(ctx);
    cairo_restore(ctx);
}
