#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <linux/input.h>
#include <math.h>
#include <poll.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#define BAR_H 48

static volatile sig_atomic_t g_quit = 0;

static void on_sig(int sig) {
    (void)sig;
    g_quit = 1;
}

/* ---- Tiny 5x7 bitmap font (ASCII 32..95) ---- */
static const uint8_t FONT5X7[][5] = {
    {0x00,0x00,0x00,0x00,0x00}, // ' '
    {0x00,0x00,0x5F,0x00,0x00}, // '!'
    {0x00,0x07,0x00,0x07,0x00}, // '"'
    {0x14,0x7F,0x14,0x7F,0x14}, // '#'
    {0x24,0x2A,0x7F,0x2A,0x12}, // '$'
    {0x23,0x13,0x08,0x64,0x62}, // '%'
    {0x36,0x49,0x55,0x22,0x50}, // '&'
    {0x00,0x05,0x03,0x00,0x00}, // '''
    {0x00,0x1C,0x22,0x41,0x00}, // '('
    {0x00,0x41,0x22,0x1C,0x00}, // ')'
    {0x14,0x08,0x3E,0x08,0x14}, // '*'
    {0x08,0x08,0x3E,0x08,0x08}, // '+'
    {0x00,0x50,0x30,0x00,0x00}, // ','
    {0x08,0x08,0x08,0x08,0x08}, // '-'
    {0x00,0x60,0x60,0x00,0x00}, // '.'
    {0x20,0x10,0x08,0x04,0x02}, // '/'
    {0x3E,0x51,0x49,0x45,0x3E}, // '0'
    {0x00,0x42,0x7F,0x40,0x00}, // '1'
    {0x42,0x61,0x51,0x49,0x46}, // '2'
    {0x21,0x41,0x45,0x4B,0x31}, // '3'
    {0x18,0x14,0x12,0x7F,0x10}, // '4'
    {0x27,0x45,0x45,0x45,0x39}, // '5'
    {0x3C,0x4A,0x49,0x49,0x30}, // '6'
    {0x01,0x71,0x09,0x05,0x03}, // '7'
    {0x36,0x49,0x49,0x49,0x36}, // '8'
    {0x06,0x49,0x49,0x29,0x1E}, // '9'
    {0x00,0x36,0x36,0x00,0x00}, // ':'
    {0x00,0x56,0x36,0x00,0x00}, // ';'
    {0x08,0x14,0x22,0x41,0x00}, // '<'
    {0x14,0x14,0x14,0x14,0x14}, // '='
    {0x00,0x41,0x22,0x14,0x08}, // '>'
    {0x02,0x01,0x51,0x09,0x06}, // '?'
    {0x32,0x49,0x79,0x41,0x3E}, // '@'
    {0x7E,0x11,0x11,0x11,0x7E}, // 'A'
    {0x7F,0x49,0x49,0x49,0x36}, // 'B'
    {0x3E,0x41,0x41,0x41,0x22}, // 'C'
    {0x7F,0x41,0x41,0x22,0x1C}, // 'D'
    {0x7F,0x49,0x49,0x49,0x41}, // 'E'
    {0x7F,0x09,0x09,0x09,0x01}, // 'F'
    {0x3E,0x41,0x49,0x49,0x7A}, // 'G'
    {0x7F,0x08,0x08,0x08,0x7F}, // 'H'
    {0x00,0x41,0x7F,0x41,0x00}, // 'I'
    {0x20,0x40,0x41,0x3F,0x01}, // 'J'
    {0x7F,0x08,0x14,0x22,0x41}, // 'K'
    {0x7F,0x40,0x40,0x40,0x40}, // 'L'
    {0x7F,0x02,0x0C,0x02,0x7F}, // 'M'
    {0x7F,0x04,0x08,0x10,0x7F}, // 'N'
    {0x3E,0x41,0x41,0x41,0x3E}, // 'O'
    {0x7F,0x09,0x09,0x09,0x06}, // 'P'
    {0x3E,0x41,0x51,0x21,0x5E}, // 'Q'
    {0x7F,0x09,0x19,0x29,0x46}, // 'R'
    {0x46,0x49,0x49,0x49,0x31}, // 'S'
    {0x01,0x01,0x7F,0x01,0x01}, // 'T'
    {0x3F,0x40,0x40,0x40,0x3F}, // 'U'
    {0x1F,0x20,0x40,0x20,0x1F}, // 'V'
    {0x3F,0x40,0x38,0x40,0x3F}, // 'W'
    {0x63,0x14,0x08,0x14,0x63}, // 'X'
    {0x07,0x08,0x70,0x08,0x07}, // 'Y'
    {0x61,0x51,0x49,0x45,0x43}, // 'Z'
    {0x00,0x7F,0x41,0x41,0x00}, // '['
    {0x02,0x04,0x08,0x10,0x20}, // '\'
    {0x00,0x41,0x41,0x7F,0x00}, // ']'
    {0x04,0x02,0x01,0x02,0x04}, // '^'
    {0x40,0x40,0x40,0x40,0x40}, // '_'
};

typedef struct {
    int fd;
    struct fb_fix_screeninfo finfo;
    struct fb_var_screeninfo vinfo;
    uint8_t *fb_mem;
    size_t fb_size;
    uint32_t *canvas;
    int w, h;
    int stride_px;
    bool double_buf;
    int back_page;
} FB;

typedef struct {
    int x0, y0, x1, y1;
    bool valid;
} Rect;

static inline void rect_reset(Rect *r) {
    r->valid = false;
}

static inline void rect_expand(Rect *r, int x0, int y0, int x1, int y1, int w, int h) {
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

static inline uint32_t pack_rgb(const FB *fb, uint8_t r, uint8_t g, uint8_t b) {
    uint32_t px = ((uint32_t)r << fb->vinfo.red.offset)
                | ((uint32_t)g << fb->vinfo.green.offset)
                | ((uint32_t)b << fb->vinfo.blue.offset);
    if (fb->vinfo.transp.length)
        px |= (0xFFu << fb->vinfo.transp.offset);
    return px;
}

static int read_int_file(const char *path, int fallback) {
    FILE *f = fopen(path, "r");
    if (!f) return fallback;
    int v = fallback;
    if (fscanf(f, "%d", &v) != 1) v = fallback;
    fclose(f);
    return v;
}

static void ensure_backlight(void) {
    const char *path = "/sys/class/leds/lcd-backlight/brightness";
    int cur = read_int_file(path, -1);
    if (cur >= 0 && cur < 40) {
        FILE *f = fopen(path, "w");
        if (f) {
            fprintf(f, "180\n");
            fclose(f);
        }
    }
}

static void draw_char(FB *fb, int x, int y, char c, uint32_t color, int scale) {
    if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
    if (c == '|') {
        for (int dy = 0; dy < 7 * scale; dy++) {
            for (int dx = 0; dx < scale; dx++) {
                int px = x + 2 * scale + dx;
                int py = y + dy;
                if (px >= 0 && px < fb->w && py >= 0 && py < fb->h)
                    fb->canvas[py * fb->w + px] = color;
            }
        }
        return;
    }
    if (c < 32 || c > 95) return;
    const uint8_t *glyph = FONT5X7[c - 32];
    for (int col = 0; col < 5; col++) {
        uint8_t bits = glyph[col];
        for (int row = 0; row < 7; row++) {
            if (bits & (1u << row)) {
                for (int sy = 0; sy < scale; sy++) {
                    int py = y + row * scale + sy;
                    if (py < 0 || py >= fb->h) continue;
                    for (int sx = 0; sx < scale; sx++) {
                        int px = x + col * scale + sx;
                        if (px >= 0 && px < fb->w)
                            fb->canvas[py * fb->w + px] = color;
                    }
                }
            }
        }
    }
}

static void draw_text(FB *fb, int x, int y, const char *s, uint32_t color, int scale) {
    for (; *s; s++) {
        draw_char(fb, x, y, *s, color, scale);
        x += 6 * scale;
    }
}

static void draw_status_bar(FB *fb, const char *mode_str, int pressure, Rect *dirty) {
    uint32_t bar_bg = pack_rgb(fb, 30, 34, 42);
    uint32_t bar_fg = pack_rgb(fb, 240, 242, 245);
    uint32_t bar_line = pack_rgb(fb, 70, 78, 92);

    for (int y = 0; y < BAR_H; y++) {
        uint32_t c = (y == BAR_H - 1) ? bar_line : bar_bg;
        uint32_t *row = fb->canvas + y * fb->w;
        for (int x = 0; x < fb->w; x++) row[x] = c;
    }

    int bat = read_int_file("/sys/class/power_supply/battery/capacity", -1);
    char msg[192];
    snprintf(msg, sizeof(msg),
             "E-TAB 5 BARE NOTE OS  |  BAT: %d%%  |  MODE: %s  |  P: %4d  |  VOL+: CLEAR  |  VOL-: EXIT",
             bat, mode_str, pressure);
    draw_text(fb, 20, 14, msg, bar_fg, 3);
    rect_expand(dirty, 0, 0, fb->w - 1, BAR_H - 1, fb->w, fb->h);
}

static void clear_paper(FB *fb, Rect *dirty) {
    uint32_t paper = pack_rgb(fb, 247, 245, 239);
    uint32_t rule  = pack_rgb(fb, 224, 226, 232);
    uint32_t margin = pack_rgb(fb, 238, 198, 198);
    uint32_t hint = pack_rgb(fb, 180, 180, 185);

    for (int y = BAR_H; y < fb->h; y++) {
        uint32_t *row = fb->canvas + y * fb->w;
        bool is_rule = ((y - BAR_H) % 48 == 47);
        uint32_t c = is_rule ? rule : paper;
        for (int x = 0; x < fb->w; x++) row[x] = c;
        if (fb->w > 120) row[120] = margin;
    }

    draw_text(fb, 16, BAR_H + 12, "TL", hint, 2);
    draw_text(fb, fb->w - 50, BAR_H + 12, "TR", hint, 2);
    draw_text(fb, 16, fb->h - 28, "BL", hint, 2);
    draw_text(fb, fb->w - 50, fb->h - 28, "BR", hint, 2);

    draw_status_bar(fb, "PEN", 0, dirty);
    rect_expand(dirty, 0, 0, fb->w - 1, fb->h - 1, fb->w, fb->h);
}

static uint32_t paper_pixel_at(const FB *fb, int x, int y) {
    if (y < BAR_H) return pack_rgb(fb, 30, 34, 42);
    if (x == 120) return pack_rgb(fb, 238, 198, 198);
    if ((y - BAR_H) % 48 == 47) return pack_rgb(fb, 224, 226, 232);
    return pack_rgb(fb, 247, 245, 239);
}

static void draw_disc(FB *fb, float cx, float cy, float r, bool erase, uint32_t ink, Rect *dirty) {
    int ir = (int)ceilf(r);
    int x0 = (int)floorf(cx) - ir;
    int y0 = (int)floorf(cy) - ir;
    int x1 = (int)ceilf(cx) + ir;
    int y1 = (int)ceilf(cy) + ir;
    if (x0 < 0) x0 = 0;
    if (y0 < BAR_H) y0 = BAR_H;
    if (x1 >= fb->w) x1 = fb->w - 1;
    if (y1 >= fb->h) y1 = fb->h - 1;
    if (x0 > x1 || y0 > y1) return;

    float r2 = r * r;
    for (int y = y0; y <= y1; y++) {
        float dy = (float)y - cy;
        uint32_t *row = fb->canvas + y * fb->w;
        for (int x = x0; x <= x1; x++) {
            float dx = (float)x - cx;
            if (dx * dx + dy * dy <= r2) {
                row[x] = erase ? paper_pixel_at(fb, x, y) : ink;
            }
        }
    }
    rect_expand(dirty, x0, y0, x1, y1, fb->w, fb->h);
}

static void draw_stroke_seg(FB *fb, float x0, float y0, float r0,
                            float x1, float y1, float r1,
                            bool erase, uint32_t ink, Rect *dirty) {
    float dx = x1 - x0;
    float dy = y1 - y0;
    float dist = sqrtf(dx * dx + dy * dy);
    float step = fmaxf(0.6f, fminf(r0, r1) * 0.35f);
    int steps = (int)ceilf(dist / step);
    if (steps < 1) steps = 1;
    for (int i = 0; i <= steps; i++) {
        float t = (float)i / (float)steps;
        float x = x0 + dx * t;
        float y = y0 + dy * t;
        float r = r0 + (r1 - r0) * t;
        draw_disc(fb, x, y, r, erase, ink, dirty);
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

static void draw_cursor_on_page(FB *fb, int page, int cx, int cy, bool erase) {
    if (cy < BAR_H || cy >= fb->h || cx < 0 || cx >= fb->w) return;
    uint32_t c = erase ? pack_rgb(fb, 200, 60, 60) : pack_rgb(fb, 40, 110, 220);
    int rad = erase ? 14 : 6;
    int y_off = page * fb->h;
    for (int dy = -rad; dy <= rad; dy++) {
        int y = cy + dy;
        if (y < BAR_H || y >= fb->h) continue;
        uint32_t *row = (uint32_t *)(fb->fb_mem + (size_t)(y + y_off) * fb->finfo.line_length);
        for (int dx = -rad; dx <= rad; dx++) {
            int x = cx + dx;
            if (x < 0 || x >= fb->w) continue;
            int d2 = dx * dx + dy * dy;
            if (d2 <= rad * rad && d2 >= (rad - 2) * (rad - 2)) {
                row[x] = c;
            }
        }
    }
}

static void fb_present(FB *fb, Rect *dirty_pages[2], bool hover, int hx, int hy, bool erase,
                       Rect *prev_cursor_pages[2]) {
    int page = fb->double_buf ? fb->back_page : 0;

    /* Restore previous cursor area on this page from canvas */
    if (prev_cursor_pages[page]->valid) {
        blit_rect_to_page(fb, page, prev_cursor_pages[page]);
        rect_reset(prev_cursor_pages[page]);
    }

    /* Blit canvas changes accumulated for this page */
    if (dirty_pages[page]->valid) {
        blit_rect_to_page(fb, page, dirty_pages[page]);
        rect_reset(dirty_pages[page]);
    }

    /* Draw hover cursor if active */
    if (hover && hy >= BAR_H) {
        draw_cursor_on_page(fb, page, hx, hy, erase);
        int rad = erase ? 16 : 8;
        rect_expand(prev_cursor_pages[page], hx - rad, hy - rad, hx + rad, hy + rad, fb->w, fb->h);
    }

    fb->vinfo.xoffset = 0;
    fb->vinfo.yoffset = (uint32_t)(page * fb->h);
    ioctl(fb->fd, FBIOPAN_DISPLAY, &fb->vinfo);

    if (fb->double_buf) {
        fb->back_page ^= 1;
    }
}

int main(void) {
    signal(SIGINT, on_sig);
    signal(SIGTERM, on_sig);

    FB fb;
    memset(&fb, 0, sizeof(fb));
    fb.fd = open("/dev/graphics/fb0", O_RDWR);
    if (fb.fd < 0) fb.fd = open("/dev/fb0", O_RDWR);
    if (fb.fd < 0) {
        perror("open fb0");
        return 1;
    }

    if (ioctl(fb.fd, FBIOGET_FSCREENINFO, &fb.finfo) < 0 ||
        ioctl(fb.fd, FBIOGET_VSCREENINFO, &fb.vinfo) < 0) {
        perror("fb ioctl");
        return 1;
    }

    ioctl(fb.fd, FBIOBLANK, FB_BLANK_UNBLANK);
    ensure_backlight();

    fb.w = (int)fb.vinfo.xres;
    fb.h = (int)fb.vinfo.yres;
    fb.stride_px = (int)(fb.finfo.line_length / 4);
    fb.double_buf = (fb.vinfo.yres_virtual >= fb.vinfo.yres * 2);
    fb.fb_size = fb.finfo.smem_len;

    fprintf(stderr,
            "FB: %dx%d (virt %dx%d) bpp=%u line_len=%u smem=%u R=%u/%u G=%u/%u B=%u/%u A=%u/%u double=%d\n",
            fb.w, fb.h, fb.vinfo.xres_virtual, fb.vinfo.yres_virtual,
            fb.vinfo.bits_per_pixel, fb.finfo.line_length, fb.finfo.smem_len,
            fb.vinfo.red.offset, fb.vinfo.red.length,
            fb.vinfo.green.offset, fb.vinfo.green.length,
            fb.vinfo.blue.offset, fb.vinfo.blue.length,
            fb.vinfo.transp.offset, fb.vinfo.transp.length,
            fb.double_buf ? 1 : 0);

    if (fb.vinfo.bits_per_pixel != 32) {
        fprintf(stderr, "Unsupported bpp: %u\n", fb.vinfo.bits_per_pixel);
        return 1;
    }

    fb.fb_mem = mmap(NULL, fb.fb_size, PROT_READ | PROT_WRITE, MAP_SHARED, fb.fd, 0);
    if (fb.fb_mem == MAP_FAILED) {
        perror("mmap fb0");
        return 1;
    }

    fb.canvas = malloc((size_t)fb.w * (size_t)fb.h * sizeof(uint32_t));
    if (!fb.canvas) {
        perror("malloc canvas");
        return 1;
    }

    int fd_pen = open("/dev/input/event1", O_RDONLY | O_NONBLOCK);
    int fd_key = open("/dev/input/event0", O_RDONLY | O_NONBLOCK);
    if (fd_pen < 0) {
        perror("open /dev/input/event1");
        return 1;
    }

    struct input_absinfo abs_x = {0}, abs_y = {0}, abs_p = {0};
    ioctl(fd_pen, EVIOCGABS(ABS_X), &abs_x);
    ioctl(fd_pen, EVIOCGABS(ABS_Y), &abs_y);
    ioctl(fd_pen, EVIOCGABS(ABS_PRESSURE), &abs_p);
    int max_x = abs_x.maximum > 0 ? abs_x.maximum : 21696;
    int max_y = abs_y.maximum > 0 ? abs_y.maximum : 13560;
    int max_p = abs_p.maximum > 0 ? abs_p.maximum : 1023;
    fprintf(stderr, "WACOM: X=0..%d Y=0..%d P=0..%d\n", max_x, max_y, max_p);

    Rect dirty0, dirty1, cur0, cur1;
    rect_reset(&dirty0); rect_reset(&dirty1);
    rect_reset(&cur0);   rect_reset(&cur1);
    Rect *dirty_pages[2] = { &dirty0, &dirty1 };
    Rect *cur_pages[2]   = { &cur0,   &cur1   };

    Rect tmp_dirty;
    rect_reset(&tmp_dirty);
    clear_paper(&fb, &tmp_dirty);
    rect_expand(&dirty0, 0, 0, fb.w - 1, fb.h - 1, fb.w, fb.h);
    rect_expand(&dirty1, 0, 0, fb.w - 1, fb.h - 1, fb.w, fb.h);
    fb_present(&fb, dirty_pages, false, 0, 0, false, cur_pages);
    if (fb.double_buf) {
        fb_present(&fb, dirty_pages, false, 0, 0, false, cur_pages);
    }

    uint32_t ink_color = pack_rgb(&fb, 24, 28, 38);

    int raw_x = 0, raw_y = 0, raw_p = 0;
    bool tool_pen = false, tool_rubber = false, btn_stylus = false, touching = false;
    bool have_prev = false;
    float prev_x = 0, prev_y = 0, prev_r = 2.0f;
    int log_samples = 0;
    int frame_counter = 0;

    struct pollfd pfds[2];
    pfds[0].fd = fd_pen;
    pfds[0].events = POLLIN;
    pfds[1].fd = fd_key;
    pfds[1].events = POLLIN;
    nfds_t nfds = (fd_key >= 0) ? 2 : 1;

    while (!g_quit) {
        int pr = poll(pfds, nfds, 250);
        if (pr < 0) {
            if (errno == EINTR) continue;
            break;
        }
        bool need_present = false;
        rect_reset(&tmp_dirty);

        if (pfds[0].revents & POLLIN) {
            struct input_event ev;
            while (read(fd_pen, &ev, sizeof(ev)) == (ssize_t)sizeof(ev)) {
                if (ev.type == EV_ABS) {
                    if (ev.code == ABS_X) raw_x = ev.value;
                    else if (ev.code == ABS_Y) raw_y = ev.value;
                    else if (ev.code == ABS_PRESSURE) raw_p = ev.value;
                } else if (ev.type == EV_KEY) {
                    if (ev.code == BTN_TOOL_PEN) {
                        tool_pen = (ev.value != 0);
                        if (!tool_pen) have_prev = false;
                    } else if (ev.code == BTN_TOOL_RUBBER) {
                        tool_rubber = (ev.value != 0);
                        if (!tool_rubber) have_prev = false;
                    } else if (ev.code == BTN_STYLUS || ev.code == BTN_STYLUS2) {
                        btn_stylus = (ev.value != 0);
                    } else if (ev.code == BTN_TOUCH) {
                        touching = (ev.value != 0);
                        if (!touching) have_prev = false;
                    }
                } else if (ev.type == EV_SYN && ev.code == SYN_REPORT) {
                    float sx = (float)raw_x * (float)(fb.w - 1) / (float)max_x;
                    float sy = (float)raw_y * (float)(fb.h - 1) / (float)max_y;
                    bool erase = tool_rubber || btn_stylus;
                    bool down = touching || raw_p > 0;

                    if (log_samples < 10 && (down || tool_pen || tool_rubber)) {
                        fprintf(stderr, "SAMPLE: raw=(%d,%d) p=%d -> screen=(%.1f,%.1f) pen=%d rub=%d btn=%d\n",
                                raw_x, raw_y, raw_p, sx, sy, tool_pen, tool_rubber, btn_stylus);
                        log_samples++;
                    }

                    if (down && sy >= BAR_H) {
                        float pf = (float)raw_p / (float)max_p;
                        if (pf < 0.0f) pf = 0.0f;
                        if (pf > 1.0f) pf = 1.0f;
                        float rad = erase ? (12.0f + pf * 16.0f) : (1.2f + pf * 4.5f);
                        if (have_prev) {
                            draw_stroke_seg(&fb, prev_x, prev_y, prev_r, sx, sy, rad,
                                            erase, ink_color, &tmp_dirty);
                        } else {
                            draw_disc(&fb, sx, sy, rad, erase, ink_color, &tmp_dirty);
                        }
                        prev_x = sx; prev_y = sy; prev_r = rad;
                        have_prev = true;
                    } else {
                        have_prev = false;
                    }

                    frame_counter++;
                    if ((frame_counter & 7) == 0) {
                        draw_status_bar(&fb, erase ? "ERASER" : "PEN", raw_p, &tmp_dirty);
                    }
                    need_present = true;
                }
            }
        }

        if (nfds == 2 && (pfds[1].revents & POLLIN)) {
            struct input_event ev;
            while (read(fd_key, &ev, sizeof(ev)) == (ssize_t)sizeof(ev)) {
                if (ev.type == EV_KEY && ev.value == 1) {
                    if (ev.code == KEY_VOLUMEUP) {
                        clear_paper(&fb, &tmp_dirty);
                        need_present = true;
                    } else if (ev.code == KEY_VOLUMEDOWN || ev.code == KEY_POWER) {
                        g_quit = 1;
                    }
                }
            }
        }

        if (tmp_dirty.valid) {
            rect_expand(&dirty0, tmp_dirty.x0, tmp_dirty.y0, tmp_dirty.x1, tmp_dirty.y1, fb.w, fb.h);
            rect_expand(&dirty1, tmp_dirty.x0, tmp_dirty.y0, tmp_dirty.x1, tmp_dirty.y1, fb.w, fb.h);
        }

        if (need_present) {
            int hx = (int)((float)raw_x * (float)(fb.w - 1) / (float)max_x);
            int hy = (int)((float)raw_y * (float)(fb.h - 1) / (float)max_y);
            bool hover = (tool_pen || tool_rubber) && !(touching || raw_p > 0);
            bool erase = tool_rubber || btn_stylus;
            fb_present(&fb, dirty_pages, hover, hx, hy, erase, cur_pages);
        }
    }

    free(fb.canvas);
    munmap(fb.fb_mem, fb.fb_size);
    close(fd_pen);
    if (fd_key >= 0) close(fd_key);
    close(fb.fd);
    return 0;
}
