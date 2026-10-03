#ifndef DOOM_HUD_H
#define DOOM_HUD_H
#include <cairo.h>

typedef enum { HUD_RED,
               HUD_GOLD,
               HUD_GREEN } hud_color_t;

/* Optional WAD/PNG UI graphics. Owns glyphs and background until close.
 * Missing graphics retain a procedural panel and system-font fallback. */
void hud_init(const char *folder);
void hud_close(void);
void hud_text(cairo_t *ctx, const char *text, double center, double baseline,
              double size, double max_width, hud_color_t color);
/* Draws a stable, decorative rune per character and returns its fitted width.
 * Uses only the count; these glyphs never encode password characters. */
double hud_password(cairo_t *ctx, unsigned characters, double center,
                    double baseline, double size, double max_width);
void hud_panel(cairo_t *ctx, double x, double y, double width, double height);
#endif
