#define _GNU_SOURCE
#include "osk.h"
#include "fb.h"

#include <stdio.h>
#include <string.h>

static const char *ROW0_ALPHA = "1234567890";
static const char *ROW1_ALPHA = "QWERTYUIOP";
static const char *ROW2_ALPHA = "ASDFGHJKL";
static const char *ROW3_ALPHA = "ZXCVBNM";

static const char *ROW0_SYM = "!@#$%^&*()";
static const char *ROW1_SYM = "-_=+[]{};:";
static const char *ROW2_SYM = "'\",.<>/?\\";
static const char *ROW3_SYM = "|~`!@#?";

static bool hit_rect(int px, int py, int x, int y, int w, int h) {
    return px >= x && px < x + w && py >= y && py < y + h;
}

void osk_open(OskState *osk, OskPurpose purpose, const char *prompt,
              const char *initial, const char *target_ssid) {
    memset(osk, 0, sizeof(*osk));
    osk->open = true;
    osk->purpose = purpose;
    snprintf(osk->prompt, sizeof(osk->prompt), "%s", prompt ? prompt : "ENTER TEXT");
    if (target_ssid) snprintf(osk->target_ssid, sizeof(osk->target_ssid), "%s", target_ssid);
    if (initial && initial[0]) {
        snprintf(osk->buf, sizeof(osk->buf), "%s", initial);
        osk->len = (int)strlen(osk->buf);
    }
}

static void render_textbox(FB *fb, const ThemePalette *tp, const OskState *osk) {
    int bx = 180, by = 220, bw = fb->w - 360;
    fb_fill_rect(fb, bx + 28, by + 62, bw - 56, 62, tp->bg);
    fb_stroke_rect(fb, bx + 28, by + 62, bw - 56, 62, 2, tp->bar_border);
    char disp[140];
    snprintf(disp, sizeof(disp), "%s_", osk->buf);
    fb_draw_text(fb, bx + 44, by + 84, disp, tp->bar_text, 2);
}

void osk_render_full(FB *fb, ThemeKind theme, const OskState *osk) {
    if (!osk->open) return;
    const ThemePalette *tp = &THEMES[theme];
    int bx = 180, by = 220, bw = fb->w - 360, bh = 740;
    fb_fill_rect(fb, bx, by, bw, bh, tp->sidebar_bg);
    fb_stroke_rect(fb, bx, by, bw, bh, 3, tp->accent);

    fb_draw_text(fb, bx + 28, by + 24, osk->prompt, tp->accent, 2);
    render_textbox(fb, tp, osk);

    const char *r0 = osk->symbols ? ROW0_SYM : ROW0_ALPHA;
    const char *r1 = osk->symbols ? ROW1_SYM : ROW1_ALPHA;
    const char *r2 = osk->symbols ? ROW2_SYM : ROW2_ALPHA;
    const char *r3 = osk->symbols ? ROW3_SYM : ROW3_ALPHA;

    int kw = 136, kh = 92, gap = 12;
    int start_y = by + 150;
    int rx0 = bx + (bw - (10 * kw + 9 * gap)) / 2;
    int rx2 = bx + (bw - (9 * kw + 8 * gap)) / 2;

    for (int i = 0; i < 10; i++) {
        char lbl[2] = { r0[i], '\0' };
        fb_draw_button(fb, tp, rx0 + i * (kw + gap), start_y, kw, kh, lbl, false, false);
    }

    int ry1 = start_y + kh + gap;
    for (int i = 0; i < 10; i++) {
        char lbl[2] = { r1[i], '\0' };
        fb_draw_button(fb, tp, rx0 + i * (kw + gap), ry1, kw, kh, lbl, false, false);
    }

    int ry2 = ry1 + kh + gap;
    for (int i = 0; i < 9; i++) {
        char lbl[2] = { r2[i], '\0' };
        fb_draw_button(fb, tp, rx2 + i * (kw + gap), ry2, kw, kh, lbl, false, false);
    }

    int ry3 = ry2 + kh + gap;
    fb_draw_button(fb, tp, rx0, ry3, kw + 40, kh, osk->shift ? "CAPS" : "SHIFT", osk->shift, false);
    int k3_start = rx0 + kw + 40 + gap;
    for (int i = 0; i < 7; i++) {
        char lbl[2] = { r3[i], '\0' };
        fb_draw_button(fb, tp, k3_start + i * (kw + gap), ry3, kw, kh, lbl, false, false);
    }
    int bksp_x = k3_start + 7 * (kw + gap);
    fb_draw_button(fb, tp, bksp_x, ry3, rx0 + 10 * kw + 9 * gap - bksp_x, kh, "BKSP", false, false);

    int ry4 = ry3 + kh + gap + 8;
    fb_draw_button(fb, tp, rx0, ry4, 150, kh, osk->symbols ? "ABC" : "!@#", osk->symbols, false);
    fb_draw_button(fb, tp, rx0 + 162, ry4, 140, kh, "CLR", false, false);
    fb_draw_button(fb, tp, rx0 + 314, ry4, 590, kh, "SPACE", false, false);
    fb_draw_button(fb, tp, rx0 + 918, ry4, 260, kh, "CANCEL", false, false);
    fb_draw_button(fb, tp, rx0 + 1192, ry4, rx0 + 10 * kw + 9 * gap - (rx0 + 1192), kh, "OK", true, false);
}

static void set_highlight(OskState *osk, int x, int y, int w, int h, const char *lbl) {
    osk->hi_x = x; osk->hi_y = y; osk->hi_w = w; osk->hi_h = h;
    snprintf(osk->hi_label, sizeof(osk->hi_label), "%s", lbl);
    osk->hi_active = true;
}

void osk_render_fast(FB *fb, ThemeKind theme, OskState *osk) {
    if (!osk->open) return;
    if (osk->need_full_keys) {
        osk->need_full_keys = false;
        osk_render_full(fb, theme, osk);
        return;
    }
    const ThemePalette *tp = &THEMES[theme];
    render_textbox(fb, tp, osk);
    if (osk->hi_active) {
        fb_draw_button(fb, tp, osk->hi_x, osk->hi_y, osk->hi_w, osk->hi_h,
                       osk->hi_label, true, false);
    }
}

void osk_release(FB *fb, ThemeKind theme, OskState *osk) {
    if (!osk->open || !osk->hi_active) return;
    const ThemePalette *tp = &THEMES[theme];
    fb_draw_button(fb, tp, osk->hi_x, osk->hi_y, osk->hi_w, osk->hi_h,
                   osk->hi_label, false, false);
    osk->hi_active = false;
}

SettingsAction osk_handle_tap(FB *fb, OskState *osk, int x, int y,
                              OskPurpose *out_purpose, char *out_text, size_t out_sz,
                              char *out_ssid, size_t ssid_sz) {
    int bx = 180, by = 220, bw = fb->w - 360;
    int kw = 136, kh = 92, gap = 12;
    int start_y = by + 150;
    int rx0 = bx + (bw - (10 * kw + 9 * gap)) / 2;
    int rx2 = bx + (bw - (9 * kw + 8 * gap)) / 2;

    const char *r0 = osk->symbols ? ROW0_SYM : ROW0_ALPHA;
    const char *r1 = osk->symbols ? ROW1_SYM : ROW1_ALPHA;
    const char *r2 = osk->symbols ? ROW2_SYM : ROW2_ALPHA;
    const char *r3 = osk->symbols ? ROW3_SYM : ROW3_ALPHA;

    char ch = 0;
    int hx = 0, hy = 0;
    int ry1 = start_y + kh + gap;
    int ry2 = ry1 + kh + gap;
    int ry3 = ry2 + kh + gap;
    int ry4 = ry3 + kh + gap + 8;

    for (int i = 0; i < 10; i++) {
        int kx = rx0 + i * (kw + gap);
        if (hit_rect(x, y, kx, start_y, kw, kh)) { ch = r0[i]; hx = kx; hy = start_y; }
        if (hit_rect(x, y, kx, ry1, kw, kh))     { ch = r1[i]; hx = kx; hy = ry1; }
    }
    for (int i = 0; i < 9; i++) {
        int kx = rx2 + i * (kw + gap);
        if (hit_rect(x, y, kx, ry2, kw, kh))     { ch = r2[i]; hx = kx; hy = ry2; }
    }
    int k3_start = rx0 + kw + 40 + gap;
    for (int i = 0; i < 7; i++) {
        int kx = k3_start + i * (kw + gap);
        if (hit_rect(x, y, kx, ry3, kw, kh))     { ch = r3[i]; hx = kx; hy = ry3; }
    }

    if (ch) {
        char lbl[2] = { ch, '\0' };
        set_highlight(osk, hx, hy, kw, kh, lbl);
        if (!osk->symbols && !osk->shift && ch >= 'A' && ch <= 'Z') {
            ch = (char)(ch - 'A' + 'a');
        }
        if (osk->len + 1 < (int)sizeof(osk->buf)) {
            osk->buf[osk->len++] = ch;
            osk->buf[osk->len] = '\0';
        }
        return SET_ACT_OSK_FAST;
    }

    if (hit_rect(x, y, rx0, ry3, kw + 40, kh)) {
        osk->shift = !osk->shift;
        osk->hi_active = false;
        osk->need_full_keys = true;
        return SET_ACT_OSK_FAST;
    }
    int bksp_x = k3_start + 7 * (kw + gap);
    int bksp_w = rx0 + 10 * kw + 9 * gap - bksp_x;
    if (hit_rect(x, y, bksp_x, ry3, bksp_w, kh)) {
        set_highlight(osk, bksp_x, ry3, bksp_w, kh, "BKSP");
        if (osk->len > 0) osk->buf[--osk->len] = '\0';
        return SET_ACT_OSK_FAST;
    }
    if (hit_rect(x, y, rx0, ry4, 150, kh)) {
        osk->symbols = !osk->symbols;
        osk->hi_active = false;
        osk->need_full_keys = true;
        return SET_ACT_OSK_FAST;
    }
    if (hit_rect(x, y, rx0 + 162, ry4, 140, kh)) {
        set_highlight(osk, rx0 + 162, ry4, 140, kh, "CLR");
        osk->len = 0;
        osk->buf[0] = '\0';
        return SET_ACT_OSK_FAST;
    }
    if (hit_rect(x, y, rx0 + 314, ry4, 590, kh)) {
        set_highlight(osk, rx0 + 314, ry4, 590, kh, "SPACE");
        if (osk->len + 1 < (int)sizeof(osk->buf)) {
            osk->buf[osk->len++] = ' ';
            osk->buf[osk->len] = '\0';
        }
        return SET_ACT_OSK_FAST;
    }
    if (hit_rect(x, y, rx0 + 918, ry4, 260, kh)) {
        osk->open = false;
        osk->hi_active = false;
        return SET_ACT_REDRAW;
    }
    int ok_w = rx0 + 10 * kw + 9 * gap - (rx0 + 1192);
    if (hit_rect(x, y, rx0 + 1192, ry4, ok_w, kh)) {
        if (out_purpose) *out_purpose = osk->purpose;
        if (out_text && out_sz > 0) snprintf(out_text, out_sz, "%s", osk->buf);
        if (out_ssid && ssid_sz > 0) snprintf(out_ssid, ssid_sz, "%s", osk->target_ssid);
        osk->open = false;
        osk->hi_active = false;
        return SET_ACT_OSK_DONE;
    }
    return SET_ACT_NONE;
}
