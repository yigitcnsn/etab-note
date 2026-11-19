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
#include <string.h>

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
    } else {
        app->sleeping = false;
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
