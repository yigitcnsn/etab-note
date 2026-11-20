#define _GNU_SOURCE
#include "fb.h"

#include <fcntl.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

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

/* 5x7 bitmap font (ASCII 32..126) */
static const uint8_t FONT5X7[][5] = {
    {0x00,0x00,0x00,0x00,0x00}, /* 32 ' ' */
    {0x00,0x00,0x5F,0x00,0x00}, /* 33 '!' */
    {0x00,0x07,0x00,0x07,0x00}, /* 34 '"' */
    {0x14,0x7F,0x14,0x7F,0x14}, /* 35 '#' */
    {0x24,0x2A,0x7F,0x2A,0x12}, /* 36 '$' */
    {0x23,0x13,0x08,0x64,0x62}, /* 37 '%' */
    {0x36,0x49,0x55,0x22,0x50}, /* 38 '&' */
    {0x00,0x05,0x03,0x00,0x00}, /* 39 ''' */
    {0x00,0x1C,0x22,0x41,0x00}, /* 40 '(' */
    {0x00,0x41,0x22,0x1C,0x00}, /* 41 ')' */
    {0x14,0x08,0x3E,0x08,0x14}, /* 42 '*' */
    {0x08,0x08,0x3E,0x08,0x08}, /* 43 '+' */
    {0x00,0x50,0x30,0x00,0x00}, /* 44 ',' */
    {0x08,0x08,0x08,0x08,0x08}, /* 45 '-' */
    {0x00,0x60,0x60,0x00,0x00}, /* 46 '.' */
    {0x20,0x10,0x08,0x04,0x02}, /* 47 '/' */
    {0x3E,0x51,0x49,0x45,0x3E}, /* 48 '0' */
    {0x00,0x42,0x7F,0x40,0x00}, /* 49 '1' */
    {0x42,0x61,0x51,0x49,0x46}, /* 50 '2' */
    {0x21,0x41,0x45,0x4B,0x31}, /* 51 '3' */
    {0x18,0x14,0x12,0x7F,0x10}, /* 52 '4' */
    {0x27,0x45,0x45,0x45,0x39}, /* 53 '5' */
    {0x3C,0x4A,0x49,0x49,0x30}, /* 54 '6' */
    {0x01,0x71,0x09,0x05,0x03}, /* 55 '7' */
    {0x36,0x49,0x49,0x49,0x36}, /* 56 '8' */
    {0x06,0x49,0x49,0x29,0x1E}, /* 57 '9' */
    {0x00,0x36,0x36,0x00,0x00}, /* 58 ':' */
    {0x00,0x56,0x36,0x00,0x00}, /* 59 ';' */
    {0x08,0x14,0x22,0x41,0x00}, /* 60 '<' */
    {0x14,0x14,0x14,0x14,0x14}, /* 61 '=' */
    {0x00,0x41,0x22,0x14,0x08}, /* 62 '>' */
    {0x02,0x01,0x51,0x09,0x06}, /* 63 '?' */
    {0x32,0x49,0x79,0x41,0x3E}, /* 64 '@' */
    {0x7E,0x11,0x11,0x11,0x7E}, /* 65 'A' */
    {0x7F,0x49,0x49,0x49,0x36}, /* 66 'B' */
    {0x3E,0x41,0x41,0x41,0x22}, /* 67 'C' */
    {0x7F,0x41,0x41,0x22,0x1C}, /* 68 'D' */
    {0x7F,0x49,0x49,0x49,0x41}, /* 69 'E' */
    {0x7F,0x09,0x09,0x09,0x01}, /* 70 'F' */
    {0x3E,0x41,0x49,0x49,0x7A}, /* 71 'G' */
    {0x7F,0x08,0x08,0x08,0x7F}, /* 72 'H' */
    {0x00,0x41,0x7F,0x41,0x00}, /* 73 'I' */
    {0x20,0x40,0x41,0x3F,0x01}, /* 74 'J' */
    {0x7F,0x08,0x14,0x22,0x41}, /* 75 'K' */
    {0x7F,0x40,0x40,0x40,0x40}, /* 76 'L' */
    {0x7F,0x02,0x0C,0x02,0x7F}, /* 77 'M' */
    {0x7F,0x04,0x08,0x10,0x7F}, /* 78 'N' */
    {0x3E,0x41,0x41,0x41,0x3E}, /* 79 'O' */
    {0x7F,0x09,0x09,0x09,0x06}, /* 80 'P' */
    {0x3E,0x41,0x51,0x21,0x5E}, /* 81 'Q' */
    {0x7F,0x09,0x19,0x29,0x46}, /* 82 'R' */
    {0x46,0x49,0x49,0x49,0x31}, /* 83 'S' */
    {0x01,0x01,0x7F,0x01,0x01}, /* 84 'T' */
    {0x3F,0x40,0x40,0x40,0x3F}, /* 85 'U' */
    {0x1F,0x20,0x40,0x20,0x1F}, /* 86 'V' */
    {0x3F,0x40,0x38,0x40,0x3F}, /* 87 'W' */
    {0x63,0x14,0x08,0x14,0x63}, /* 88 'X' */
    {0x07,0x08,0x70,0x08,0x07}, /* 89 'Y' */
    {0x61,0x51,0x49,0x45,0x43}, /* 90 'Z' */
    {0x00,0x7F,0x41,0x41,0x00}, /* 91 '[' */
    {0x02,0x04,0x08,0x10,0x20}, /* 92 '\' */
    {0x00,0x41,0x41,0x7F,0x00}, /* 93 ']' */
    {0x04,0x02,0x01,0x02,0x04}, /* 94 '^' */
    {0x40,0x40,0x40,0x40,0x40}, /* 95 '_' */
    {0x00,0x01,0x02,0x04,0x00}, /* 96 '`' */
    {0x20,0x54,0x54,0x54,0x78}, /* 97 'a' */
    {0x7F,0x48,0x44,0x44,0x38}, /* 98 'b' */
    {0x38,0x44,0x44,0x44,0x20}, /* 99 'c' */
    {0x38,0x44,0x44,0x48,0x7F}, /* 100 'd' */
    {0x38,0x54,0x54,0x54,0x18}, /* 101 'e' */
    {0x08,0x7E,0x09,0x01,0x02}, /* 102 'f' */
    {0x0C,0x52,0x52,0x52,0x3E}, /* 103 'g' */
    {0x7F,0x08,0x04,0x04,0x78}, /* 104 'h' */
    {0x00,0x44,0x7D,0x40,0x00}, /* 105 'i' */
    {0x20,0x40,0x44,0x3D,0x00}, /* 106 'j' */
    {0x7F,0x10,0x28,0x44,0x00}, /* 107 'k' */
    {0x00,0x41,0x7F,0x40,0x00}, /* 108 'l' */
    {0x7C,0x04,0x18,0x04,0x78}, /* 109 'm' */
    {0x7C,0x08,0x04,0x04,0x78}, /* 110 'n' */
    {0x38,0x44,0x44,0x44,0x38}, /* 111 'o' */
    {0x7C,0x14,0x14,0x14,0x08}, /* 112 'p' */
    {0x08,0x14,0x14,0x18,0x7C}, /* 113 'q' */
    {0x7C,0x08,0x04,0x04,0x08}, /* 114 'r' */
    {0x48,0x54,0x54,0x54,0x20}, /* 115 's' */
    {0x04,0x3F,0x44,0x40,0x20}, /* 116 't' */
    {0x3C,0x40,0x40,0x20,0x7C}, /* 117 'u' */
    {0x1C,0x20,0x40,0x20,0x1C}, /* 118 'v' */
    {0x3C,0x40,0x30,0x40,0x3C}, /* 119 'w' */
    {0x44,0x28,0x10,0x28,0x44}, /* 120 'x' */
    {0x0C,0x50,0x50,0x50,0x3C}, /* 121 'y' */
    {0x44,0x64,0x54,0x4C,0x44}, /* 122 'z' */
    {0x00,0x08,0x36,0x41,0x00}, /* 123 '{' */
    {0x00,0x00,0x7F,0x00,0x00}, /* 124 '|' */
    {0x00,0x41,0x36,0x08,0x00}, /* 125 '}' */
    {0x08,0x04,0x08,0x10,0x08}  /* 126 '~' */
};

/* Decode next UTF-8 codepoint into ASCII (mapping Turkish & common Unicode cleanly) */
static char next_utf8_ascii(const char **sp) {
    const unsigned char *s = (const unsigned char *)*sp;
    if (*s == 0) return 0;
    if (*s < 0x80) {
        *sp += 1;
        return (char)*s;
    }
    uint32_t cp = 0;
    if ((*s & 0xE0) == 0xC0 && s[1]) {
        cp = ((uint32_t)(s[0] & 0x1F) << 6) | (s[1] & 0x3F);
        *sp += 2;
    } else if ((*s & 0xF0) == 0xE0 && s[1] && s[2]) {
        cp = ((uint32_t)(s[0] & 0x0F) << 12) | ((uint32_t)(s[1] & 0x3F) << 6) | (s[2] & 0x3F);
        *sp += 3;
    } else if ((*s & 0xF8) == 0xF0 && s[1] && s[2] && s[3]) {
        *sp += 4;
        return ' ';
    } else {
        *sp += 1;
        return '?';
    }
    switch (cp) {
        case 0x00C7: return 'C'; case 0x00E7: return 'c'; /* Ç ç */
        case 0x011E: return 'G'; case 0x011F: return 'g'; /* Ğ ğ */
        case 0x0130: return 'I'; case 0x0131: return 'i'; /* İ ı */
        case 0x00D6: return 'O'; case 0x00F6: return 'o'; /* Ö ö */
        case 0x015E: return 'S'; case 0x015F: return 's'; /* Ş ş */
        case 0x00DC: return 'U'; case 0x00FC: return 'u'; /* Ü ü */
        case 0x2013: case 0x2014: return '-';
        case 0x2018: case 0x2019: return '\'';
        case 0x201C: case 0x201D: return '"';
        case 0x2022: return '*';
        default: return ' ';
    }
}

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

uint32_t fb_blend_pixel(const FB *fb, uint32_t dst_px,
                        uint8_t sr, uint8_t sg, uint8_t sb, uint8_t alpha) {
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
                row[x] = fb_blend_pixel(fb, row[x], sr, sg, sb, 255);
            } else if (d < rf + 0.5f) {
                uint8_t a = (uint8_t)((rf + 0.5f - d) * 255.0f);
                row[x] = fb_blend_pixel(fb, row[x], sr, sg, sb, a);
            }
        }
    }
    fb_mark_dirty(fb, x0, y0, x1, y1);
}

void fb_draw_button(FB *fb, const ThemePalette *tp, int x, int y, int w, int h,
                    const char *label, bool active, bool danger) {
    uint32_t bg = active ? tp->accent : tp->bar_bg;
    uint32_t fg = active ? 0xFFFFFF : tp->bar_text;
    uint32_t bd = active ? tp->accent : tp->bar_border;
    if (danger) {
        bg = 0xD9383A;
        fg = 0xFFFFFF;
        bd = 0xD9383A;
    }
    fb_fill_rect(fb, x, y, w, h, bg);
    fb_stroke_rect(fb, x, y, w, h, 2, bd);
    int tw = fb_text_width(label, 2);
    fb_draw_text(fb, x + (w - tw) / 2, y + (h - 14) / 2, label, fg, 2);
}

void fb_draw_gear_button(FB *fb, const ThemePalette *tp, int x, int y, int w, int h, bool active) {
    uint32_t bg = active ? tp->accent : tp->bar_bg;
    uint32_t fg = active ? 0xFFFFFF : tp->bar_text;
    uint32_t bd = active ? tp->accent : tp->bar_border;
    fb_fill_rect(fb, x, y, w, h, bg);
    fb_stroke_rect(fb, x, y, w, h, 2, bd);

    int cx = x + w / 2;
    int cy = y + h / 2;
    /* 8 gear teeth around the outer ring */
    static const int8_t dx8[8] = {  0,  8, 11,  8,  0, -8, -11, -8 };
    static const int8_t dy8[8] = { -11, -8, 0,  8, 11,  8,   0, -8 };
    for (int i = 0; i < 8; i++) {
        fb_fill_circle(fb, cx + dx8[i], cy + dy8[i], 3, fg);
    }
    /* Gear body ring & center hole */
    fb_fill_circle(fb, cx, cy, 10, fg);
    fb_fill_circle(fb, cx, cy, 5, bg);
}

void fb_flash_last_button(FB *fb, const ThemePalette *tp) {
    if (fb->last_btn_w <= 0 || fb->last_btn_h <= 0) return;
    int x = fb->last_btn_x, y = fb->last_btn_y, w = fb->last_btn_w, h = fb->last_btn_h;
    int x0 = x < 0 ? 0 : x;
    int y0 = y < 0 ? 0 : y;
    int x1 = x + w - 1;
    int y1 = y + h - 1;
    if (x1 >= fb->w) x1 = fb->w - 1;
    if (y1 >= fb->h) y1 = fb->h - 1;
    if (x0 > x1 || y0 > y1) return;

    /* Invert/highlight existing button pixels & text in-place + draw thick accent border */
    uint32_t acc = fb_pack_hex(fb, tp->accent);
    uint32_t wht = fb_pack_hex(fb, 0xFFFFFF);
    for (int row = y0; row <= y1; row++) {
        uint32_t *dst = fb->canvas + (size_t)row * fb->w;
        bool border_y = (row - y0 < 4) || (y1 - row < 4);
        for (int col = x0; col <= x1; col++) {
            bool border_x = (col - x0 < 4) || (x1 - col < 4);
            if (border_y || border_x) {
                dst[col] = acc;
            } else {
                dst[col] = fb_blend_pixel(fb, dst[col],
                                          (uint8_t)((tp->accent >> 16) & 0xFF),
                                          (uint8_t)((tp->accent >> 8) & 0xFF),
                                          (uint8_t)(tp->accent & 0xFF), 140);
            }
        }
    }
    (void)wht;
    fb_mark_dirty(fb, x0, y0, x1, y1);
    fb_present(fb, false, 0, 0, false, 0);
    if (fb->double_buf) {
        fb_present(fb, false, 0, 0, false, 0);
    }
    usleep(50000);
    fb->last_btn_w = 0;
    fb->last_btn_h = 0;
}

int fb_text_width(const char *s, int scale) {
    int count = 0;
    const char *p = s;
    while (next_utf8_ascii(&p) != 0) count++;
    if (count <= 0) return 0;
    return count * 6 * scale - scale;
}

void fb_draw_text(FB *fb, int x, int y, const char *s, uint32_t hex, int scale) {
    uint32_t px = fb_pack_hex(fb, hex);
    int start_x = x;
    const char *p = s;
    char c;
    while ((c = next_utf8_ascii(&p)) != 0) {
        if (c >= 32 && c <= 126) {
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
