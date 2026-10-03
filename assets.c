#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <cairo.h>
#include "config.h"
#include "assets.h"

#if DOOM_WAD_ASSETS
typedef struct {
    const unsigned char *data;
    size_t size;
    char name[9];
} lump_t;
typedef struct {
    char name[64];
    cairo_surface_t *surface;
} image_t;
typedef struct {
    const char *id, *prefix, *death, *walk, *pain;
    int origin_x, origin_y;
} sprite_t;
static sprite_t sprites[] = {
#define MONSTER(id, label, prefix, death, walk, pain, floating) {id, prefix, death, walk, pain, 0, 0},
#include "monsters.def"
#undef MONSTER
};
static unsigned char *wad;
static lump_t *lumps;
static size_t lump_count;
static const unsigned char *palette;
static image_t images[384];
static size_t image_count;

static uint16_t read16(const unsigned char *data) {
    return data[0] | (uint16_t)data[1] << 8;
}
static uint32_t read32(const unsigned char *data) {
    return data[0] | (uint32_t)data[1] << 8 |
           (uint32_t)data[2] << 16 | (uint32_t)data[3] << 24;
}
static int signed16(const unsigned char *data) {
    unsigned value = read16(data);
    return value < 32768 ? (int)value : (int)value - 65536;
}
static bool contains(size_t size, size_t offset, size_t length) {
    return offset <= size && length <= size - offset;
}
static void lump_name(char name[9], const unsigned char *raw) {
    for (int i = 0; i < 8; i++) {
        name[i] = raw[i] >= 'a' && raw[i] <= 'z' ? raw[i] - 'a' + 'A' : raw[i];
    }
    name[8] = '\0';
}
static lump_t *find_lump(const char *name) {
    /* As in Doom, later entries override earlier entries with the same name. */
    for (size_t i = lump_count; i > 0; i--) {
        if (strcmp(lumps[i - 1].name, name) == 0) {
            return &lumps[i - 1];
        }
    }
    return NULL;
}
static lump_t *find_sprite(const char *name, bool *flip) {
    *flip = false;
    for (size_t i = lump_count; i > 0; i--) {
        lump_t *entry = &lumps[i - 1];
        if (strlen(entry->name) != 8 || entry->name[5] < '0' || entry->name[5] > '9' ||
            entry->name[7] < '0' || entry->name[7] > '9') {
            continue;
        }
        if (memcmp(entry->name, name, 4) == 0 && entry->name[6] == name[4] && entry->name[7] == name[5]) {
            *flip = true;
            return entry;
        }
        if (memcmp(entry->name, name, 6) == 0) {
            return entry;
        }
    }
    return find_lump(name);
}
static uint32_t color_pixel(unsigned index) {
    return UINT32_C(0xff000000) | (uint32_t)palette[index * 3] << 16 |
           (uint32_t)palette[index * 3 + 1] << 8 | palette[index * 3 + 2];
}
static cairo_surface_t *new_image(int width, int height, bool opaque) {
    cairo_surface_t *surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, width, height);
    if (cairo_surface_status(surface) != CAIRO_STATUS_SUCCESS) {
        cairo_surface_destroy(surface);
        return NULL;
    }
    if (opaque) {
        uint32_t *pixels = (uint32_t *)cairo_image_surface_get_data(surface);
        int stride = cairo_image_surface_get_stride(surface) / 4;
        for (int y = 0; y < height; y++) {
            for (int x = 0; x < width; x++) {
                pixels[y * stride + x] = UINT32_C(0xff000000);
            }
        }
        cairo_surface_mark_dirty(surface);
    }
    return surface;
}
static cairo_surface_t *patch(lump_t *entry, int *left, int *top) {
    if (!entry || entry->size < 8) {
        return NULL;
    }
    const unsigned char *raw = entry->data;
    int width = read16(raw), height = read16(raw + 2);
    if (width <= 0 || width > 512 || height <= 0 || height > 512 ||
        !contains(entry->size, 8, (size_t)width * 4)) {
        return NULL;
    }
    *left = signed16(raw + 4);
    *top = signed16(raw + 6);
    cairo_surface_t *surface = new_image(width, height, false);
    if (!surface) {
        return NULL;
    }
    uint32_t *pixels = (uint32_t *)cairo_image_surface_get_data(surface);
    int stride = cairo_image_surface_get_stride(surface) / 4;
    for (int x = 0; x < width; x++) {
        size_t offset = read32(raw + 8 + x * 4);
        int previous = -1;
        int posts = 0;
        if (offset < 8 + (size_t)width * 4) {
            goto invalid;
        }
        while (true) {
            if (!contains(entry->size, offset, 1)) {
                goto invalid;
            }
            if (raw[offset] == 255) {
                break;
            }
            if (++posts > height + 1) {
                goto invalid;
            }
            if (!contains(entry->size, offset, 4)) {
                goto invalid;
            }
            int y = raw[offset], length = raw[offset + 1];
            /* Extended tall patches use a relative top delta. */
            if (y <= previous) {
                y += previous;
            }
            previous = y;
            if (y > height || length > height - y ||
                !contains(entry->size, offset, (size_t)length + 4)) {
                goto invalid;
            }
            for (int row = 0; row < length; row++) {
                pixels[(y + row) * stride + x] = color_pixel(raw[offset + 3 + row]);
            }
            offset += (size_t)length + 4;
        }
    }
    cairo_surface_mark_dirty(surface);
    return surface;
invalid:
    cairo_surface_destroy(surface);
    return NULL;
}
static cairo_surface_t *frame_image(const char *name) {
    bool flip;
    lump_t *entry = find_sprite(name, &flip);
    int left, top;
    cairo_surface_t *source = patch(entry, &left, &top);
    if (!source) {
        return NULL;
    }
    int width = cairo_image_surface_get_width(source), height = cairo_image_surface_get_height(source);
    if (flip) {
        left = width - left;
    }
    cairo_surface_t *canvas = new_image(256, 192, false);
    if (canvas) {
        uint32_t *src = (uint32_t *)cairo_image_surface_get_data(source);
        uint32_t *dst = (uint32_t *)cairo_image_surface_get_data(canvas);
        int src_stride = cairo_image_surface_get_stride(source) / 4;
        int dst_stride = cairo_image_surface_get_stride(canvas) / 4;
        for (int y = 0; y < height; y++) {
            int cy = 170 - top + y;
            if (cy < 0 || cy >= 192) {
                continue;
            }
            for (int x = 0; x < width; x++) {
                int cx = 128 - left + x;
                if (cx >= 0 && cx < 256) {
                    dst[cy * dst_stride + cx] = src[y * src_stride + (flip ? width - 1 - x : x)];
                }
            }
        }
        cairo_surface_mark_dirty(canvas);
    }
    cairo_surface_destroy(source);
    return canvas;
}
static bool cache_image(const char *name, cairo_surface_t *surface) {
    if (!surface) {
        return false;
    }
    if (image_count == sizeof(images) / sizeof(images[0]) || strlen(name) >= sizeof(images[0].name)) {
        cairo_surface_destroy(surface);
        return false;
    }
    image_t *image = &images[image_count++];
    snprintf(image->name, sizeof(image->name), "%s", name);
    image->surface = surface;
    return true;
}
static bool cache_frames(cairo_surface_t **frames, const char names[][64], int count, int *origin_x, int *origin_y) {
    int left = 256, top = 192, right = 0, bottom = 0;
    bool valid = true;
    for (int i = 0; i < count; i++) {
        if (!frames[i]) {
            valid = false;
            break;
        }
        uint32_t *pixels = (uint32_t *)cairo_image_surface_get_data(frames[i]);
        int stride = cairo_image_surface_get_stride(frames[i]) / 4;
        for (int y = 0; y < 192; y++) {
            for (int x = 0; x < 256; x++) {
                if (!(pixels[y * stride + x] >> 24)) {
                    continue;
                }
                if (x < left) {
                    left = x;
                }
                if (y < top) {
                    top = y;
                }
                if (x + 1 > right) {
                    right = x + 1;
                }
                if (y + 1 > bottom) {
                    bottom = y + 1;
                }
            }
        }
    }
    if (right <= left || bottom <= top) {
        valid = false;
    }
    for (int i = 0; i < count; i++) {
        if (valid) {
            cairo_surface_t *cropped = new_image(right - left, bottom - top, false);
            if (cropped) {
                cairo_t *ctx = cairo_create(cropped);
                cairo_set_source_surface(ctx, frames[i], -left, -top);
                cairo_paint(ctx);
                if (cairo_status(ctx) != CAIRO_STATUS_SUCCESS) {
                    cairo_surface_destroy(cropped);
                    cropped = NULL;
                }
                cairo_destroy(ctx);
            }
            valid = cache_image(names[i], cropped);
        }
        if (frames[i]) {
            cairo_surface_destroy(frames[i]);
        }
    }
    if (valid && origin_x) {
        *origin_x = 128 - left;
    }
    if (valid && origin_y) {
        *origin_y = 170 - top;
    }
    return valid;
}
static cairo_surface_t *flat_image(const char *name) {
    lump_t *entry = find_lump(name);
    if (!entry || entry->size != 4096) {
        return NULL;
    }
    cairo_surface_t *surface = new_image(64, 64, true);
    if (!surface) {
        return NULL;
    }
    uint32_t *pixels = (uint32_t *)cairo_image_surface_get_data(surface);
    int stride = cairo_image_surface_get_stride(surface) / 4;
    for (int y = 0; y < 64; y++) {
        for (int x = 0; x < 64; x++) {
            pixels[y * stride + x] = color_pixel(entry->data[y * 64 + x]);
        }
    }
    cairo_surface_mark_dirty(surface);
    return surface;
}

static bool load_ui(void) {
    /* UI graphics are optional so older PNG sets and complete sprite-only
     * PWADs keep working. Present but malformed patches still fail validation. */
    for (int code = 33; code <= 95; code++) {
        char lump[32], name[64];
        snprintf(lump, sizeof(lump), "STCFN%03d", code);
        lump_t *entry = find_lump(lump);
        if (!entry) {
            continue;
        }
        int left, top;
        cairo_surface_t *source = patch(entry, &left, &top);
        if (!source) {
            return false;
        }
        int width = cairo_image_surface_get_width(source), height = cairo_image_surface_get_height(source);
        if (width > 32 || height > 32 || left < -32 || left > 32 || top < -32 || top > 32) {
            cairo_surface_destroy(source);
            return false;
        }
        int canvas_height = height - top > 7 ? height - top : 7;
        cairo_surface_t *glyph = new_image(width, canvas_height, false);
        if (glyph) {
            cairo_t *ctx = cairo_create(glyph);
            cairo_set_source_surface(ctx, source, -left, -top);
            cairo_paint(ctx);
            if (cairo_status(ctx) != CAIRO_STATUS_SUCCESS) {
                cairo_surface_destroy(glyph);
                glyph = NULL;
            }
            cairo_destroy(ctx);
        }
        cairo_surface_destroy(source);
        snprintf(name, sizeof(name), "ui/font-%03d.png", code);
        if (!cache_image(name, glyph)) {
            return false;
        }
    }
    if (find_lump("GRNROCK") && !cache_image("ui/stone.png", flat_image("GRNROCK"))) {
        return false;
    }
    return true;
}

static bool load_sprites(void) {
    int left, top;
    if (!cache_image("player-dead.png", patch(find_lump("STFDEAD0"), &left, &top))) {
        return false;
    }
    cairo_surface_t *blast[4] = {0};
    char blast_names[4][64];
    for (int i = 0; i < 4; i++) {
        char name[9];
        snprintf(name, sizeof(name), "BFE2%c0", 'A' + i);
        blast[i] = frame_image(name);
        snprintf(blast_names[i], sizeof(blast_names[i]), "bfg-%02d.png", i);
    }
    if (!cache_frames(blast, blast_names, 4, NULL, NULL)) {
        return false;
    }
    for (int i = 0; i < 2; i++) {
        char name[9], filename[64];
        snprintf(name, sizeof(name), "BFS1%c0", 'A' + i);
        snprintf(filename, sizeof(filename), "bfg-projectile-%02d.png", i);
        if (!cache_image(filename, patch(find_lump(name), &left, &top))) {
            return false;
        }
    }
    for (size_t i = 0; i < sizeof(sprites) / sizeof(sprites[0]); i++) {
        sprite_t *sprite = &sprites[i];
        cairo_surface_t *frames[20] = {0};
        char filenames[20][64];
        int count = 0;
        const char *sequences[] = {"A", sprite->death, sprite->walk, sprite->pain};
        for (int sequence = 0; sequence < 4; sequence++) {
            for (int frame = 0; sequences[sequence][frame]; frame++) {
                char name[9];
                snprintf(name, sizeof(name), "%s%c0", sprite->prefix, sequences[sequence][frame]);
                if (!find_lump(name)) {
                    name[5] = '1';
                }
                frames[count] = frame_image(name);
                if (sequence == 0 || sequence == 3) {
                    snprintf(filenames[count], sizeof(filenames[count]), "%s/%s.png", sprite->id, sequence == 0 ? "idle" : "pain");
                } else {
                    snprintf(filenames[count], sizeof(filenames[count]), "%s/%s-%02d.png", sprite->id, sequence == 1 ? "death" : "walk", frame);
                }
                count++;
            }
        }
        if (!cache_frames(frames, filenames, count, &sprite->origin_x, &sprite->origin_y)) {
            return false;
        }
    }
    return true;
}
static cairo_surface_t *wall_image(const char *name) {
    lump_t *pnames = find_lump("PNAMES");
    if (!pnames || pnames->size < 4) {
        return NULL;
    }
    size_t name_count = read32(pnames->data);
    if (name_count > 100000 || name_count > (pnames->size - 4) / 8) {
        return NULL;
    }
    const unsigned char *definition = NULL;
    lump_t *table = NULL;
    const char *tables[] = {"TEXTURE1", "TEXTURE2"};
    for (int t = 0; t < 2; t++) {
        lump_t *candidate = find_lump(tables[t]);
        if (!candidate) {
            continue;
        }
        if (candidate->size < 4) {
            return NULL;
        }
        size_t count = read32(candidate->data);
        if (count > 100000 || count > (candidate->size - 4) / 4) {
            return NULL;
        }
        for (size_t i = 0; i < count; i++) {
            size_t offset = read32(candidate->data + 4 + i * 4);
            if (!contains(candidate->size, offset, 22)) {
                return NULL;
            }
            char texture_name[9];
            lump_name(texture_name, candidate->data + offset);
            if (strcmp(texture_name, name) == 0) {
                definition = candidate->data + offset;
                table = candidate;
            }
        }
    }
    if (!definition) {
        return NULL;
    }
    int width = read16(definition + 12), height = read16(definition + 14);
    size_t count = read16(definition + 20);
    if (!width || width > 1024 || !height || height > 1024 ||
        !contains(table->size, (size_t)(definition - table->data) + 22, count * 10)) {
        return NULL;
    }
    cairo_surface_t *surface = new_image(width, height, true);
    if (!surface) {
        return NULL;
    }
    cairo_t *ctx = cairo_create(surface);
    for (size_t i = 0; i < count; i++) {
        const unsigned char *part = definition + 22 + i * 10;
        size_t index = read16(part + 4);
        if (index >= name_count) {
            goto invalid;
        }
        char patch_name[9];
        lump_name(patch_name, pnames->data + 4 + index * 8);
        int left, top;
        cairo_surface_t *fragment = patch(find_lump(patch_name), &left, &top);
        if (!fragment) {
            goto invalid;
        }
        cairo_set_source_surface(ctx, fragment, signed16(part), signed16(part + 2));
        cairo_paint(ctx);
        cairo_surface_destroy(fragment);
        if (cairo_status(ctx) != CAIRO_STATUS_SUCCESS) {
            goto invalid;
        }
    }
    cairo_destroy(ctx);
    return surface;
invalid:
    cairo_destroy(ctx);
    cairo_surface_destroy(surface);
    return NULL;
}
static bool load_level(void) {
    const char *walls[] = {"TEKGREN2", "TEKGREN5", "STONE4", "PIPEWAL1", "BIGBRIK1", "BIGBRIK2",
                           "BSTONE1", "BSTONE2", "SKIN2", "TANROCK5"};
    const char *flats[] = {"FLOOR3_3", "GRNLITE1", "FLAT5_4", "FLAT1", "FLAT10", "FLOOR5_4",
                           "FLAT1_2", "FLOOR7_1", "CEIL5_1", "NUKAGE1", "NUKAGE2", "NUKAGE3"};
    for (size_t i = 0; i < sizeof(walls) / sizeof(walls[0]); i++) {
        char name[64];
        snprintf(name, sizeof(name), "level/%s.png", walls[i]);
        if (!cache_image(name, wall_image(walls[i]))) {
            return false;
        }
    }
    for (size_t i = 0; i < sizeof(flats) / sizeof(flats[0]); i++) {
        char name[64];
        snprintf(name, sizeof(name), "level/%s.png", flats[i]);
        if (!cache_image(name, flat_image(flats[i]))) {
            return false;
        }
    }
    return true;
}
#endif

void assets_close(void) {
#if DOOM_WAD_ASSETS
    for (size_t i = 0; i < image_count; i++) {
        cairo_surface_destroy(images[i].surface);
    }
    image_count = 0;
    free(lumps);
    free(wad);
    lumps = NULL;
    wad = NULL;
    lump_count = 0;
    palette = NULL;
#endif
}

bool assets_open(const char *wad_path) {
    assets_close();
    if (!wad_path || !wad_path[0]) {
        return true;
    }
#if DOOM_WAD_ASSETS
    FILE *file = fopen(wad_path, "rb");
    if (!file) {
        goto invalid;
    }
    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        goto invalid;
    }
    long length = ftell(file);
    if (length < 12 || length > 512L * 1024 * 1024 || fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        goto invalid;
    }
    size_t size = (size_t)length;
    wad = malloc(size);
    bool loaded = wad && fread(wad, 1, size, file) == size;
    fclose(file);
    if (!loaded || (memcmp(wad, "IWAD", 4) != 0 && memcmp(wad, "PWAD", 4) != 0)) {
        goto invalid;
    }
    lump_count = read32(wad + 4);
    size_t directory = read32(wad + 8);
    if (lump_count == 0 || lump_count > 100000 ||
        !contains(size, directory, lump_count * 16)) {
        goto invalid;
    }
    lumps = calloc(lump_count, sizeof(*lumps));
    if (!lumps) {
        goto invalid;
    }
    for (size_t i = 0; i < lump_count; i++) {
        const unsigned char *entry = wad + directory + i * 16;
        size_t offset = read32(entry), lump_size = read32(entry + 4);
        if (!contains(size, offset, lump_size)) {
            goto invalid;
        }
        lumps[i].data = wad + offset;
        lumps[i].size = lump_size;
        lump_name(lumps[i].name, entry + 8);
    }
    lump_t *playpal = find_lump("PLAYPAL");
    if (!playpal || playpal->size < 768) {
        goto invalid;
    }
    palette = playpal->data;
    if (!load_sprites() || !load_level() || !load_ui()) {
        goto invalid;
    }
    return true;
invalid:
    fprintf(stderr, "Doom assets: unreadable, invalid, or incomplete Doom II-compatible WAD\n");
    assets_close();
#else
    fprintf(stderr, "Doom assets: this build has WAD support disabled\n");
#endif
    return false;
}

cairo_surface_t *assets_image(const char *folder, const char *name) {
#if DOOM_WAD_ASSETS
    if (wad) {
        for (size_t i = 0; i < image_count; i++) {
            if (strcmp(images[i].name, name) == 0) {
                return cairo_surface_reference(images[i].surface);
            }
        }
        return NULL;
    }
#endif
    if (!folder || !folder[0]) {
        return NULL;
    }
    char path[4096];
    int length = snprintf(path, sizeof(path), "%s/%s", folder, name);
    if (length < 0 || (size_t)length >= sizeof(path)) {
        return NULL;
    }
    cairo_surface_t *surface = cairo_image_surface_create_from_png(path);
    if (cairo_surface_status(surface) == CAIRO_STATUS_SUCCESS) {
        return surface;
    }
    cairo_surface_destroy(surface);
    return NULL;
}

bool assets_origin(const char *folder, const char *monster, int *x, int *y) {
#if DOOM_WAD_ASSETS
    if (wad) {
        for (size_t i = 0; i < sizeof(sprites) / sizeof(sprites[0]); i++) {
            if (strcmp(sprites[i].id, monster) == 0) {
                *x = sprites[i].origin_x;
                *y = sprites[i].origin_y;
                return true;
            }
        }
        return false;
    }
#endif
    if (!folder || !folder[0]) {
        return false;
    }
    char path[4096];
    int length = snprintf(path, sizeof(path), "%s/%s/origin.txt", folder, monster);
    if (length < 0 || (size_t)length >= sizeof(path)) {
        return false;
    }
    FILE *file = fopen(path, "r");
    if (!file) {
        return false;
    }
    int fields = fscanf(file, "%d %d", x, y);
    fclose(file);
    return fields == 2 && *x >= 0 && *x <= 256 && *y >= 0 && *y <= 192;
}
