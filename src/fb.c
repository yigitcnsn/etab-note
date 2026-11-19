#define _GNU_SOURCE
#include "noteos.h"

#include <fcntl.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

const uint32_t SWATCH_RGB[5] = {
    0x1C1E26, /* 0: primary ink (replaced by theme.primary_ink) */
    0x005FB8, /* 1: Engram blue */
    0xD9383A, /* 2: crimson red */
    0x2E8B57, /* 3: forest green */
    0xE6B800  /* 4: amber / highlighter yellow */
};

const float PEN_SIZES[3] = { 1.8f, 3.4f, 6.0f };

const ThemePalette THEMES[THEME_COUNT] = {
    {
        .name        = "LIGHT",
        .bg          = 0xFFFFFF,
        .rule        = 0xE5E7EC,
        .margin      = 0xF2C6C6,
        .bar_bg      = 0xF4F5F7,
        .bar_border  = 0xD8DCE3,
        .bar_text    = 0x24292F,
        .bar_muted   = 0x6E7681,
        .accent      = 0x005FB8,
        .sidebar_bg  = 0xF7F8FA,
        .sidebar_sel = 0xDDEAF8,
        .primary_ink = 0x1E222A
    },
    {
        .name        = "PAPER",
        .bg          = 0xFDF6E3,
        .rule        = 0xE7DFC8,
        .margin      = 0xEBC2B8,
        .bar_bg      = 0xF3EBD3,
        .bar_border  = 0xD8CEB3,
        .bar_text    = 0x3F4D54,
        .bar_muted   = 0x7A888E,
        .accent      = 0x268BD2,
        .sidebar_bg  = 0xF6EED9,
        .sidebar_sel = 0xE4D8B8,
        .primary_ink = 0x263137
    },
    {
        .name        = "DARK",
        .bg          = 0x0D1117,
        .rule        = 0x1E2633,
        .margin      = 0x3D232A,
        .bar_bg      = 0x161B22,
        .bar_border  = 0x30363D,
        .bar_text    = 0xE6EDF3,
        .bar_muted   = 0x8B949E,
        .accent      = 0x58A6FF,
        .sidebar_bg  = 0x12171F,
        .sidebar_sel = 0x1F324D,
        .primary_ink = 0xE6EDF3
    }
};

/* ---- 5x7 bitmap font (ASCII 32..95) ---- */
static const uint8_t FONT5X7[][5] = {
    {0x00,0x00,0x00,0x00,0x00}, /* ' ' */
    {0x00,0x00,0x5F,0x00,0x00}, /* '!' */
    {0x00,0x07,0x00,0x07,0x00}, /* '"' */
    {0x14,0x7F,0x14,0x7F,0x14}, /* '#' */
    {0x24,0x2A,0x7F,0x2A,0x12}, /* '$' */
    {0x23,0x13,0x08,0x64,0x62}, /* '%' */
    {0x36,0x49,0x55,0x22,0x50}, /* '&' */
    {0x00,0x05,0x03,0x00,0x00}, /* ''' */
    {0x00,0x1C,0x22,0x41,0x00}, /* '(' */
    {0x00,0x41,0x22,0x1C,0x00}, /* ')' */
    {0x14,0x08,0x3E,0x08,0x14}, /* '*' */
    {0x08,0x08,0x3E,0x08,0x08}, /* '+' */
    {0x00,0x50,0x30,0x00,0x00}, /* ',' */
    {0x08,0x08,0x08,0x08,0x08}, /* '-' */
    {0x00,0x60,0x60,0x00,0x00}, /* '.' */
    {0x20,0x10,0x08,0x04,0x02}, /* '/' */
    {0x3E,0x51,0x49,0x45,0x3E}, /* '0' */
    {0x00,0x42,0x7F,0x40,0x00}, /* '1' */
    {0x42,0x61,0x51,0x49,0x46}, /* '2' */
    {0x21,0x41,0x45,0x4B,0x31}, /* '3' */
    {0x18,0x14,0x12,0x7F,0x10}, /* '4' */
    {0x27,0x45,0x45,0x45,0x39}, /* '5' */
    {0x3C,0x4A,0x49,0x49,0x30}, /* '6' */
    {0x01,0x71,0x09,0x05,0x03}, /* '7' */
    {0x36,0x49,0x49,0x49,0x36}, /* '8' */
    {0x06,0x49,0x49,0x29,0x1E}, /* '9' */
    {0x00,0x36,0x36,0x00,0x00}, /* ':' */
    {0x00,0x56,0x36,0x00,0x00}, /* ';' */
    {0x08,0x14,0x22,0x41,0x00}, /* '<' */
    {0x14,0x14,0x14,0x14,0x14}, /* '=' */
    {0x00,0x41,0x22,0x14,0x08}, /* '>' */
    {0x02,0x01,0x51,0x09,0x06}, /* '?' */
    {0x32,0x49,0x79,0x41,0x3E}, /* '@' */
    {0x7E,0x11,0x11,0x11,0x7E}, /* 'A' */
    {0x7F,0x49,0x49,0x49,0x36}, /* 'B' */
    {0x3E,0x41,0x41,0x41,0x22}, /* 'C' */
    {0x7F,0x41,0x41,0x22,0x1C}, /* 'D' */
    {0x7F,0x49,0x49,0x49,0x41}, /* 'E' */
    {0x7F,0x09,0x09,0x09,0x01}, /* 'F' */
    {0x3E,0x41,0x49,0x49,0x7A}, /* 'G' */
    {0x7F,0x08,0x08,0x08,0x7F}, /* 'H' */
    {0x00,0x41,0x7F,0x41,0x00}, /* 'I' */
    {0x20,0x40,0x41,0x3F,0x01}, /* 'J' */
    {0x7F,0x08,0x14,0x22,0x41}, /* 'K' */
    {0x7F,0x40,0x40,0x40,0x40}, /* 'L' */
    {0x7F,0x02,0x0C,0x02,0x7F}, /* 'M' */
    {0x7F,0x04,0x08,0x10,0x7F}, /* 'N' */
    {0x3E,0x41,0x41,0x41,0x3E}, /* 'O' */
    {0x7F,0x09,0x09,0x09,0x06}, /* 'P' */
    {0x3E,0x41,0x51,0x21,0x5E}, /* 'Q' */
    {0x7F,0x09,0x19,0x29,0x46}, /* 'R' */
    {0x46,0x49,0x49,0x49,0x31}, /* 'S' */
    {0x01,0x01,0x7F,0x01,0x01}, /* 'T' */
    {0x3F,0x40,0x40,0x40,0x3F}, /* 'U' */
    {0x1F,0x20,0x40,0x20,0x1F}, /* 'V' */
    {0x3F,0x40,0x38,0x40,0x3F}, /* 'W' */
    {0x63,0x14,0x08,0x14,0x63}, /* 'X' */
    {0x07,0x08,0x70,0x08,0x07}, /* 'Y' */
    {0x61,0x51,0x49,0x45,0x43}, /* 'Z' */
    {0x00,0x7F,0x41,0x41,0x00}, /* '[' */
    {0x02,0x04,0x08,0x10,0x20}, /* '\' */
    {0x00,0x41,0x41,0x7F,0x00}, /* ']' */
    {0x04,0x02,0x01,0x02,0x04}, /* '^' */
    {0x40,0x40,0x40,0x40,0x40}  /* '_' */
};

void rect_reset(Rect *r) {
    r->valid = false;
}

void rect_expand(Rect *r, int x0, int y0, int x1, int y1, int w, int h) {
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 >= w) x1 = w - 1;
    if (y1 >= h) y1 = h - 1;
    if (x0 > x1 || y0 > y1) return;
    if (!r->valid) {
        r->x0 = x0; r->y0 = y0; r->x1 = x1; r->y1 = y1;
        r->valid = true;
    } else {
        if (x0 < r->x0) r->x0 = x0;
        if (y0 < r->y0) r->y0 = y0;
        if (x1 > r->x1) r->x1 = x1;
        if (y1 > r->y1) r->y1 = y1;
    }
}

void fb_mark_dirty(FB *fb, int x0, int y0, int x1, int y1) {
    rect_expand(&fb->dirty[0], x0, y0, x1, y1, fb->w, fb->h);
    rect_expand(&fb->dirty[1], x0, y0, x1, y1, fb->w, fb->h);
}

void fb_mark_all(FB *fb) {
    fb_mark_dirty(fb, 0, 0, fb->w - 1, fb->h - 1);
}

uint32_t fb_pack_hex(const FB *fb, uint32_t hex) {
    uint32_t r = (hex >> 16) & 0xFFu;
    uint32_t g = (hex >> 8) & 0xFFu;
    uint32_t b = hex & 0xFFu;
    uint32_t px = (r << fb->vinfo.red.offset)
                | (g << fb->vinfo.green.offset)
                | (b << fb->vinfo.blue.offset);
    if (fb->vinfo.transp.length)
        px |= (0xFFu << fb->vinfo.transp.offset);
    return px;
}

static inline uint32_t blend_pixel(const FB *fb, uint32_t dst_px, uint8_t sr, uint8_t sg, uint8_t sb, uint8_t alpha) {
    if (alpha >= 254) {
        uint32_t px = ((uint32_t)sr << fb->vinfo.red.offset)
                    | ((uint32_t)sg << fb->vinfo.green.offset)
                    | ((uint32_t)sb << fb->vinfo.blue.offset);
        if (fb->vinfo.transp.length) px |= (0xFFu << fb->vinfo.transp.offset);
        return px;
    }
    uint32_t dr = (dst_px >> fb->vinfo.red.offset) & 0xFFu;
    uint32_t dg = (dst_px >> fb->vinfo.green.offset) & 0xFFu;
    uint32_t db = (dst_px >> fb->vinfo.blue.offset) & 0xFFu;
    uint32_t inv = 255u - alpha;
    uint32_t rr = (sr * alpha + dr * inv + 127u) / 255u;
    uint32_t rg = (sg * alpha + dg * inv + 127u) / 255u;
    uint32_t rb = (sb * alpha + db * inv + 127u) / 255u;
    uint32_t px = (rr << fb->vinfo.red.offset)
                | (rg << fb->vinfo.green.offset)
                | (rb << fb->vinfo.blue.offset);
    if (fb->vinfo.transp.length) px |= (0xFFu << fb->vinfo.transp.offset);
    return px;
}

int fb_get_backlight(void) {
    FILE *f = fopen("/sys/class/leds/lcd-backlight/brightness", "r");
    if (!f) return 180;
    int v = 180;
    if (fscanf(f, "%d", &v) != 1) v = 180;
    fclose(f);
    return v;
}

void fb_set_backlight(int level) {
    if (level < 0) level = 0;
    if (level > 255) level = 255;
    FILE *f = fopen("/sys/class/leds/lcd-backlight/brightness", "w");
    if (f) {
        fprintf(f, "%d\n", level);
        fclose(f);
    }
}

void fb_blank(FB *fb, bool blank) {
    ioctl(fb->fd, FBIOBLANK, blank ? FB_BLANK_POWERDOWN : FB_BLANK_UNBLANK);
}

int fb_open(FB *fb) {
    memset(fb, 0, sizeof(*fb));
    fb->fd = open("/dev/graphics/fb0", O_RDWR);
    if (fb->fd < 0) fb->fd = open("/dev/fb0", O_RDWR);
    if (fb->fd < 0) {
        perror("open fb0");
        return -1;
    }
    if (ioctl(fb->fd, FBIOGET_FSCREENINFO, &fb->finfo) < 0 ||
        ioctl(fb->fd, FBIOGET_VSCREENINFO, &fb->vinfo) < 0) {
        perror("fb ioctl");
        close(fb->fd);
        return -1;
    }
    fb_blank(fb, false);
    if (fb_get_backlight() < 40) fb_set_backlight(190);

    fb->w = (int)fb->vinfo.xres;
    fb->h = (int)fb->vinfo.yres;
    fb->double_buf = (fb->vinfo.yres_virtual >= fb->vinfo.yres * 2);
    fb->fb_size = fb->finfo.smem_len;

    fb->fb_mem = mmap(NULL, fb->fb_size, PROT_READ | PROT_WRITE, MAP_SHARED, fb->fd, 0);
    if (fb->fb_mem == MAP_FAILED) {
        perror("mmap fb0");
        close(fb->fd);
        return -1;
    }
    fb->canvas = malloc((size_t)fb->w * (size_t)fb->h * sizeof(uint32_t));
    if (!fb->canvas) {
        munmap(fb->fb_mem, fb->fb_size);
        close(fb->fd);
        return -1;
    }
    rect_reset(&fb->dirty[0]);
    rect_reset(&fb->dirty[1]);
    rect_reset(&fb->prev_cursor[0]);
    rect_reset(&fb->prev_cursor[1]);
    return 0;
}

void fb_close(FB *fb) {
    free(fb->canvas);
    if (fb->fb_mem && fb->fb_mem != MAP_FAILED) munmap(fb->fb_mem, fb->fb_size);
    if (fb->fd >= 0) close(fb->fd);
}

void fb_fill_rect(FB *fb, int x, int y, int w, int h, uint32_t hex) {
    int x0 = x < 0 ? 0 : x;
    int y0 = y < 0 ? 0 : y;
    int x1 = x + w - 1;
    int y1 = y + h - 1;
    if (x1 >= fb->w) x1 = fb->w - 1;
    if (y1 >= fb->h) y1 = fb->h - 1;
    if (x0 > x1 || y0 > y1) return;
    uint32_t px = fb_pack_hex(fb, hex);
    for (int row = y0; row <= y1; row++) {
        uint32_t *dst = fb->canvas + (size_t)row * fb->w;
        for (int col = x0; col <= x1; col++) dst[col] = px;
    }
    fb_mark_dirty(fb, x0, y0, x1, y1);
}

void fb_stroke_rect(FB *fb, int x, int y, int w, int h, int thick, uint32_t hex) {
    fb_fill_rect(fb, x, y, w, thick, hex);
    fb_fill_rect(fb, x, y + h - thick, w, thick, hex);
    fb_fill_rect(fb, x, y, thick, h, hex);
    fb_fill_rect(fb, x + w - thick, y, thick, h, hex);
}

void fb_fill_circle(FB *fb, int cx, int cy, int r, uint32_t hex) {
    int x0 = cx - r - 1, y0 = cy - r - 1;
    int x1 = cx + r + 1, y1 = cy + r + 1;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 >= fb->w) x1 = fb->w - 1;
    if (y1 >= fb->h) y1 = fb->h - 1;
    uint8_t sr = (uint8_t)((hex >> 16) & 0xFF);
    uint8_t sg = (uint8_t)((hex >> 8) & 0xFF);
    uint8_t sb = (uint8_t)(hex & 0xFF);
    float rf = (float)r;
    for (int y = y0; y <= y1; y++) {
        float dy = (float)(y - cy);
        uint32_t *row = fb->canvas + (size_t)y * fb->w;
        for (int x = x0; x <= x1; x++) {
            float dx = (float)(x - cx);
            float d = sqrtf(dx * dx + dy * dy);
            if (d <= rf - 0.5f) {
                row[x] = blend_pixel(fb, row[x], sr, sg, sb, 255);
            } else if (d < rf + 0.5f) {
                uint8_t a = (uint8_t)((rf + 0.5f - d) * 255.0f);
                row[x] = blend_pixel(fb, row[x], sr, sg, sb, a);
            }
        }
    }
    fb_mark_dirty(fb, x0, y0, x1, y1);
}

int fb_text_width(const char *s, int scale) {
    int len = (int)strlen(s);
    if (len <= 0) return 0;
    return len * 6 * scale - scale;
}

void fb_draw_text(FB *fb, int x, int y, const char *s, uint32_t hex, int scale) {
    uint32_t px = fb_pack_hex(fb, hex);
    int start_x = x;
    for (; *s; s++) {
        char c = *s;
        if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
        if (c == '|') {
            for (int dy = 0; dy < 7 * scale; dy++) {
                int py = y + dy;
                if (py < 0 || py >= fb->h) continue;
                for (int dx = 0; dx < scale; dx++) {
                    int px_x = x + 2 * scale + dx;
                    if (px_x >= 0 && px_x < fb->w) fb->canvas[(size_t)py * fb->w + px_x] = px;
                }
            }
        } else if (c >= 32 && c <= 95) {
            const uint8_t *glyph = FONT5X7[c - 32];
            for (int col = 0; col < 5; col++) {
                uint8_t bits = glyph[col];
                for (int row = 0; row < 7; row++) {
                    if (bits & (1u << row)) {
                        for (int sy = 0; sy < scale; sy++) {
                            int py = y + row * scale + sy;
                            if (py < 0 || py >= fb->h) continue;
                            for (int sx = 0; sx < scale; sx++) {
                                int px_x = x + col * scale + sx;
                                if (px_x >= 0 && px_x < fb->w)
                                    fb->canvas[(size_t)py * fb->w + px_x] = px;
                            }
                        }
                    }
                }
            }
        }
        x += 6 * scale;
    }
    fb_mark_dirty(fb, start_x, y, x, y + 7 * scale);
}

void render_paper_region(FB *fb, ThemeKind theme, PaperKind paper, int x0, int y0, int x1, int y1) {
    const ThemePalette *tp = &THEMES[theme];
    if (x0 < 0) x0 = 0;
    if (y0 < TOOLBAR_H) y0 = TOOLBAR_H;
    if (x1 >= fb->w) x1 = fb->w - 1;
    if (y1 >= fb->h) y1 = fb->h - 1;
    if (x0 > x1 || y0 > y1) return;

    uint32_t bg_px     = fb_pack_hex(fb, tp->bg);
    uint32_t rule_px   = fb_pack_hex(fb, tp->rule);
    uint32_t margin_px = fb_pack_hex(fb, tp->margin);

    for (int y = y0; y <= y1; y++) {
        uint32_t *row = fb->canvas + (size_t)y * fb->w;
        int ry = y - TOOLBAR_H;
        bool h_line = false;
        if (paper == PAPER_LINED) h_line = (ry > 0 && (ry % 48 == 47));
        else if (paper == PAPER_GRID) h_line = (ry > 0 && (ry % 40 == 0));

        if (h_line) {
            for (int x = x0; x <= x1; x++) row[x] = rule_px;
        } else {
            for (int x = x0; x <= x1; x++) row[x] = bg_px;
            if (paper == PAPER_GRID) {
                int gx = ((x0 + 39) / 40) * 40;
                for (int x = gx; x <= x1; x += 40) row[x] = rule_px;
            }
        }
        if (paper == PAPER_LINED && x0 <= 116 && x1 >= 116) {
            row[116] = margin_px;
        }
    }
    fb_mark_dirty(fb, x0, y0, x1, y1);
}

static uint32_t stroke_color_hex(ThemeKind theme, uint8_t tool, uint8_t color_idx) {
    if (tool == TOOL_HIGHLIGHTER) {
        if (color_idx == 0) return 0xF5D94E;
        return SWATCH_RGB[color_idx < 5 ? color_idx : 4];
    }
    if (color_idx == 0) return THEMES[theme].primary_ink;
    return SWATCH_RGB[color_idx < 5 ? color_idx : 0];
}

static float stroke_radius_at(uint8_t tool, uint8_t size_idx, float p) {
    if (p < 0.0f) p = 0.0f;
    if (p > 1.0f) p = 1.0f;
    float base = PEN_SIZES[size_idx < 3 ? size_idx : 1];
    switch (tool) {
        case TOOL_HIGHLIGHTER:
            return base * 3.8f + 4.0f;
        case TOOL_BRUSH:
            /* Dramatic fountain / calligraphy taper */
            return base * (0.18f + 2.15f * powf(p, 1.15f));
        case TOOL_PENCIL:
            /* Soft wide graphite tip */
            return base * (0.65f + 0.85f * p);
        default: /* TOOL_PEN */
            return base * (0.35f + 1.15f * powf(p, 0.78f));
    }
}

static inline uint32_t grain_hash(int x, int y) {
    uint32_t h = (uint32_t)x * 374761393u + (uint32_t)y * 668265263u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return (h ^ (h >> 16)) & 0xFFu;
}

static void stamp_aa_disc(FB *fb, float cx, float cy, float r,
                          uint8_t sr, uint8_t sg, uint8_t sb, uint8_t max_alpha,
                          bool pencil_grain, int clip_x0) {
    int ir = (int)ceilf(r) + 1;
    int x0 = (int)floorf(cx) - ir;
    int y0 = (int)floorf(cy) - ir;
    int x1 = (int)ceilf(cx) + ir;
    int y1 = (int)ceilf(cy) + ir;
    if (x0 < clip_x0) x0 = clip_x0;
    if (y0 < TOOLBAR_H) y0 = TOOLBAR_H;
    if (x1 >= fb->w) x1 = fb->w - 1;
    if (y1 >= fb->h) y1 = fb->h - 1;
    if (x0 > x1 || y0 > y1) return;

    float inner = r - 0.65f;
    float outer = r + 0.65f;
    float inv_span = 1.0f / (outer - inner);

    for (int y = y0; y <= y1; y++) {
        float dy = (float)y - cy;
        float dy2 = dy * dy;
        uint32_t *row = fb->canvas + (size_t)y * fb->w;
        for (int x = x0; x <= x1; x++) {
            float dx = (float)x - cx;
            float d2 = dx * dx + dy2;
            if (d2 >= outer * outer) continue;
            float cov;
            if (d2 <= inner * inner && inner > 0.0f) {
                cov = 1.0f;
            } else {
                float d = sqrtf(d2);
                cov = (outer - d) * inv_span;
                if (cov <= 0.0f) continue;
                if (cov > 1.0f) cov = 1.0f;
            }
            uint32_t a = (uint32_t)(cov * (float)max_alpha);
            if (pencil_grain) {
                uint32_t g = grain_hash(x, y);
                a = (a * (90u + g)) / 345u;
            }
            if (a > 0) row[x] = blend_pixel(fb, row[x], sr, sg, sb, (uint8_t)a);
        }
    }
    fb_mark_dirty(fb, x0, y0, x1, y1);
}

void render_stroke_segment(FB *fb, ThemeKind theme, uint8_t tool, uint8_t color_idx, uint8_t size_idx,
                           StrokePoint a, StrokePoint b, int clip_x0) {
    uint32_t hex = stroke_color_hex(theme, tool, color_idx);
    uint8_t sr = (uint8_t)((hex >> 16) & 0xFF);
    uint8_t sg = (uint8_t)((hex >> 8) & 0xFF);
    uint8_t sb = (uint8_t)(hex & 0xFF);
    bool pencil = (tool == TOOL_PENCIL);

    float r0 = stroke_radius_at(tool, size_idx, a.p);
    float r1 = stroke_radius_at(tool, size_idx, b.p);
    float dx = b.x - a.x;
    float dy = b.y - a.y;
    float dist = sqrtf(dx * dx + dy * dy);
    float step = (tool == TOOL_HIGHLIGHTER)
        ? fmaxf(3.0f, fminf(r0, r1) * 0.45f)
        : (pencil ? fmaxf(1.2f, fminf(r0, r1) * 0.45f)
                  : fmaxf(0.55f, fminf(r0, r1) * 0.30f));
    int steps = (int)ceilf(dist / step);
    if (steps < 1) steps = 1;
    for (int i = 0; i <= steps; i++) {
        float t = (float)i / (float)steps;
        float x = a.x + dx * t;
        float y = a.y + dy * t;
        float r = r0 + (r1 - r0) * t;
        float p = a.p + (b.p - a.p) * t;
        uint8_t alpha = 255;
        if (tool == TOOL_HIGHLIGHTER) alpha = 52;
        else if (pencil) alpha = (uint8_t)(75.0f + 135.0f * p);
        stamp_aa_disc(fb, x, y, r, sr, sg, sb, alpha, pencil, clip_x0);
    }
}

void render_stroke(FB *fb, ThemeKind theme, const Stroke *st, int clip_x0) {
    if (st->count == 0) return;
    if (st->count == 1) {
        render_stroke_segment(fb, theme, st->tool, st->color_idx, st->size_idx,
                              st->pts[0], st->pts[0], clip_x0);
        return;
    }
    for (uint32_t i = 1; i < st->count; i++) {
        render_stroke_segment(fb, theme, st->tool, st->color_idx, st->size_idx,
                              st->pts[i - 1], st->pts[i], clip_x0);
    }
}

void render_page(FB *fb, const NoteDoc *doc, int clip_x0) {
    render_paper_region(fb, (ThemeKind)doc->theme, (PaperKind)doc->paper,
                        clip_x0, TOOLBAR_H, fb->w - 1, fb->h - 1);
    const Page *pg = &doc->pages[doc->cur_page];
    for (size_t i = 0; i < pg->count; i++) {
        render_stroke(fb, (ThemeKind)doc->theme, &pg->strokes[i], clip_x0);
    }
}

/* ---- Radial Circular Menu (Stylus Button) ---- */

int radial_hit_wedge(const RadialMenu *rm, int x, int y) {
    if (!rm->open) return -1;
    float dx = (float)(x - rm->cx);
    float dy = (float)(y - rm->cy);
    float d2 = dx * dx + dy * dy;
    if (d2 < (float)(RADIAL_INNER_R * RADIAL_INNER_R) ||
        d2 > (float)(RADIAL_OUTER_R * RADIAL_OUTER_R)) {
        return -1;
    }
    /* Wedge 0 centered at -90 deg (top), clockwise */
    float ang = atan2f(dy, dx) + (float)(M_PI * 0.5) + (float)(M_PI / 8.0);
    while (ang < 0.0f) ang += (float)(2.0 * M_PI);
    while (ang >= (float)(2.0 * M_PI)) ang -= (float)(2.0 * M_PI);
    int idx = (int)(ang / (float)(M_PI / 4.0));
    if (idx < 0) idx = 0;
    if (idx >= RADIAL_WEDGES) idx = RADIAL_WEDGES - 1;
    return idx;
}

int radial_hit_color(const RadialMenu *rm, int x, int y) {
    if (!rm->open) return -1;
    int bar_y = rm->cy + RADIAL_OUTER_R + 34;
    for (int i = 0; i < 5; i++) {
        int sx = rm->cx + (i - 2) * 52;
        int dx = x - sx, dy = y - bar_y;
        if (dx * dx + dy * dy <= 24 * 24) return i;
    }
    return -1;
}

void render_radial_menu(FB *fb, ThemeKind theme, const RadialMenu *rm,
                        ToolKind cur_tool, uint8_t cur_size, uint8_t cur_color) {
    if (!rm->open) return;
    const ThemePalette *tp = &THEMES[theme];
    int cx = rm->cx, cy = rm->cy;
    int outer = RADIAL_OUTER_R;
    int inner = RADIAL_INNER_R;

    int x0 = cx - outer - 4, y0 = cy - outer - 4;
    int x1 = cx + outer + 4, y1 = cy + outer + 4;
    if (x0 < 0) x0 = 0;
    if (y0 < TOOLBAR_H) y0 = TOOLBAR_H;
    if (x1 >= fb->w) x1 = fb->w - 1;
    if (y1 >= fb->h) y1 = fb->h - 1;

    uint8_t bg_r = (tp->bar_bg >> 16) & 0xFF, bg_g = (tp->bar_bg >> 8) & 0xFF, bg_b = tp->bar_bg & 0xFF;
    uint8_t bd_r = (tp->bar_border >> 16) & 0xFF, bd_g = (tp->bar_border >> 8) & 0xFF, bd_b = tp->bar_border & 0xFF;
    uint8_t ac_r = (tp->accent >> 16) & 0xFF, ac_g = (tp->accent >> 8) & 0xFF, ac_b = tp->accent & 0xFF;
    uint8_t sl_r = (tp->sidebar_sel >> 16) & 0xFF, sl_g = (tp->sidebar_sel >> 8) & 0xFF, sl_b = tp->sidebar_sel & 0xFF;

    int active_wedge = -1;
    if (cur_tool == TOOL_PEN) active_wedge = 0;
    else if (cur_tool == TOOL_BRUSH) active_wedge = 1;
    else if (cur_tool == TOOL_PENCIL) active_wedge = 2;
    else if (cur_tool == TOOL_HIGHLIGHTER) active_wedge = 3;
    else if (cur_tool == TOOL_ERASER) active_wedge = 4;

    for (int y = y0; y <= y1; y++) {
        float dy = (float)(y - cy);
        uint32_t *row = fb->canvas + (size_t)y * fb->w;
        for (int x = x0; x <= x1; x++) {
            float dx = (float)(x - cx);
            float d = sqrtf(dx * dx + dy * dy);
            if (d > (float)outer + 2.0f) continue;

            if (d > (float)outer - 2.0f) {
                /* outer border ring */
                row[x] = blend_pixel(fb, row[x], bd_r, bd_g, bd_b, 240);
            } else if (d < (float)inner - 2.0f) {
                /* center dismiss hub */
                row[x] = blend_pixel(fb, row[x], bg_r, bg_g, bg_b, 235);
            } else if (d < (float)inner + 2.0f) {
                /* inner border ring */
                row[x] = blend_pixel(fb, row[x], bd_r, bd_g, bd_b, 240);
            } else {
                /* inside radial ring */
                float ang = atan2f(dy, dx) + (float)(M_PI * 0.5) + (float)(M_PI / 8.0);
                while (ang < 0.0f) ang += (float)(2.0 * M_PI);
                while (ang >= (float)(2.0 * M_PI)) ang -= (float)(2.0 * M_PI);
                float wedge_f = ang / (float)(M_PI / 4.0);
                int idx = (int)wedge_f;
                if (idx >= RADIAL_WEDGES) idx = RADIAL_WEDGES - 1;
                float frac = wedge_f - (float)idx;
                float edge_dist = fminf(frac, 1.0f - frac) * d * (float)(M_PI / 4.0);

                if (edge_dist < 1.3f) {
                    row[x] = blend_pixel(fb, row[x], bd_r, bd_g, bd_b, 220);
                } else if (idx == rm->hover_wedge) {
                    row[x] = blend_pixel(fb, row[x], ac_r, ac_g, ac_b, 235);
                } else if (idx == active_wedge) {
                    row[x] = blend_pixel(fb, row[x], sl_r, sl_g, sl_b, 235);
                } else {
                    row[x] = blend_pixel(fb, row[x], bg_r, bg_g, bg_b, 230);
                }
            }
        }
    }
    fb_mark_dirty(fb, x0, y0, x1, y1);

    /* Wedge labels */
    const char *size_lbl[3] = { "SZ:S", "SZ:M", "SZ:L" };
    const char *labels[RADIAL_WEDGES] = {
        "PEN", "BRUSH", "PENCIL", "HI", "ERASE", size_lbl[cur_size % 3], "UNDO", "REDO"
    };
    float mid_r = (float)(inner + outer) * 0.52f;
    for (int i = 0; i < RADIAL_WEDGES; i++) {
        float theta = (float)(-M_PI * 0.5) + (float)i * (float)(M_PI / 4.0);
        int lx = cx + (int)(cosf(theta) * mid_r);
        int ly = cy + (int)(sinf(theta) * mid_r);
        int tw = fb_text_width(labels[i], 2);
        uint32_t fg = (i == rm->hover_wedge) ? 0xFFFFFF : tp->bar_text;
        fb_draw_text(fb, lx - tw / 2, ly - 7, labels[i], fg, 2);
    }

    /* Center X */
    fb_draw_text(fb, cx - 5, cy - 7, "X", tp->bar_muted, 2);

    /* Color swatch pill underneath the wheel */
    int bar_w = 276, bar_h = 52;
    int bar_x = cx - bar_w / 2;
    int bar_y = cy + outer + 8;
    fb_fill_rect(fb, bar_x, bar_y, bar_w, bar_h, tp->bar_bg);
    fb_stroke_rect(fb, bar_x, bar_y, bar_w, bar_h, 2, tp->bar_border);
    int swatch_cy = bar_y + bar_h / 2;
    for (int i = 0; i < 5; i++) {
        int sx = cx + (i - 2) * 52;
        uint32_t col = (i == 0) ? tp->primary_ink : SWATCH_RGB[i];
        if (cur_color == (uint8_t)i || rm->hover_color == i) {
            fb_fill_circle(fb, sx, swatch_cy, 20, tp->accent);
            fb_fill_circle(fb, sx, swatch_cy, 17, tp->bar_bg);
        }
        fb_fill_circle(fb, sx, swatch_cy, 14, col);
    }
}

static void blit_rect_to_page(FB *fb, int page, const Rect *r) {
    if (!r->valid) return;
    int y_off = page * fb->h;
    int count = r->x1 - r->x0 + 1;
    for (int y = r->y0; y <= r->y1; y++) {
        uint32_t *dst = (uint32_t *)(fb->fb_mem + (size_t)(y + y_off) * fb->finfo.line_length) + r->x0;
        const uint32_t *src = fb->canvas + (size_t)y * fb->w + r->x0;
        memcpy(dst, src, (size_t)count * sizeof(uint32_t));
    }
}

static void draw_cursor_on_page(FB *fb, int page, int cx, int cy, bool erase_cursor, int clip_x0) {
    if (cy < TOOLBAR_H || cy >= fb->h || cx < clip_x0 || cx >= fb->w) return;
    uint32_t px = fb_pack_hex(fb, erase_cursor ? 0xD9383A : 0x005FB8);
    int rad = erase_cursor ? 16 : 6;
    int y_off = page * fb->h;
    for (int dy = -rad; dy <= rad; dy++) {
        int y = cy + dy;
        if (y < TOOLBAR_H || y >= fb->h) continue;
        uint32_t *row = (uint32_t *)(fb->fb_mem + (size_t)(y + y_off) * fb->finfo.line_length);
        for (int dx = -rad; dx <= rad; dx++) {
            int x = cx + dx;
            if (x < clip_x0 || x >= fb->w) continue;
            int d2 = dx * dx + dy * dy;
            if (d2 <= rad * rad && d2 >= (rad - 2) * (rad - 2)) {
                row[x] = px;
            }
        }
    }
}

void fb_present(FB *fb, bool hover, int hx, int hy, bool erase_cursor, int clip_x0) {
    int page = fb->double_buf ? fb->back_page : 0;

    if (fb->prev_cursor[page].valid) {
        blit_rect_to_page(fb, page, &fb->prev_cursor[page]);
        rect_reset(&fb->prev_cursor[page]);
    }
    if (fb->dirty[page].valid) {
        blit_rect_to_page(fb, page, &fb->dirty[page]);
        rect_reset(&fb->dirty[page]);
    }
    if (hover && hy >= TOOLBAR_H && hx >= clip_x0) {
        draw_cursor_on_page(fb, page, hx, hy, erase_cursor, clip_x0);
        int rad = erase_cursor ? 18 : 8;
        rect_expand(&fb->prev_cursor[page], hx - rad, hy - rad, hx + rad, hy + rad, fb->w, fb->h);
    }

    fb->vinfo.xoffset = 0;
    fb->vinfo.yoffset = (uint32_t)(page * fb->h);
    ioctl(fb->fd, FBIOPAN_DISPLAY, &fb->vinfo);

    if (fb->double_buf) fb->back_page ^= 1;
}
