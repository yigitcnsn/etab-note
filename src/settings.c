#define _GNU_SOURCE
#include "settings.h"
#include "fb.h"
#include "osk.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/statvfs.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define WPA_CLI_PATH "/system/bin/wpa_cli"
#define WPA_SOCK_DIR "/data/misc/wifi/sockets"

static bool hit_rect(int px, int py, int x, int y, int w, int h) {
    return px >= x && px < x + w && py >= y && py < y + h;
}

static bool hit_btn(FB *fb, int px, int py, int x, int y, int w, int h) {
    if (px >= x && px < x + w && py >= y && py < y + h) {
        fb->last_btn_x = x;
        fb->last_btn_y = y;
        fb->last_btn_w = w;
        fb->last_btn_h = h;
        fb->last_btn_label[0] = '\0';
        fb_flash_last_button(fb, &THEMES[0]);
        return true;
    }
    return false;
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

typedef struct {
    time_t ts;
    int    cap;
    int    mv;
    int    ma;
    char   status[16];
    char   event[16];
} BattSample;

#define MAX_BATT_SAMPLES 48

void settings_log_battery(const char *event) {
    static int prev_cap = -1;
    static char prev_stat[16] = {0};
    int cap = read_int_file("/sys/class/power_supply/battery/capacity", -1);
    if (cap < 0) return;
    int vol = read_int_file("/sys/class/power_supply/battery/batt_vol", 0);
    int cur = read_int_file("/sys/class/power_supply/battery/BatteryAverageCurrent", 0);
    char bstat[16];
    read_str_file("/sys/class/power_supply/battery/status", bstat, sizeof(bstat), "UNK");

    if (prev_stat[0] && strcmp(prev_stat, bstat) != 0) {
        settings_log_event("POWER STATUS: %s -> %s (%d%%)", prev_stat, bstat, cap);
    } else if (prev_cap >= 0 && abs(cap - prev_cap) >= 2 && strcmp(event, "TICK") == 0) {
        settings_log_event("BATTERY LEVEL: %d%% -> %d%% (%s)", prev_cap, cap, bstat);
    }
    prev_cap = cap;
    snprintf(prev_stat, sizeof(prev_stat), "%s", bstat);

    /* Load existing samples and keep last MAX_BATT_SAMPLES - 1 */
    BattSample samples[MAX_BATT_SAMPLES];
    int count = 0;
    FILE *rf = fopen(BATT_LOG_PATH, "r");
    if (rf) {
        char line[160];
        while (fgets(line, sizeof(line), rf)) {
            long ts = 0;
            int c = 0, v = 0, a = 0;
            char st[16] = {0}, ev[16] = {0};
            if (sscanf(line, "%ld,%d,%d,%d,%15[^,],%15[^\r\n]", &ts, &c, &v, &a, st, ev) >= 5) {
                if (count == MAX_BATT_SAMPLES) {
                    memmove(&samples[0], &samples[1], sizeof(BattSample) * (MAX_BATT_SAMPLES - 1));
                    count--;
                }
                samples[count].ts = (time_t)ts;
                samples[count].cap = c;
                samples[count].mv = v;
                samples[count].ma = a;
                snprintf(samples[count].status, sizeof(samples[count].status), "%s", st);
                snprintf(samples[count].event, sizeof(samples[count].event), "%s", ev[0] ? ev : "TICK");
                count++;
            }
        }
        fclose(rf);
    }

    time_t now = time(NULL);
    /* Avoid duplicate TICK entries if less than 15 min since last sample AND capacity unchanged */
    if (count > 0 && strcmp(event, "TICK") == 0) {
        BattSample *last = &samples[count - 1];
        if (last->cap == cap && (now - last->ts) < 900) return;
    }

    if (count == MAX_BATT_SAMPLES) {
        memmove(&samples[0], &samples[1], sizeof(BattSample) * (MAX_BATT_SAMPLES - 1));
        count--;
    }
    samples[count].ts = now;
    samples[count].cap = cap;
    samples[count].mv = vol;
    samples[count].ma = cur;
    snprintf(samples[count].status, sizeof(samples[count].status), "%s", bstat);
    snprintf(samples[count].event, sizeof(samples[count].event), "%s", event ? event : "TICK");
    count++;

    FILE *wf = fopen(BATT_LOG_PATH, "w");
    if (wf) {
        for (int i = 0; i < count; i++) {
            fprintf(wf, "%ld,%d,%d,%d,%s,%s\n",
                    (long)samples[i].ts, samples[i].cap, samples[i].mv,
                    samples[i].ma, samples[i].status, samples[i].event);
        }
        fclose(wf);
    }
}

static int load_batt_samples(BattSample *out, int max_out) {
    FILE *rf = fopen(BATT_LOG_PATH, "r");
    if (!rf) return 0;
    int count = 0;
    char line[160];
    while (fgets(line, sizeof(line), rf)) {
        long ts = 0;
        int c = 0, v = 0, a = 0;
        char st[16] = {0}, ev[16] = {0};
        if (sscanf(line, "%ld,%d,%d,%d,%15[^,],%15[^\r\n]", &ts, &c, &v, &a, st, ev) >= 5) {
            if (count == max_out) {
                memmove(&out[0], &out[1], sizeof(BattSample) * (size_t)(max_out - 1));
                count--;
            }
            out[count].ts = (time_t)ts;
            out[count].cap = c;
            out[count].mv = v;
            out[count].ma = a;
            snprintf(out[count].status, sizeof(out[count].status), "%s", st);
            snprintf(out[count].event, sizeof(out[count].event), "%s", ev[0] ? ev : "TICK");
            count++;
        }
    }
    fclose(rf);
    return count;
}

#define MAX_SYS_EVENTS 24

void settings_log_event(const char *fmt, ...) {
    if (!fmt) return;
    char msg[120];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);

    int cap = read_int_file("/sys/class/power_supply/battery/capacity", -1);
    int ma  = read_int_file("/sys/class/power_supply/battery/BatteryAverageCurrent", 0);
    time_t now = time(NULL);
    struct tm tm_now;
    localtime_r(&now, &tm_now);

    char entry[180];
    snprintf(entry, sizeof(entry), "%02d:%02d:%02d [%d%% %dmA] %s",
             tm_now.tm_hour, tm_now.tm_min, tm_now.tm_sec, cap, ma, msg);

    char lines[MAX_SYS_EVENTS][180];
    int count = 0;
    FILE *rf = fopen(EVENT_LOG_PATH, "r");
    if (rf) {
        char buf[180];
        while (fgets(buf, sizeof(buf), rf)) {
            size_t n = strlen(buf);
            while (n > 0 && (buf[n - 1] == '\n' || buf[n - 1] == '\r')) buf[--n] = '\0';
            if (!buf[0]) continue;
            if (count == MAX_SYS_EVENTS) {
                memmove(&lines[0], &lines[1], sizeof(lines[0]) * (MAX_SYS_EVENTS - 1));
                count--;
            }
            snprintf(lines[count++], sizeof(lines[0]), "%s", buf);
        }
        fclose(rf);
    }

    if (count == MAX_SYS_EVENTS) {
        memmove(&lines[0], &lines[1], sizeof(lines[0]) * (MAX_SYS_EVENTS - 1));
        count--;
    }
    snprintf(lines[count++], sizeof(lines[0]), "%s", entry);

    FILE *wf = fopen(EVENT_LOG_PATH, "w");
    if (wf) {
        for (int i = 0; i < count; i++) {
            fprintf(wf, "%s\n", lines[i]);
        }
        fclose(wf);
    }
}

static int load_sys_events(char out[][180], int max_out) {
    FILE *rf = fopen(EVENT_LOG_PATH, "r");
    if (!rf) return 0;
    int count = 0;
    char buf[180];
    while (fgets(buf, sizeof(buf), rf)) {
        size_t n = strlen(buf);
        while (n > 0 && (buf[n - 1] == '\n' || buf[n - 1] == '\r')) buf[--n] = '\0';
        if (!buf[0]) continue;
        if (count == max_out) {
            memmove(&out[0], &out[1], sizeof(out[0]) * (size_t)(max_out - 1));
            count--;
        }
        snprintf(out[count++], 180, "%s", buf);
    }
    fclose(rf);
    return count;
}

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
    char *argv[] = {
        (char *)"/system/bin/ip", (char *)"-4", (char *)"addr",
        (char *)"show", (char *)"wlan0", NULL
    };
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
            if (strncmp(line, "ssid=", 5) == 0)
                snprintf(st->ssid, sizeof(st->ssid), "%s", line + 5);
            else if (strncmp(line, "wpa_state=", 10) == 0)
                snprintf(st->wpa_state, sizeof(st->wpa_state), "%s", line + 10);
            else if (strncmp(line, "ip_address=", 11) == 0)
                snprintf(st->ip, sizeof(st->ip), "%s", line + 11);
        }
    }
    if (strcmp(st->ip, "NONE") == 0) {
        parse_ip_from_sys(st->ip, sizeof(st->ip));
    }
}

static int cmp_wifi(const void *a, const void *b) {
    const WifiNet *wa = (const WifiNet *)a;
    const WifiNet *wb = (const WifiNet *)b;
    return wb->signal - wa->signal;
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
        if (first) { first = false; continue; }
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

void settings_connect_wifi(SettingsState *st, const char *ssid, const char *psk) {
    char id_buf[64] = {0};
    if (wpa_cmd("add_network", NULL, NULL, NULL, id_buf, sizeof(id_buf)) != 0) {
        snprintf(st->status_msg, sizeof(st->status_msg), "ADD_NETWORK FAILED");
        return;
    }
    char id_str[16];
    snprintf(id_str, sizeof(id_str), "%d", atoi(id_buf));

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

void settings_render(FB *fb, ThemeKind theme, PaperKind paper, const SettingsState *st) {
    if (!st->open && st->osk.open) {
        osk_render_full(fb, theme, &st->osk);
        return;
    }
    if (!st->open) return;

    const ThemePalette *tp = &THEMES[theme];
    const char *paper_names[PAPER_COUNT] = { "LINED", "GRID", "BLANK" };
    fb_fill_rect(fb, 0, 0, fb->w, fb->h, tp->bg);

    /* Top header */
    fb_fill_rect(fb, 0, 0, fb->w, TOOLBAR_H, tp->bar_bg);
    fb_fill_rect(fb, 0, TOOLBAR_H - 2, fb->w, 2, tp->bar_border);
    fb_draw_button(fb, tp, 14, 9, 130, 40, "< BACK", true, false);
    fb_draw_text(fb, 170, 20, "E-TAB 5 OS SETTINGS & ACTIVITY LOG", tp->bar_text, 2);
    if (st->status_msg[0]) {
        int tw = fb_text_width(st->status_msg, 2);
        fb_draw_text(fb, fb->w - tw - 24, 20, st->status_msg, tp->accent, 2);
    }

    /* Left panel: Wi-Fi + System Events Log */
    int lx = 32, ly = TOOLBAR_H + 24, lw = 910, lh = fb->h - TOOLBAR_H - 48;
    fb_fill_rect(fb, lx, ly, lw, lh, tp->sidebar_bg);
    fb_stroke_rect(fb, lx, ly, lw, lh, 2, tp->bar_border);
    fb_draw_text(fb, lx + 24, ly + 18, "WI-FI (WLAN0) & REMOTE DROP", tp->accent, 2);

    char line[160];
    snprintf(line, sizeof(line), "SSID: %s  (%s)", st->ssid, st->wpa_state);
    fb_draw_text(fb, lx + 24, ly + 50, line, tp->bar_text, 2);
    snprintf(line, sizeof(line), "IP  : %s  |  HTTP://ETAB.LOCAL:8080  (ADB :5555)", st->ip);
    fb_draw_text(fb, lx + 24, ly + 78, line, tp->bar_muted, 2);

    fb_draw_button(fb, tp, lx + 24,  ly + 112, 168, 44, "SCAN WIFI", true, false);
    fb_draw_button(fb, tp, lx + 204, ly + 112, 176, 44, "RECONNECT", false, false);
    fb_draw_button(fb, tp, lx + 392, ly + 112, 184, 44, "DISCONNECT", false, false);
    fb_draw_button(fb, tp, lx + 588, ly + 112, 176, 44, "ADB :5555", false, false);
    fb_draw_button(fb, tp, lx + 776, ly + 112, 110, 44, "SYNC", false, false);

    fb_fill_rect(fb, lx + 20, ly + 168, lw - 40, 2, tp->bar_border);
    fb_draw_text(fb, lx + 24, ly + 180, "NEARBY NETWORKS (TAP TO CONNECT)", tp->bar_muted, 2);

    int ny = ly + 210;
    int max_nets = 5;
    if (st->net_count == 0) {
        fb_draw_text(fb, lx + 24, ny + 12, "TAP 'SCAN WIFI' TO LIST NEARBY NETWORKS.", tp->bar_muted, 2);
    } else {
        for (int i = 0; i < st->net_count && i < max_nets; i++) {
            bool connected = (strcmp(st->nets[i].ssid, st->ssid) == 0);
            fb_fill_rect(fb, lx + 24, ny, lw - 48, 46, connected ? tp->sidebar_sel : tp->bar_bg);
            fb_stroke_rect(fb, lx + 24, ny, lw - 48, 46, 1, connected ? tp->accent : tp->bar_border);
            fb_draw_text(fb, lx + 38, ny + 15, st->nets[i].ssid, tp->bar_text, 2);
            char rinfo[48];
            snprintf(rinfo, sizeof(rinfo), "%s %d DBM", st->nets[i].secured ? "WPA" : "OPEN", st->nets[i].signal);
            int rw_t = fb_text_width(rinfo, 2);
            fb_draw_text(fb, lx + lw - 38 - rw_t, ny + 15, rinfo, tp->bar_muted, 2);
            ny += 52;
        }
    }

    /* Bottom-left: SYSTEM EVENTS & AWAY LOG */
    int ey = ly + 482;
    fb_fill_rect(fb, lx + 20, ey, lw - 40, 2, tp->bar_border);
    fb_draw_text(fb, lx + 24, ey + 14, "SYSTEM EVENTS & WHILE-AWAY LOG", tp->accent, 2);
    fb_draw_button(fb, tp, lx + lw - 154, ey + 8, 130, 34, "CLR LOG", false, false);

    int log_box_y = ey + 48;
    int log_box_h = lh - (log_box_y - ly) - 18;
    fb_fill_rect(fb, lx + 24, log_box_y, lw - 48, log_box_h, tp->bar_bg);
    fb_stroke_rect(fb, lx + 24, log_box_y, lw - 48, log_box_h, 1, tp->bar_border);

    char ev_lines[16][180];
    int ev_count = load_sys_events(ev_lines, 16);
    int max_ev_rows = (log_box_h - 16) / 22;
    if (ev_count == 0) {
        fb_draw_text(fb, lx + 40, log_box_y + 20, "NO EVENTS RECORDED YET.", tp->bar_muted, 2);
    } else {
        int start_idx = ev_count > max_ev_rows ? (ev_count - max_ev_rows) : 0;
        int ry_ev = log_box_y + 10;
        for (int i = start_idx; i < ev_count; i++) {
            /* Show newest at top so user immediately sees what happened most recently */
            int rev = ev_count - 1 - (i - start_idx);
            uint32_t col = (i == start_idx) ? tp->bar_text : tp->bar_muted;
            if (strstr(ev_lines[rev], "SLEEP") || strstr(ev_lines[rev], "WAKE") || strstr(ev_lines[rev], "DROP"))
                col = tp->accent;
            char short_ev[72];
            snprintf(short_ev, sizeof(short_ev), "%.68s", ev_lines[rev]);
            fb_draw_text(fb, lx + 36, ry_ev, short_ev, col, 2);
            ry_ev += 22;
        }
    }

    /* Right panel: Display, Appearance (Theme/Paper), Battery Tracker & System Power */
    int rx = 974, ry = ly, rw = fb->w - rx - 32, rh = lh;
    fb_fill_rect(fb, rx, ry, rw, rh, tp->sidebar_bg);
    fb_stroke_rect(fb, rx, ry, rw, rh, 2, tp->bar_border);

    int bri = fb_get_backlight();
    fb_draw_text(fb, rx + 24, ry + 18, "DISPLAY & APPEARANCE", tp->accent, 2);
    snprintf(line, sizeof(line), "BRIGHTNESS %d%%", (bri * 100) / 255);
    fb_draw_text(fb, rx + rw - fb_text_width(line, 2) - 24, ry + 18, line, tp->bar_text, 2);

    fb_draw_button(fb, tp, rx + 24, ry + 52, 74, 48, "-", false, false);
    int bar_x = rx + 112, bar_y = ry + 56, bar_w = rw - 224, bar_h = 40;
    fb_fill_rect(fb, bar_x, bar_y, bar_w, bar_h, tp->bar_bg);
    fb_stroke_rect(fb, bar_x, bar_y, bar_w, bar_h, 2, tp->bar_border);
    int fill_w = (bar_w - 4) * bri / 255;
    if (fill_w > 0) fb_fill_rect(fb, bar_x + 2, bar_y + 2, fill_w, bar_h - 4, tp->accent);
    fb_draw_button(fb, tp, rx + rw - 98, ry + 52, 74, 48, "+", false, false);

    /* Theme & Paper buttons moved from top bar into Settings */
    char t_lbl[48], p_lbl[48];
    snprintf(t_lbl, sizeof(t_lbl), "THEME: %s", tp->name);
    snprintf(p_lbl, sizeof(p_lbl), "PAPER: %s", paper_names[paper % PAPER_COUNT]);
    fb_draw_button(fb, tp, rx + 24,  ry + 112, (rw - 64) / 2, 48, t_lbl, false, false);
    fb_draw_button(fb, tp, rx + 40 + (rw - 64) / 2, ry + 112, (rw - 64) / 2, 48, p_lbl, false, false);

    fb_fill_rect(fb, rx + 20, ry + 174, rw - 40, 2, tp->bar_border);

    fb_draw_text(fb, rx + 24, ry + 188, "BATTERY & DAY TRACKER", tp->accent, 2);
    int cap = read_int_file("/sys/class/power_supply/battery/capacity", -1);
    int vol = read_int_file("/sys/class/power_supply/battery/batt_vol", 0);
    int cur = read_int_file("/sys/class/power_supply/battery/BatteryAverageCurrent", 0);
    int tmp = read_int_file("/sys/class/power_supply/battery/batt_temp", 0);
    char bstat[32];
    read_str_file("/sys/class/power_supply/battery/status", bstat, sizeof(bstat), "UNKNOWN");

    struct statvfs vfs;
    double free_gb = 0.0, tot_gb = 0.0;
    if (statvfs("/data", &vfs) == 0) {
        free_gb = (double)vfs.f_bavail * (double)vfs.f_frsize / (1024.0 * 1024.0 * 1024.0);
        tot_gb  = (double)vfs.f_blocks * (double)vfs.f_frsize / (1024.0 * 1024.0 * 1024.0);
    }

    snprintf(line, sizeof(line), "%d%% (%s)  |  %d MV  |  %d MA  |  %.1fC  |  %.1f/%.1f GB",
             cap, bstat, vol, cur, (float)tmp / 10.0f, free_gb, tot_gb);
    fb_draw_text(fb, rx + 24, ry + 216, line, tp->bar_text, 2);

    /* 24-hour / 32-sample Battery History Bar Chart */
    BattSample samples[32];
    int scount = load_batt_samples(samples, 32);
    int gx = rx + 24, gy = ry + 244, gw = rw - 48, gh = 220;
    fb_fill_rect(fb, gx, gy, gw, gh, tp->bar_bg);
    fb_stroke_rect(fb, gx, gy, gw, gh, 1, tp->bar_border);
    /* 50% dashed reference line */
    for (int dx = gx + 6; dx < gx + gw - 6; dx += 12) {
        fb_fill_rect(fb, dx, gy + (gh - 20) / 2, 6, 1, tp->bar_border);
    }
    fb_draw_text(fb, gx + 6, gy + 8, "100%", tp->bar_muted, 1);
    fb_draw_text(fb, gx + 6, gy + (gh - 20) / 2 - 4, "50%", tp->bar_muted, 1);
    fb_draw_text(fb, gx + 6, gy + gh - 28, "0%", tp->bar_muted, 1);

    if (scount > 0) {
        int slot_w = (gw - 52) / 32;
        if (slot_w < 8) slot_w = 8;
        int start_x = gx + 44;
        for (int i = 0; i < scount; i++) {
            int c = samples[i].cap;
            if (c < 0) c = 0;
            if (c > 100) c = 100;
            int bh = ((gh - 34) * c) / 100;
            if (bh < 3) bh = 3;
            int bx = start_x + i * slot_w;
            int by = gy + gh - 20 - bh;
            uint32_t bcol = (c <= 20) ? 0xD9383A :
                            (strncmp(samples[i].status, "Charging", 8) == 0 ? 0x2E8B57 : tp->accent);
            fb_fill_rect(fb, bx, by, slot_w - 3, bh, bcol);
        }
        struct tm tm0, tm1;
        localtime_r(&samples[0].ts, &tm0);
        localtime_r(&samples[scount - 1].ts, &tm1);
        double hrs = difftime(samples[scount - 1].ts, samples[0].ts) / 3600.0;
        int dcap = samples[scount - 1].cap - samples[0].cap;
        char hinfo[128];
        if (hrs >= 0.05) {
            snprintf(hinfo, sizeof(hinfo), "%02d:%02d (%d%%) -> %02d:%02d (%d%%)  |  %+d%% IN %.1fH (%.1f%%/H)",
                     tm0.tm_hour, tm0.tm_min, samples[0].cap,
                     tm1.tm_hour, tm1.tm_min, samples[scount - 1].cap,
                     dcap, hrs, (double)dcap / hrs);
        } else {
            snprintf(hinfo, sizeof(hinfo), "LOGGED %d SAMPLE%s (LATEST %02d:%02d @ %d%% [%s])",
                     scount, scount == 1 ? "" : "S",
                     tm1.tm_hour, tm1.tm_min, samples[scount - 1].cap, samples[scount - 1].event);
        }
        fb_draw_text(fb, gx + 44, gy + gh - 14, hinfo, tp->bar_muted, 1);
    } else {
        fb_draw_text(fb, gx + 54, gy + 90, "RECORDING BATTERY HISTORY...", tp->bar_muted, 2);
    }

    fb_fill_rect(fb, rx + 20, ry + 480, rw - 40, 2, tp->bar_border);

    fb_draw_text(fb, rx + 24, ry + 502, "SYSTEM POWER", tp->accent, 2);
    fb_draw_button(fb, tp, rx + 24,  ry + 544, 270, 58, "SLEEP SCREEN", false, false);
    fb_draw_button(fb, tp, rx + 312, ry + 544, 270, 58,
                   st->confirm_action == 1 ? "CONFIRM REBOOT?" : "REBOOT", false, st->confirm_action == 1);
    fb_draw_button(fb, tp, rx + 24,  ry + 620, 270, 58,
                   st->confirm_action == 2 ? "CONFIRM TWRP?" : "BOOT RECOVERY", false, st->confirm_action == 2);
    fb_draw_button(fb, tp, rx + 312, ry + 620, 270, 58,
                   st->confirm_action == 3 ? "CONFIRM OFF?" : "POWER OFF", false, st->confirm_action == 3);

    fb_draw_text(fb, rx + 24, ry + rh - 36, "E-TAB 5 BARE NOTE OS  |  LINUX 3.4.67 ARMV7", tp->bar_muted, 2);

    if (st->osk.open) {
        osk_render_full(fb, theme, &st->osk);
    }
}

SettingsAction settings_handle_tap(FB *fb, SettingsState *st, int x, int y,
                                   OskPurpose *out_purpose, char *out_text, size_t out_sz) {
    if (st->osk.open) {
        OskPurpose p = OSK_NONE;
        char text[128] = {0}, ssid[64] = {0};
        SettingsAction act = osk_handle_tap(fb, &st->osk, x, y, &p, text, sizeof(text), ssid, sizeof(ssid));
        if (act == SET_ACT_OSK_DONE) {
            if (p == OSK_WIFI_PASS) {
                settings_connect_wifi(st, ssid, text);
                return SET_ACT_REDRAW;
            }
            if (out_purpose) *out_purpose = p;
            if (out_text && out_sz > 0) snprintf(out_text, out_sz, "%s", text);
        }
        return act;
    }
    if (!st->open) return SET_ACT_NONE;

    if (hit_btn(fb, x, y, 14, 9, 130, 40)) {
        st->open = false;
        st->confirm_action = 0;
        return SET_ACT_CLOSE;
    }

    int lx = 32, ly = TOOLBAR_H + 24, lw = 910;
    int rx = 974, ry = ly, rw = fb->w - rx - 32;

    if (hit_btn(fb, x, y, lx + 24, ly + 112, 168, 44)) {
        st->confirm_action = 0;
        settings_scan_wifi(st);
        settings_log_event("WIFI SCAN (%d NETS)", st->net_count);
        return SET_ACT_REDRAW;
    }
    if (hit_btn(fb, x, y, lx + 204, ly + 112, 176, 44)) {
        char res[128];
        wpa_cmd("reconnect", NULL, NULL, NULL, res, sizeof(res));
        snprintf(st->status_msg, sizeof(st->status_msg), "RECONNECTING...");
        usleep(400000);
        settings_refresh(st);
        settings_log_event("WIFI RECONNECT (%s)", st->ssid);
        return SET_ACT_REDRAW;
    }
    if (hit_btn(fb, x, y, lx + 392, ly + 112, 184, 44)) {
        char res[128];
        wpa_cmd("disconnect", NULL, NULL, NULL, res, sizeof(res));
        snprintf(st->status_msg, sizeof(st->status_msg), "DISCONNECTED");
        settings_refresh(st);
        settings_log_event("WIFI DISCONNECTED");
        return SET_ACT_REDRAW;
    }
    if (hit_btn(fb, x, y, lx + 588, ly + 112, 176, 44)) {
        restart_adbd_tcp(st);
        settings_refresh(st);
        settings_log_event("ADBD RESTARTED :5555");
        return SET_ACT_REDRAW;
    }
    if (hit_btn(fb, x, y, lx + 776, ly + 112, 110, 44)) {
        settings_refresh(st);
        snprintf(st->status_msg, sizeof(st->status_msg), "STATUS REFRESHED");
        return SET_ACT_REDRAW;
    }

    int ny = ly + 210;
    int max_nets = 5;
    for (int i = 0; i < st->net_count && i < max_nets; i++) {
        if (hit_btn(fb, x, y, lx + 24, ny, lw - 48, 46)) {
            st->confirm_action = 0;
            if (!st->nets[i].secured) {
                settings_connect_wifi(st, st->nets[i].ssid, "");
                settings_log_event("WIFI CONNECT %s", st->nets[i].ssid);
            } else {
                char prompt[96];
                snprintf(prompt, sizeof(prompt), "WIFI PASSWORD FOR %s:", st->nets[i].ssid);
                osk_open(&st->osk, OSK_WIFI_PASS, prompt, "", st->nets[i].ssid);
            }
            return SET_ACT_REDRAW;
        }
        ny += 52;
    }

    /* Clear event log button */
    int ey = ly + 482;
    if (hit_btn(fb, x, y, lx + lw - 154, ey + 8, 130, 34)) {
        unlink(EVENT_LOG_PATH);
        settings_log_event("EVENT LOG CLEARED");
        snprintf(st->status_msg, sizeof(st->status_msg), "EVENT LOG CLEARED");
        return SET_ACT_REDRAW;
    }

    if (hit_btn(fb, x, y, rx + 24, ry + 52, 74, 48)) {
        int b = fb_get_backlight() - 25;
        if (b < 15) b = 15;
        fb_set_backlight(b);
        return SET_ACT_REDRAW;
    }
    if (hit_btn(fb, x, y, rx + rw - 98, ry + 52, 74, 48)) {
        int b = fb_get_backlight() + 25;
        if (b > 255) b = 255;
        fb_set_backlight(b);
        return SET_ACT_REDRAW;
    }
    int bar_x = rx + 112, bar_y = ry + 52, bar_w = rw - 224, bar_h = 48;
    if (hit_rect(x, y, bar_x, bar_y, bar_w, bar_h)) {
        int b = 15 + ((x - bar_x) * 240) / bar_w;
        if (b < 15) b = 15;
        if (b > 255) b = 255;
        fb_set_backlight(b);
        return SET_ACT_REDRAW;
    }

    /* Theme & Paper buttons inside Settings */
    int half_w = (rw - 64) / 2;
    if (hit_btn(fb, x, y, rx + 24, ry + 112, half_w, 48)) {
        return SET_ACT_CYCLE_THEME;
    }
    if (hit_btn(fb, x, y, rx + 40 + half_w, ry + 112, half_w, 48)) {
        return SET_ACT_CYCLE_PAPER;
    }

    if (hit_btn(fb, x, y, rx + 24, ry + 544, 270, 58)) {
        st->confirm_action = 0;
        return SET_ACT_SLEEP;
    }
    if (hit_btn(fb, x, y, rx + 312, ry + 544, 270, 58)) {
        if (st->confirm_action != 1) { st->confirm_action = 1; return SET_ACT_REDRAW; }
        char out[64];
        char *argv[] = { (char *)"/system/bin/reboot", NULL };
        run_cmd_capture(argv, out, sizeof(out));
        return SET_ACT_NONE;
    }
    if (hit_btn(fb, x, y, rx + 24, ry + 620, 270, 58)) {
        if (st->confirm_action != 2) { st->confirm_action = 2; return SET_ACT_REDRAW; }
        char out[64];
        char *argv[] = { (char *)"/system/bin/reboot", (char *)"recovery", NULL };
        run_cmd_capture(argv, out, sizeof(out));
        return SET_ACT_NONE;
    }
    if (hit_btn(fb, x, y, rx + 312, ry + 620, 270, 58)) {
        if (st->confirm_action != 3) { st->confirm_action = 3; return SET_ACT_REDRAW; }
        char out[64];
        char *argv[] = { (char *)"/system/bin/reboot", (char *)"-p", NULL };
        run_cmd_capture(argv, out, sizeof(out));
        return SET_ACT_NONE;
    }

    st->confirm_action = 0;
    return SET_ACT_REDRAW;
}
