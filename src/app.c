#define _GNU_SOURCE
#include "app.h"
#include "fb.h"
#include "prefs.h"
#include "render.h"
#include "settings.h"
#include "storage.h"
#include "ui.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void write_sysfs_str(const char *path, const char *val) {
    FILE *f = fopen(path, "w");
    if (f) {
        fputs(val, f);
        fclose(f);
    }
}

static void set_power_profile(bool low_power) {
    /* 1. Wi-Fi IEEE 802.11 power-save mode (DTIM beacon listen) */
    if (access("/system/bin/wpa_cli", X_OK) == 0) {
        if (low_power) {
            system("/system/bin/wpa_cli -p /data/misc/wifi/sockets -i wlan0 set ps 1 >/dev/null 2>&1");
        } else {
            system("/system/bin/wpa_cli -p /data/misc/wifi/sockets -i wlan0 set ps 1 >/dev/null 2>&1");
        }
    }
    /* 2. CPU frequency floor / governor & MediaTek Smart-Book suspend state */
    if (low_power) {
        write_sysfs_str("/sys/devices/system/cpu/cpu0/cpufreq/scaling_governor", "powersave\n");
        write_sysfs_str("/sys/power/sb_state", "suspend\n");
        /* Enter Linux kernel suspend-to-RAM (blocks until hardware KEY_POWER or wakeup IRQ;
           immediately returns if charger wakelock is active while plugged in) */
        sync();
        write_sysfs_str("/sys/power/state", "mem\n");
    } else {
        write_sysfs_str("/sys/power/sb_state", "resume\n");
        write_sysfs_str("/sys/devices/system/cpu/cpu0/cpufreq/scaling_governor", "hotplug\n");
    }
}

int app_canvas_clip_x0(const App *app) {
    return app->sidebar_open ? SIDEBAR_W : 0;
}

void app_pretty_folder_name(const char *name, char *out, size_t sz) {
    const char *base = strrchr(name, '/');
    const char *src = base ? (base + 1) : name;
    size_t i = 0;
    for (; src[i] && i + 1 < sz; i++) {
        out[i] = (src[i] == '_') ? ' ' : src[i];
    }
    out[i] = '\0';
}

void app_refresh_browser(App *app) {
    app->folder_count = storage_list_folders(STORAGE_DIR, app->cur_folder, app->folders, MAX_FOLDERS);
    app->md_count = storage_list_md(STORAGE_DIR, app->cur_folder, app->md_notes, MAX_NOTES);
    app->note_count = storage_list(STORAGE_DIR, app->cur_folder, app->notes, MAX_NOTES);
}

void app_save_current(App *app) {
    if (app->md.active) return;
    if (app->doc.dirty) {
        if (storage_save(STORAGE_DIR, &app->doc)) {
            app->doc.dirty = false;
            app_refresh_browser(app);
        }
    }
}

void app_open_or_create_first_note(App *app) {
    if (app->pref_note[0]) {
        size_t len = strlen(app->pref_note);
        if (len > 3 && strcmp(app->pref_note + len - 3, ".md") == 0) {
            if (md_load(&app->md, STORAGE_DIR, app->pref_folder, app->pref_note)) {
                snprintf(app->cur_folder, sizeof(app->cur_folder), "%s", app->pref_folder);
                app->doc.theme = (uint8_t)app->pref_theme;
                app->doc.paper = (uint8_t)app->pref_paper;
                app_refresh_browser(app);
                return;
            }
        } else if (storage_load(STORAGE_DIR, app->pref_folder, app->pref_note, &app->doc)) {
            md_free(&app->md);
            snprintf(app->cur_folder, sizeof(app->cur_folder), "%s", app->pref_folder);
            app->doc.theme = (uint8_t)app->pref_theme;
            app->doc.paper = (uint8_t)app->pref_paper;
            app_refresh_browser(app);
            return;
        }
    }
    app_refresh_browser(app);
    if (app->note_count > 0 &&
        storage_load(STORAGE_DIR, app->cur_folder, app->notes[0].id, &app->doc)) {
        md_free(&app->md);
        app->doc.theme = (uint8_t)app->pref_theme;
        app->doc.paper = (uint8_t)app->pref_paper;
        return;
    }
    if (app->md_count > 0 &&
        md_load(&app->md, STORAGE_DIR, app->cur_folder, app->md_notes[0].filename)) {
        app->doc.theme = (uint8_t)app->pref_theme;
        app->doc.paper = (uint8_t)app->pref_paper;
        return;
    }
    char id[64], title[64];
    storage_next_id(STORAGE_DIR, app->cur_folder, id, sizeof(id), title, sizeof(title));
    md_free(&app->md);
    doc_init_new(&app->doc, app->cur_folder, id, title, app->pref_paper, app->pref_theme);
    storage_save(STORAGE_DIR, &app->doc);
    app->doc.dirty = false;
    app_refresh_browser(app);
}

void app_finish_active_stroke(App *app) {
    if (!app->drawing) return;
    app->drawing = false;
    if (app->active_stroke.count > 0) {
        Page *pg = &app->doc.pages[app->doc.cur_page];
        page_push_stroke(pg, app->active_stroke);
        memset(&app->active_stroke, 0, sizeof(app->active_stroke));
        app->doc.dirty = true;
        app_save_current(app);
    } else {
        stroke_free(&app->active_stroke);
    }
}

void app_redraw_all(App *app) {
    if (app->settings.open) {
        settings_render(&app->fb, (ThemeKind)app->doc.theme, &app->settings);
        return;
    }
    if (app->md.active) {
        md_render(&app->fb, (ThemeKind)app->doc.theme, &app->md, app_canvas_clip_x0(app));
    } else {
        render_page(&app->fb, &app->doc, app_canvas_clip_x0(app), app->selected_img, app->selected_text);
    }
    ui_draw_sidebar(app);
    ui_draw_toolbar(app);
    if (app->toast_msg[0] && time(NULL) <= app->toast_until) {
        const ThemePalette *tp = &THEMES[app->doc.theme];
        int tw = fb_text_width(app->toast_msg, 2) + 40;
        int tx = (app->fb.w - tw) / 2;
        int ty = TOOLBAR_H + 14;
        fb_fill_rect(&app->fb, tx, ty, tw, 42, tp->accent);
        fb_draw_text(&app->fb, tx + 20, ty + 14, app->toast_msg, 0xFFFFFF, 2);
    }
    if (app->radial.open) {
        render_radial_menu(&app->fb, (ThemeKind)app->doc.theme, &app->radial,
                           app->tool, app->size_idx, app->color_idx);
    }
    if (app->settings.osk.open) {
        settings_render(&app->fb, (ThemeKind)app->doc.theme, &app->settings);
    }
}

void app_toggle_sleep(App *app) {
    if (!app->sleeping) {
        app_finish_active_stroke(app);
        app->radial.open = false;
        app_save_current(app);
        prefs_save(app);
        app->saved_brightness = fb_get_backlight();
        if (app->saved_brightness < 40) app->saved_brightness = 180;
        fb_set_backlight(0);
        fb_blank(&app->fb, true);
        app->sleeping = true;
        settings_log_battery("SLEEP");
        set_power_profile(true);
    } else {
        app->sleeping = false;
        app->last_activity_time = time(NULL);
        set_power_profile(false);
        settings_log_battery("WAKE");
        fb_blank(&app->fb, false);
        fb_set_backlight(app->saved_brightness);
        fb_mark_all(&app->fb);
        app_redraw_all(app);
        fb_present(&app->fb, false, 0, 0, false, app_canvas_clip_x0(app));
        if (app->fb.double_buf) {
            fb_present(&app->fb, false, 0, 0, false, app_canvas_clip_x0(app));
        }
    }
}
