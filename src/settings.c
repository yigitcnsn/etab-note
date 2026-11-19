#define _GNU_SOURCE
#include "noteos.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/statvfs.h>
#include <sys/wait.h>
#include <unistd.h>

#define WPA_CLI_PATH "/system/bin/wpa_cli"
#define WPA_SOCK_DIR "/data/misc/wifi/sockets"

static bool hit_r(int px, int py, int x, int y, int w, int h) {
    return px >= x && px < x + w && py >= y && py < y + h;
}

static int read_int_file(const char *path, int fallback) {
    FILE *f = fopen(path, "r");
    if (!f) return fallback;
    int v = fallback;
    if (fscanf(f, "%d", &v) != 1) v = fallback;
    fclose(f);
    return v;
}

static void read_str_file(const char *path, char *out, size_t sz, const char *fallback) {
    FILE *f = fopen(path, "r");
    if (!f) {
        snprintf(out, sz, "%s", fallback);
        return;
    }
    if (!fgets(out, (int)sz, f)) {
        snprintf(out, sz, "%s", fallback);
    } else {
        size_t n = strlen(out);
        while (n > 0 && (out[n - 1] == '\n' || out[n - 1] == '\r')) out[--n] = '\0';
    }
    fclose(f);
}

/* Safe exec without shell — captures stdout into buf */
static int run_cmd_capture(char *const argv[], char *out, size_t out_sz) {
    int pipefd[2];
    if (pipe(pipefd) != 0) return -1;
    pid_t pid = fork();
    if (pid < 0) {
        close(pipefd[0]);
        close(pipefd[1]);
        return -1;
    }
    if (pid == 0) {
        close(pipefd[0]);
        dup2(pipefd[1], STDOUT_FILENO);
        dup2(pipefd[1], STDERR_FILENO);
        close(pipefd[1]);
        execv(argv[0], argv);
        _exit(127);
    }
    close(pipefd[1]);
    size_t total = 0;
    if (out && out_sz > 0) {
        ssize_t n;
        while (total + 1 < out_sz &&
               (n = read(pipefd[0], out + total, out_sz - 1 - total)) > 0) {
            total += (size_t)n;
        }
        out[total] = '\0';
    }
    close(pipefd[0]);
    int status = 0;
    waitpid(pid, &status, 0);
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

static int wpa_cmd(const char *a1, const char *a2, const char *a3, const char *a4,
                   char *out, size_t out_sz) {
    char *argv[10];
    int i = 0;
    argv[i++] = (char *)WPA_CLI_PATH;
    argv[i++] = (char *)"-p";
    argv[i++] = (char *)WPA_SOCK_DIR;
    argv[i++] = (char *)"-i";
    argv[i++] = (char *)"wlan0";
    if (a1) argv[i++] = (char *)a1;
    if (a2) argv[i++] = (char *)a2;
    if (a3) argv[i++] = (char *)a3;
    if (a4) argv[i++] = (char *)a4;
    argv[i] = NULL;
    return run_cmd_capture(argv, out, out_sz);
}

static void parse_ip_from_sys(char *ip_out, size_t sz) {
    char buf[1024] = {0};
    char *argv[] = { (char *)"/system/bin/ip", (char *)"-4", (char *)"addr", (char *)"show", (char *)"wlan0", NULL };
    if (run_cmd_capture(argv, buf, sizeof(buf)) == 0) {
        char *p = strstr(buf, "inet ");
        if (p) {
            p += 5;
            size_t i = 0;
            while (*p && *p != '/' && *p != ' ' && i + 1 < sz) ip_out[i++] = *p++;
            ip_out[i] = '\0';
            return;
        }
    }
    snprintf(ip_out, sz, "NONE");
}

void settings_refresh(SettingsState *st) {
    snprintf(st->ssid, sizeof(st->ssid), "NOT CONNECTED");
    snprintf(st->wpa_state, sizeof(st->wpa_state), "OFFLINE");
    snprintf(st->ip, sizeof(st->ip), "NONE");

    char out[2048] = {0};
    if (wpa_cmd("status", NULL, NULL, NULL, out, sizeof(out)) == 0) {
        char *saveptr = NULL;
        for (char *line = strtok_r(out, "\n", &saveptr); line; line = strtok_r(NULL, "\n", &saveptr)) {
            if (strncmp(line, "ssid=", 5) == 0) {
                snprintf(st->ssid, sizeof(st->ssid), "%s", line + 5);
            } else if (strncmp(line, "wpa_state=", 10) == 0) {
                snprintf(st->wpa_state, sizeof(st->wpa_state), "%s", line + 10);
            } else if (strncmp(line, "ip_address=", 11) == 0) {
                snprintf(st->ip, sizeof(st->ip), "%s", line + 11);
            }
        }
    }
    if (strcmp(st->ip, "NONE") == 0) {
        parse_ip_from_sys(st->ip, sizeof(st->ip));
    }
}

static int cmp_wifi(const void *a, const void *b) {
    const WifiNet *wa = (const WifiNet *)a;
    const WifiNet *wb = (const WifiNet *)b;
    return wb->signal - wa->signal; /* strongest first */
}

void settings_scan_wifi(SettingsState *st) {
    char tmp[128] = {0};
    wpa_cmd("scan", NULL, NULL, NULL, tmp, sizeof(tmp));
    usleep(600000);

    char out[8192] = {0};
    st->net_count = 0;
    if (wpa_cmd("scan_results", NULL, NULL, NULL, out, sizeof(out)) != 0) {
        snprintf(st->status_msg, sizeof(st->status_msg), "WIFI SCAN FAILED");
        return;
    }

    char *saveptr = NULL;
    bool first = true;
    for (char *line = strtok_r(out, "\n", &saveptr); line; line = strtok_r(NULL, "\n", &saveptr)) {
        if (first) { first = false; continue; } /* header line */
        /* bssid \t freq \t signal \t flags \t ssid */
        char *t1 = strchr(line, '\t'); if (!t1) continue;
        char *t2 = strchr(t1 + 1, '\t'); if (!t2) continue;
        char *t3 = strchr(t2 + 1, '\t'); if (!t3) continue;
        char *t4 = strchr(t3 + 1, '\t'); if (!t4) continue;

        int sig = atoi(t2 + 1);
        bool sec = (strstr(t3 + 1, "WPA") != NULL || strstr(t3 + 1, "WEP") != NULL);
        char *ssid = t4 + 1;
        size_t slen = strlen(ssid);
        while (slen > 0 && (ssid[slen - 1] == '\r' || ssid[slen - 1] == '\n')) ssid[--slen] = '\0';
        if (slen == 0) continue;

        /* deduplicate by SSID, keeping stronger signal */
        int dup = -1;
        for (int i = 0; i < st->net_count; i++) {
            if (strcmp(st->nets[i].ssid, ssid) == 0) { dup = i; break; }
        }
        if (dup >= 0) {
            if (sig > st->nets[dup].signal) st->nets[dup].signal = sig;
        } else if (st->net_count < MAX_WIFI_NETS) {
            snprintf(st->nets[st->net_count].ssid, sizeof(st->nets[0].ssid), "%s", ssid);
            st->nets[st->net_count].signal = sig;
            st->nets[st->net_count].secured = sec;
            st->net_count++;
        }
    }
    if (st->net_count > 1) qsort(st->nets, (size_t)st->net_count, sizeof(WifiNet), cmp_wifi);
    snprintf(st->status_msg, sizeof(st->status_msg), "FOUND %d NETWORKS", st->net_count);
    settings_refresh(st);
}

static void connect_wifi_network(SettingsState *st, const char *ssid, const char *psk) {
    char id_buf[64] = {0};
    if (wpa_cmd("add_network", NULL, NULL, NULL, id_buf, sizeof(id_buf)) != 0) {
        snprintf(st->status_msg, sizeof(st->status_msg), "ADD_NETWORK FAILED");
        return;
    }
    /* strip whitespace */
    int nid = atoi(id_buf);
    char id_str[16];
    snprintf(id_str, sizeof(id_str), "%d", nid);

    char q_ssid[160], q_psk[160], res[128];
    snprintf(q_ssid, sizeof(q_ssid), "\"%s\"", ssid);
    wpa_cmd("set_network", id_str, "ssid", q_ssid, res, sizeof(res));

    if (psk && psk[0]) {
        snprintf(q_psk, sizeof(q_psk), "\"%s\"", psk);
        wpa_cmd("set_network", id_str, "psk", q_psk, res, sizeof(res));
    } else {
        wpa_cmd("set_network", id_str, "key_mgmt", "NONE", res, sizeof(res));
    }
    wpa_cmd("enable_network", id_str, NULL, NULL, res, sizeof(res));
    wpa_cmd("select_network", id_str, NULL, NULL, res, sizeof(res));
    wpa_cmd("save_config", NULL, NULL, NULL, res, sizeof(res));
    snprintf(st->status_msg, sizeof(st->status_msg), "CONNECTING TO %s...", ssid);
    usleep(800000);
    if (access("/sbin/noteos-dhcp", X_OK) == 0) {
        char *da[] = { (char *)"/sbin/noteos-dhcp", (char *)"wlan0", (char *)"--once", NULL };
        run_cmd_capture(da, res, sizeof(res));
    }
    settings_refresh(st);
}

static void restart_adbd_tcp(SettingsState *st) {
    char out[128];
    char *a1[] = { (char *)"/system/bin/setprop", (char *)"service.adb.tcp.port", (char *)"5555", NULL };
    char *a2[] = { (char *)"/system/bin/stop", (char *)"adbd", NULL };
    char *a3[] = { (char *)"/system/bin/start", (char *)"adbd", NULL };
    run_cmd_capture(a1, out, sizeof(out));
    run_cmd_capture(a2, out, sizeof(out));
    usleep(200000);
    run_cmd_capture(a3, out, sizeof(out));
    snprintf(st->status_msg, sizeof(st->status_msg), "ADBD RESTARTED ON :5555");
}

void settings_open_osk(SettingsState *st, OskPurpose purpose, const char *prompt,
                       const char *initial, const char *target_ssid) {
    memset(&st->osk, 0, sizeof(st->osk));
    st->osk.open = true;
    st->osk.purpose = purpose;
    snprintf(st->osk.prompt, sizeof(st->osk.prompt), "%s", prompt ? prompt : "ENTER TEXT");
    if (target_ssid) snprintf(st->osk.target_ssid, sizeof(st->osk.target_ssid), "%s", target_ssid);
    if (initial && initial[0]) {
        snprintf(st->osk.buf, sizeof(st->osk.buf), "%s", initial);
        st->osk.len = (int)strlen(st->osk.buf);
    }
}

static void draw_btn(FB *fb, const ThemePalette *tp, int x, int y, int w, int h,
                     const char *label, bool active, bool danger) {
    uint32_t bg = active ? tp->accent : tp->bar_bg;
    uint32_t fg = active ? 0xFFFFFF : tp->bar_text;
    uint32_t bd = active ? tp->accent : tp->bar_border;
    if (danger) { bg = 0xD9383A; fg = 0xFFFFFF; bd = 0xD9383A; }
    fb_fill_rect(fb, x, y, w, h, bg);
    fb_stroke_rect(fb, x, y, w, h, 2, bd);
    int tw = fb_text_width(label, 2);
    fb_draw_text(fb, x + (w - tw) / 2, y + (h - 14) / 2, label, fg, 2);
}

/* ---- On-Screen QWERTY Keyboard ---- */
static const char *ROW0_ALPHA = "1234567890";
static const char *ROW1_ALPHA = "QWERTYUIOP";
static const char *ROW2_ALPHA = "ASDFGHJKL";
static const char *ROW3_ALPHA = "ZXCVBNM";

static const char *ROW0_SYM = "!@#$%^&*()";
static const char *ROW1_SYM = "-_=+[]{};:";
static const char *ROW2_SYM = "'\",.<>/?\\";
static const char *ROW3_SYM = "|~`!@#?";

static void render_osk_textbox(FB *fb, const ThemePalette *tp, const OskState *osk) {
    int bx = 180, by = 220, bw = fb->w - 360;
    fb_fill_rect(fb, bx + 28, by + 62, bw - 56, 62, tp->bg);
    fb_stroke_rect(fb, bx + 28, by + 62, bw - 56, 62, 2, tp->bar_border);
    char disp[140];
    snprintf(disp, sizeof(disp), "%s_", osk->buf);
    fb_draw_text(fb, bx + 44, by + 84, disp, tp->bar_text, 2);
}

static void render_osk(FB *fb, const ThemePalette *tp, const OskState *osk) {
    int bx = 180, by = 220, bw = fb->w - 360, bh = 740;
    fb_fill_rect(fb, bx, by, bw, bh, tp->sidebar_bg);
    fb_stroke_rect(fb, bx, by, bw, bh, 3, tp->accent);

    fb_draw_text(fb, bx + 28, by + 24, osk->prompt, tp->accent, 2);
    render_osk_textbox(fb, tp, osk);

    const char *r0 = osk->symbols ? ROW0_SYM : ROW0_ALPHA;
    const char *r1 = osk->symbols ? ROW1_SYM : ROW1_ALPHA;
    const char *r2 = osk->symbols ? ROW2_SYM : ROW2_ALPHA;
    const char *r3 = osk->symbols ? ROW3_SYM : ROW3_ALPHA;

    int kw = 136, kh = 92, gap = 12;
    int start_y = by + 150;

    /* Row 0 (10 keys) */
    int rx0 = bx + (bw - (10 * kw + 9 * gap)) / 2;
    for (int i = 0; i < 10; i++) {
        char lbl[2] = { r0[i], '\0' };
        draw_btn(fb, tp, rx0 + i * (kw + gap), start_y, kw, kh, lbl, false, false);
    }

    /* Row 1 (10 keys) */
    int ry1 = start_y + kh + gap;
    for (int i = 0; i < 10; i++) {
        char lbl[2] = { r1[i], '\0' };
        draw_btn(fb, tp, rx0 + i * (kw + gap), ry1, kw, kh, lbl, false, false);
    }

    /* Row 2 (9 keys) */
    int ry2 = ry1 + kh + gap;
    int rx2 = bx + (bw - (9 * kw + 8 * gap)) / 2;
    for (int i = 0; i < 9; i++) {
        char lbl[2] = { r2[i], '\0' };
        draw_btn(fb, tp, rx2 + i * (kw + gap), ry2, kw, kh, lbl, false, false);
    }

    /* Row 3: SHIFT + 7 keys + BKSP */
    int ry3 = ry2 + kh + gap;
    int rx3 = rx0;
    draw_btn(fb, tp, rx3, ry3, kw + 40, kh, osk->shift ? "CAPS" : "SHIFT", osk->shift, false);
    int k3_start = rx3 + kw + 40 + gap;
    for (int i = 0; i < 7; i++) {
        char lbl[2] = { r3[i], '\0' };
        draw_btn(fb, tp, k3_start + i * (kw + gap), ry3, kw, kh, lbl, false, false);
    }
    int bksp_x = k3_start + 7 * (kw + gap);
    draw_btn(fb, tp, bksp_x, ry3, rx0 + 10 * kw + 9 * gap - bksp_x, kh, "BKSP", false, false);

    /* Row 4: SYM, CLR, SPACE, CANCEL, OK */
    int ry4 = ry3 + kh + gap + 8;
    draw_btn(fb, tp, rx0, ry4, 150, kh, osk->symbols ? "ABC" : "!@#", osk->symbols, false);
    draw_btn(fb, tp, rx0 + 162, ry4, 140, kh, "CLR", false, false);
    draw_btn(fb, tp, rx0 + 314, ry4, 590, kh, "SPACE", false, false);
    draw_btn(fb, tp, rx0 + 918, ry4, 260, kh, "CANCEL", false, false);
    draw_btn(fb, tp, rx0 + 1192, ry4, rx0 + 10 * kw + 9 * gap - (rx0 + 1192), kh, "OK", true, false);
}

static void osk_set_highlight(OskState *osk, int x, int y, int w, int h, const char *lbl) {
    osk->hi_x = x; osk->hi_y = y; osk->hi_w = w; osk->hi_h = h;
    snprintf(osk->hi_label, sizeof(osk->hi_label), "%s", lbl);
    osk->hi_active = true;
}

void settings_render_osk_fast(FB *fb, ThemeKind theme, SettingsState *st) {
    if (!st->osk.open) return;
    const ThemePalette *tp = &THEMES[theme];
    if (st->osk.need_full_keys) {
        st->osk.need_full_keys = false;
        render_osk(fb, tp, &st->osk);
    } else {
        render_osk_textbox(fb, tp, &st->osk);
        if (st->osk.hi_active) {
            draw_btn(fb, tp, st->osk.hi_x, st->osk.hi_y, st->osk.hi_w, st->osk.hi_h,
                     st->osk.hi_label, true, false);
        }
    }
}

void settings_osk_release(FB *fb, ThemeKind theme, SettingsState *st) {
    if (!st->osk.open || !st->osk.hi_active) return;
    const ThemePalette *tp = &THEMES[theme];
    draw_btn(fb, tp, st->osk.hi_x, st->osk.hi_y, st->osk.hi_w, st->osk.hi_h,
             st->osk.hi_label, false, false);
    st->osk.hi_active = false;
}

static int handle_osk_tap(FB *fb, SettingsState *st, int x, int y,
                          OskPurpose *out_purpose, char *out_text, size_t out_sz) {
    OskState *osk = &st->osk;
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
    int hx = 0, hy = 0, hw = kw, hh = kh;
    int ry1 = start_y + kh + gap;
    int ry2 = ry1 + kh + gap;
    int ry3 = ry2 + kh + gap;
    int ry4 = ry3 + kh + gap + 8;

    for (int i = 0; i < 10; i++) {
        int kx = rx0 + i * (kw + gap);
        if (hit_r(x, y, kx, start_y, kw, kh)) { ch = r0[i]; hx = kx; hy = start_y; }
        if (hit_r(x, y, kx, ry1, kw, kh))     { ch = r1[i]; hx = kx; hy = ry1; }
    }
    for (int i = 0; i < 9; i++) {
        int kx = rx2 + i * (kw + gap);
        if (hit_r(x, y, kx, ry2, kw, kh))     { ch = r2[i]; hx = kx; hy = ry2; }
    }
    int k3_start = rx0 + kw + 40 + gap;
    for (int i = 0; i < 7; i++) {
        int kx = k3_start + i * (kw + gap);
        if (hit_r(x, y, kx, ry3, kw, kh))     { ch = r3[i]; hx = kx; hy = ry3; }
    }

    if (ch) {
        char lbl[2] = { ch, '\0' };
        osk_set_highlight(osk, hx, hy, hw, hh, lbl);
        if (!osk->symbols && !osk->shift && ch >= 'A' && ch <= 'Z') {
            ch = (char)(ch - 'A' + 'a');
        }
        if (osk->len + 1 < (int)sizeof(osk->buf)) {
            osk->buf[osk->len++] = ch;
            osk->buf[osk->len] = '\0';
        }
        return SET_ACT_OSK_FAST;
    }

    if (hit_r(x, y, rx0, ry3, kw + 40, kh)) {
        osk->shift = !osk->shift;
        osk->hi_active = false;
        osk->need_full_keys = true;
        return SET_ACT_OSK_FAST;
    }
    int bksp_x = k3_start + 7 * (kw + gap);
    int bksp_w = rx0 + 10 * kw + 9 * gap - bksp_x;
    if (hit_r(x, y, bksp_x, ry3, bksp_w, kh)) {
        osk_set_highlight(osk, bksp_x, ry3, bksp_w, kh, "BKSP");
        if (osk->len > 0) osk->buf[--osk->len] = '\0';
        return SET_ACT_OSK_FAST;
    }
    if (hit_r(x, y, rx0, ry4, 150, kh)) {
        osk->symbols = !osk->symbols;
        osk->hi_active = false;
        osk->need_full_keys = true;
        return SET_ACT_OSK_FAST;
    }
    if (hit_r(x, y, rx0 + 162, ry4, 140, kh)) {
        osk_set_highlight(osk, rx0 + 162, ry4, 140, kh, "CLR");
        osk->len = 0;
        osk->buf[0] = '\0';
        return SET_ACT_OSK_FAST;
    }
    if (hit_r(x, y, rx0 + 314, ry4, 590, kh)) {
        osk_set_highlight(osk, rx0 + 314, ry4, 590, kh, "SPACE");
        if (osk->len + 1 < (int)sizeof(osk->buf)) {
            osk->buf[osk->len++] = ' ';
            osk->buf[osk->len] = '\0';
        }
        return SET_ACT_OSK_FAST;
    }
    if (hit_r(x, y, rx0 + 918, ry4, 260, kh)) {
        osk->open = false;
        osk->hi_active = false;
        return SET_ACT_REDRAW;
    }
    int ok_w = rx0 + 10 * kw + 9 * gap - (rx0 + 1192);
    if (hit_r(x, y, rx0 + 1192, ry4, ok_w, kh)) {
        OskPurpose p = osk->purpose;
        char text[128], ssid[64];
        snprintf(text, sizeof(text), "%s", osk->buf);
        snprintf(ssid, sizeof(ssid), "%s", osk->target_ssid);
        osk->open = false;
        osk->hi_active = false;
        if (p == OSK_WIFI_PASS) {
            connect_wifi_network(st, ssid, text);
            return SET_ACT_REDRAW;
        }
        if (out_purpose) *out_purpose = p;
        if (out_text && out_sz > 0) snprintf(out_text, out_sz, "%s", text);
        return SET_ACT_OSK_DONE;
    }
    return SET_ACT_NONE;
}

void settings_render(FB *fb, ThemeKind theme, const SettingsState *st) {
    const ThemePalette *tp = &THEMES[theme];

    /* If only OSK is open over canvas/sidebar (rename note), draw OSK only */
    if (!st->open && st->osk.open) {
        render_osk(fb, tp, &st->osk);
        return;
    }
    if (!st->open) return;

    /* Fullscreen settings background */
    fb_fill_rect(fb, 0, 0, fb->w, fb->h, tp->bg);

    /* Top header */
    fb_fill_rect(fb, 0, 0, fb->w, TOOLBAR_H, tp->bar_bg);
    fb_fill_rect(fb, 0, TOOLBAR_H - 2, fb->w, 2, tp->bar_border);
    draw_btn(fb, tp, 14, 9, 130, 40, "< BACK", true, false);
    fb_draw_text(fb, 170, 20, "E-TAB 5 OS SETTINGS", tp->bar_text, 2);
    if (st->status_msg[0]) {
        int tw = fb_text_width(st->status_msg, 2);
        fb_draw_text(fb, fb->w - tw - 24, 20, st->status_msg, tp->accent, 2);
    }

    /* ---- LEFT PANEL: WI-FI ---- */
    int lx = 32, ly = TOOLBAR_H + 28, lw = 910, lh = fb->h - TOOLBAR_H - 56;
    fb_fill_rect(fb, lx, ly, lw, lh, tp->sidebar_bg);
    fb_stroke_rect(fb, lx, ly, lw, lh, 2, tp->bar_border);

    fb_draw_text(fb, lx + 24, ly + 22, "WI-FI (WLAN0)", tp->accent, 2);

    char line[128];
    snprintf(line, sizeof(line), "NETWORK : %s", st->ssid);
    fb_draw_text(fb, lx + 24, ly + 60, line, tp->bar_text, 2);
    snprintf(line, sizeof(line), "STATE   : %s", st->wpa_state);
    fb_draw_text(fb, lx + 24, ly + 90, line, tp->bar_muted, 2);
    snprintf(line, sizeof(line), "IP ADDR : %s   (ADB :5555)", st->ip);
    fb_draw_text(fb, lx + 24, ly + 120, line, tp->bar_text, 2);

    draw_btn(fb, tp, lx + 24,  ly + 162, 190, 48, "SCAN WIFI", true, false);
    draw_btn(fb, tp, lx + 228, ly + 162, 210, 48, "RECONNECT", false, false);
    draw_btn(fb, tp, lx + 452, ly + 162, 220, 48, "DISCONNECT", false, false);

    fb_fill_rect(fb, lx + 20, ly + 228, lw - 40, 2, tp->bar_border);
    fb_draw_text(fb, lx + 24, ly + 242, "NEARBY NETWORKS (TAP TO CONNECT)", tp->bar_muted, 2);

    int ny = ly + 280;
    int max_nets = (ly + lh - ny - 16) / 62;
    if (st->net_count == 0) {
        fb_draw_text(fb, lx + 24, ny + 12, "TAP 'SCAN WIFI' TO LIST NETWORKS.", tp->bar_muted, 2);
    } else {
        for (int i = 0; i < st->net_count && i < max_nets; i++) {
            bool connected = (strcmp(st->nets[i].ssid, st->ssid) == 0);
            fb_fill_rect(fb, lx + 24, ny, lw - 48, 52, connected ? tp->sidebar_sel : tp->bar_bg);
            fb_stroke_rect(fb, lx + 24, ny, lw - 48, 52, 1, connected ? tp->accent : tp->bar_border);
            fb_draw_text(fb, lx + 40, ny + 18, st->nets[i].ssid, tp->bar_text, 2);
            char rinfo[48];
            snprintf(rinfo, sizeof(rinfo), "%s %d DBM", st->nets[i].secured ? "WPA" : "OPEN", st->nets[i].signal);
            int rw = fb_text_width(rinfo, 2);
            fb_draw_text(fb, lx + lw - 40 - rw, ny + 18, rinfo, tp->bar_muted, 2);
            ny += 62;
        }
    }

    /* ---- RIGHT PANEL: DISPLAY, BATTERY & SYSTEM ---- */
    int rx = 974, ry = ly, rw = fb->w - rx - 32, rh = lh;
    fb_fill_rect(fb, rx, ry, rw, rh, tp->sidebar_bg);
    fb_stroke_rect(fb, rx, ry, rw, rh, 2, tp->bar_border);

    /* Brightness */
    int bri = fb_get_backlight();
    int bri_pct = (bri * 100) / 255;
    fb_draw_text(fb, rx + 24, ry + 22, "DISPLAY BRIGHTNESS", tp->accent, 2);
    snprintf(line, sizeof(line), "%d%% (%d / 255)", bri_pct, bri);
    fb_draw_text(fb, rx + rw - fb_text_width(line, 2) - 24, ry + 22, line, tp->bar_text, 2);

    draw_btn(fb, tp, rx + 24, ry + 62, 74, 52, "-", false, false);
    int bar_x = rx + 112, bar_y = ry + 66, bar_w = rw - 224, bar_h = 44;
    fb_fill_rect(fb, bar_x, bar_y, bar_w, bar_h, tp->bar_bg);
    fb_stroke_rect(fb, bar_x, bar_y, bar_w, bar_h, 2, tp->bar_border);
    int fill_w = (bar_w - 4) * bri / 255;
    if (fill_w > 0) fb_fill_rect(fb, bar_x + 2, bar_y + 2, fill_w, bar_h - 4, tp->accent);
    draw_btn(fb, tp, rx + rw - 98, ry + 62, 74, 52, "+", false, false);

    fb_fill_rect(fb, rx + 20, ry + 136, rw - 40, 2, tp->bar_border);

    /* Battery & Storage */
    fb_draw_text(fb, rx + 24, ry + 156, "BATTERY & STORAGE", tp->accent, 2);
    int cap = read_int_file("/sys/class/power_supply/battery/capacity", -1);
    int vol = read_int_file("/sys/class/power_supply/battery/batt_vol", 0);
    int cur = read_int_file("/sys/class/power_supply/battery/BatteryAverageCurrent", 0);
    int tmp = read_int_file("/sys/class/power_supply/battery/batt_temp", 0);
    char bstat[32];
    read_str_file("/sys/class/power_supply/battery/status", bstat, sizeof(bstat), "UNKNOWN");

    snprintf(line, sizeof(line), "LEVEL   : %d%% (%s)", cap, bstat);
    fb_draw_text(fb, rx + 24, ry + 196, line, tp->bar_text, 2);
    snprintf(line, sizeof(line), "POWER   : %d MV   |   %d MA   |   %.1f C", vol, cur, (float)tmp / 10.0f);
    fb_draw_text(fb, rx + 24, ry + 228, line, tp->bar_muted, 2);

    struct statvfs vfs;
    if (statvfs("/data", &vfs) == 0) {
        double free_gb = (double)vfs.f_bavail * (double)vfs.f_frsize / (1024.0 * 1024.0 * 1024.0);
        double tot_gb  = (double)vfs.f_blocks * (double)vfs.f_frsize / (1024.0 * 1024.0 * 1024.0);
        snprintf(line, sizeof(line), "STORAGE : %.1f GB FREE / %.1f GB TOTAL", free_gb, tot_gb);
    } else {
        snprintf(line, sizeof(line), "STORAGE : UNAVAILABLE");
    }
    fb_draw_text(fb, rx + 24, ry + 260, line, tp->bar_text, 2);

    fb_fill_rect(fb, rx + 20, ry + 306, rw - 40, 2, tp->bar_border);

    /* Remote / ADB */
    fb_draw_text(fb, rx + 24, ry + 326, "REMOTE ACCESS (ADB OVER WI-FI)", tp->accent, 2);
    snprintf(line, sizeof(line), "CONNECT : ADB CONNECT %s:5555", st->ip);
    fb_draw_text(fb, rx + 24, ry + 362, line, tp->bar_text, 2);
    draw_btn(fb, tp, rx + 24, ry + 402, 340, 52, "RESTART ADBD :5555", false, false);
    draw_btn(fb, tp, rx + 380, ry + 402, 220, 52, "REFRESH", false, false);

    fb_fill_rect(fb, rx + 20, ry + 480, rw - 40, 2, tp->bar_border);

    /* Power / System Actions */
    fb_draw_text(fb, rx + 24, ry + 502, "SYSTEM POWER", tp->accent, 2);
    draw_btn(fb, tp, rx + 24,  ry + 544, 270, 58, "SLEEP SCREEN", false, false);
    draw_btn(fb, tp, rx + 312, ry + 544, 270, 58,
             st->confirm_action == 1 ? "CONFIRM REBOOT?" : "REBOOT", false, st->confirm_action == 1);
    draw_btn(fb, tp, rx + 24,  ry + 620, 270, 58,
             st->confirm_action == 2 ? "CONFIRM TWRP?" : "BOOT RECOVERY", false, st->confirm_action == 2);
    draw_btn(fb, tp, rx + 312, ry + 620, 270, 58,
             st->confirm_action == 3 ? "CONFIRM OFF?" : "POWER OFF", false, st->confirm_action == 3);

    fb_draw_text(fb, rx + 24, ry + rh - 44, "E-TAB 5 BARE NOTE OS  |  LINUX 3.4.67 ARMV7", tp->bar_muted, 2);

    if (st->osk.open) {
        render_osk(fb, tp, &st->osk);
    }
}

int settings_handle_tap(FB *fb, SettingsState *st, int x, int y,
                        OskPurpose *out_purpose, char *out_text, size_t out_sz) {
    if (st->osk.open) {
        return handle_osk_tap(fb, st, x, y, out_purpose, out_text, out_sz);
    }
    if (!st->open) return SET_ACT_NONE;

    /* Header BACK */
    if (hit_r(x, y, 14, 9, 130, 40)) {
        st->open = false;
        st->confirm_action = 0;
        return SET_ACT_CLOSE;
    }

    int lx = 32, ly = TOOLBAR_H + 28, lw = 910, lh = fb->h - TOOLBAR_H - 56;
    int rx = 974, ry = ly, rw = fb->w - rx - 32;

    /* Wi-Fi buttons */
    if (hit_r(x, y, lx + 24, ly + 162, 190, 48)) {
        st->confirm_action = 0;
        settings_scan_wifi(st);
        return SET_ACT_REDRAW;
    }
    if (hit_r(x, y, lx + 228, ly + 162, 210, 48)) {
        char res[128];
        wpa_cmd("reconnect", NULL, NULL, NULL, res, sizeof(res));
        snprintf(st->status_msg, sizeof(st->status_msg), "RECONNECTING...");
        usleep(400000);
        settings_refresh(st);
        return SET_ACT_REDRAW;
    }
    if (hit_r(x, y, lx + 452, ly + 162, 220, 48)) {
        char res[128];
        wpa_cmd("disconnect", NULL, NULL, NULL, res, sizeof(res));
        snprintf(st->status_msg, sizeof(st->status_msg), "DISCONNECTED");
        settings_refresh(st);
        return SET_ACT_REDRAW;
    }

    /* Nearby Wi-Fi list */
    int ny = ly + 280;
    int max_nets = (ly + lh - ny - 16) / 62;
    for (int i = 0; i < st->net_count && i < max_nets; i++) {
        if (hit_r(x, y, lx + 24, ny, lw - 48, 52)) {
            st->confirm_action = 0;
            if (!st->nets[i].secured) {
                connect_wifi_network(st, st->nets[i].ssid, "");
            } else {
                char prompt[96];
                snprintf(prompt, sizeof(prompt), "WIFI PASSWORD FOR %s:", st->nets[i].ssid);
                settings_open_osk(st, OSK_WIFI_PASS, prompt, "", st->nets[i].ssid);
            }
            return SET_ACT_REDRAW;
        }
        ny += 62;
    }

    /* Brightness controls */
    if (hit_r(x, y, rx + 24, ry + 62, 74, 52)) {
        int b = fb_get_backlight() - 25;
        if (b < 15) b = 15;
        fb_set_backlight(b);
        return SET_ACT_REDRAW;
    }
    if (hit_r(x, y, rx + rw - 98, ry + 62, 74, 52)) {
        int b = fb_get_backlight() + 25;
        if (b > 255) b = 255;
        fb_set_backlight(b);
        return SET_ACT_REDRAW;
    }
    int bar_x = rx + 112, bar_y = ry + 62, bar_w = rw - 224, bar_h = 52;
    if (hit_r(x, y, bar_x, bar_y, bar_w, bar_h)) {
        int b = 15 + ((x - bar_x) * 240) / bar_w;
        if (b < 15) b = 15;
        if (b > 255) b = 255;
        fb_set_backlight(b);
        return SET_ACT_REDRAW;
    }

    /* Remote / ADB */
    if (hit_r(x, y, rx + 24, ry + 402, 340, 52)) {
        restart_adbd_tcp(st);
        settings_refresh(st);
        return SET_ACT_REDRAW;
    }
    if (hit_r(x, y, rx + 380, ry + 402, 220, 52)) {
        settings_refresh(st);
        snprintf(st->status_msg, sizeof(st->status_msg), "STATUS REFRESHED");
        return SET_ACT_REDRAW;
    }

    /* System Power Actions */
    if (hit_r(x, y, rx + 24, ry + 544, 270, 58)) {
        st->confirm_action = 0;
        return SET_ACT_SLEEP;
    }
    if (hit_r(x, y, rx + 312, ry + 544, 270, 58)) {
        if (st->confirm_action != 1) {
            st->confirm_action = 1;
            return SET_ACT_REDRAW;
        }
        char out[64];
        char *argv[] = { (char *)"/system/bin/reboot", NULL };
        run_cmd_capture(argv, out, sizeof(out));
        return SET_ACT_NONE;
    }
    if (hit_r(x, y, rx + 24, ry + 620, 270, 58)) {
        if (st->confirm_action != 2) {
            st->confirm_action = 2;
            return SET_ACT_REDRAW;
        }
        char out[64];
        char *argv[] = { (char *)"/system/bin/reboot", (char *)"recovery", NULL };
        run_cmd_capture(argv, out, sizeof(out));
        return SET_ACT_NONE;
    }
    if (hit_r(x, y, rx + 312, ry + 620, 270, 58)) {
        if (st->confirm_action != 3) {
            st->confirm_action = 3;
            return SET_ACT_REDRAW;
        }
        char out[64];
        char *argv[] = { (char *)"/system/bin/reboot", (char *)"-p", NULL };
        run_cmd_capture(argv, out, sizeof(out));
        return SET_ACT_NONE;
    }

    st->confirm_action = 0;
    return SET_ACT_REDRAW;
}
