#define _GNU_SOURCE
#include "ui.h"
#include "fb.h"
#include "md.h"
#include "osk.h"
#include "prefs.h"
#include "render.h"
#include "settings.h"
#include "storage.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

static bool hit_rect(int px, int py, int x, int y, int w, int h) {
    return px >= x && px < x + w && py >= y && py < y + h;
}

static int read_int_sysfs(const char *path, int fallback) {
    FILE *f = fopen(path, "r");
    if (!f) return fallback;
    int v = fallback;
    if (fscanf(f, "%d", &v) != 1) v = fallback;
    fclose(f);
    return v;
}

static bool is_charging(void) {
    FILE *f = fopen("/sys/class/power_supply/battery/status", "r");
    if (!f) return false;
    char buf[32] = {0};
    bool chg = (fgets(buf, sizeof(buf), f) != NULL && strncmp(buf, "Charging", 8) == 0);
    fclose(f);
    return chg;
}

static void draw_status_right(App *app, const ThemePalette *tp) {
    FB *fb = &app->fb;
    int bat = read_int_sysfs("/sys/class/power_supply/battery/capacity", -1);
    time_t now = time(NULL);
    struct tm tm_now;
    localtime_r(&now, &tm_now);
    char sbuf[48];
    snprintf(sbuf, sizeof(sbuf), "%d%%%s %02d:%02d",
             bat, is_charging() ? "+" : "", tm_now.tm_hour, tm_now.tm_min);
    int stw = fb_text_width(sbuf, 2);
    fb_draw_text(fb, fb->w - stw - 18, 22, sbuf, tp->bar_muted, 2);
}

void ui_draw_toolbar(App *app) {
    FB *fb = &app->fb;
    const ThemePalette *tp = &THEMES[app->doc.theme];
    const char *paper_names[PAPER_COUNT] = { "LINED", "GRID", "BLANK" };

    fb_fill_rect(fb, 0, 0, fb->w, TOOLBAR_H, tp->bar_bg);
    fb_fill_rect(fb, 0, TOOLBAR_H - 2, fb->w, 2, tp->bar_border);

    int by = 9, bh = 40;
    fb_draw_button(fb, tp, 12, by, 114, bh, "NOTES", app->sidebar_open, false);
    fb_fill_rect(fb, 136, 12, 2, 34, tp->bar_border);

    /* Markdown viewer toolbar */
    if (app->md.active) {
        char hdr[64];
        snprintf(hdr, sizeof(hdr), "MD: %.36s", app->md.title);
        fb_draw_text(fb, 154, 22, hdr, tp->bar_text, 2);

        fb_draw_button(fb, tp, 950,  by, 116, bh, "+ TASK", true, false);
        fb_draw_button(fb, tp, 1076, by, 116, bh, "+ LINE", false, false);
        fb_draw_button(fb, tp, 1210, by, 72,  bh, "UP", false, false);
        fb_draw_button(fb, tp, 1290, by, 72,  bh, "DN", false, false);
        fb_draw_button(fb, tp, 1380, by, 100, bh, tp->name, false, false);
        fb_draw_button(fb, tp, 1490, by, 122, bh, "NEW DRAW", false, false);
        fb_draw_button(fb, tp, 1622, by, 72,  bh, "SET", app->settings.open, false);
        draw_status_right(app, tp);
        return;
    }

    /* Drawing canvas toolbar */
    const char *pen_lbl = "PEN";
    bool is_pen_family = (app->tool == TOOL_PEN || app->tool == TOOL_BRUSH || app->tool == TOOL_PENCIL);
    if (app->tool == TOOL_BRUSH) pen_lbl = "BRUSH";
    else if (app->tool == TOOL_PENCIL) pen_lbl = "PENCIL";
    fb_draw_button(fb, tp, 146, by, 102, bh, pen_lbl, is_pen_family, false);
    fb_draw_button(fb, tp, 254, by, 58, bh, "HI", app->tool == TOOL_HIGHLIGHTER, false);
    fb_draw_button(fb, tp, 318, by, 92, bh, "ERASE", app->tool == TOOL_ERASER, false);
    fb_fill_rect(fb, 420, 12, 2, 34, tp->bar_border);

    fb_draw_button(fb, tp, 430, by, 44, bh, "S", app->size_idx == 0, false);
    fb_draw_button(fb, tp, 480, by, 44, bh, "M", app->size_idx == 1, false);
    fb_draw_button(fb, tp, 530, by, 44, bh, "L", app->size_idx == 2, false);
    fb_fill_rect(fb, 584, 12, 2, 34, tp->bar_border);

    for (int i = 0; i < 5; i++) {
        int cx = 614 + i * 44;
        int cy = TOOLBAR_H / 2;
        uint32_t col = (i == 0) ? tp->primary_ink : SWATCH_RGB[i];
        if (app->color_idx == (uint8_t)i) {
            fb_fill_circle(fb, cx, cy, 18, tp->accent);
            fb_fill_circle(fb, cx, cy, 15, tp->bar_bg);
        }
        fb_fill_circle(fb, cx, cy, 12, col);
    }
    fb_fill_rect(fb, 820, 12, 2, 34, tp->bar_border);

    Page *pg = &app->doc.pages[app->doc.cur_page];
    fb_draw_button(fb, tp, 832, by, 86, bh, "UNDO", false, false);
    if (pg->count == 0) fb_stroke_rect(fb, 832, by, 86, bh, 2, tp->bar_bg);
    fb_draw_button(fb, tp, 924, by, 86, bh, "REDO", false, false);
    if (pg->redo_count == 0) fb_stroke_rect(fb, 924, by, 86, bh, 2, tp->bar_bg);

    fb_draw_button(fb, tp, 1018, by, 100, bh, paper_names[app->doc.paper], false, false);
    fb_draw_button(fb, tp, 1124, by, 100, bh, tp->name, false, false);
    fb_draw_button(fb, tp, 1230, by, 92, bh, app->confirm_clear ? "SURE?" : "CLEAR", false, app->confirm_clear);
    fb_fill_rect(fb, 1334, 12, 2, 34, tp->bar_border);

    fb_draw_button(fb, tp, 1346, by, 44, bh, "<", false, false);
    char pstr[32];
    snprintf(pstr, sizeof(pstr), "%d/%d", app->doc.cur_page + 1, app->doc.page_count);
    int ptw = fb_text_width(pstr, 2);
    fb_draw_text(fb, 1394 + (84 - ptw) / 2, 22, pstr, tp->bar_text, 2);
    fb_draw_button(fb, tp, 1482, by, 44, bh, ">", false, false);
    fb_draw_button(fb, tp, 1530, by, 48, bh, "+P", false, false);
    fb_draw_button(fb, tp, 1582, by, 54, bh, "+T", false, false);
    bool editing = app->obj_edit_mode || app->selected_img >= 0 || app->selected_text >= 0;
    fb_draw_button(fb, tp, 1640, by, 66, bh, "EDIT", editing, false);
    fb_draw_button(fb, tp, 1710, by, 58, bh, "SET", app->settings.open, false);

    draw_status_right(app, tp);
}

static void trunc_str(const char *src, char *dst, size_t max_chars) {
    size_t len = strlen(src);
    if (len <= max_chars) {
        strcpy(dst, src);
    } else {
        memcpy(dst, src, max_chars - 2);
        dst[max_chars - 2] = '.';
        dst[max_chars - 1] = '.';
        dst[max_chars] = '\0';
    }
}

void ui_draw_sidebar(App *app) {
    if (!app->sidebar_open) return;
    FB *fb = &app->fb;
    const ThemePalette *tp = &THEMES[app->doc.theme];

    fb_fill_rect(fb, 0, TOOLBAR_H, SIDEBAR_W, fb->h - TOOLBAR_H, tp->sidebar_bg);
    fb_fill_rect(fb, SIDEBAR_W - 2, TOOLBAR_H, 2, fb->h - TOOLBAR_H, tp->bar_border);

    int y = TOOLBAR_H + 12;
    if (app->move_mode) {
        fb_draw_text(fb, 18, y + 12, "MOVE NOTE TO:", tp->accent, 2);
        fb_draw_button(fb, tp, SIDEBAR_W - 116, y, 102, 42, "CANCEL", false, false);
        y += 58;

        bool is_root = (app->doc.folder[0] == '\0');
        fb_fill_rect(fb, 10, y, SIDEBAR_W - 22, 50, is_root ? tp->sidebar_sel : tp->bar_bg);
        fb_stroke_rect(fb, 10, y, SIDEBAR_W - 22, 50, 1, is_root ? tp->accent : tp->bar_border);
        fb_draw_text(fb, 24, y + 16, "ALL NOTES (ROOT)", tp->bar_text, 2);
        y += 58;

        int max_y = fb->h - 24;
        for (int i = 0; i < app->folder_count && y + 54 <= max_y; i++) {
            char rel[256];
            if (app->cur_folder[0]) snprintf(rel, sizeof(rel), "%s/%s", app->cur_folder, app->folders[i].name);
            else snprintf(rel, sizeof(rel), "%s", app->folders[i].name);
            bool cur = (strcmp(rel, app->doc.folder) == 0);
            fb_fill_rect(fb, 10, y, SIDEBAR_W - 22, 50, cur ? tp->sidebar_sel : tp->bar_bg);
            fb_stroke_rect(fb, 10, y, SIDEBAR_W - 22, 50, 1, cur ? tp->accent : tp->bar_border);
            fb_fill_rect(fb, 20, y + 16, 22, 16, tp->accent);
            fb_fill_rect(fb, 20, y + 12, 10, 5, tp->accent);
            char lbl[32];
            trunc_str(app->folders[i].display, lbl, 18);
            fb_draw_text(fb, 52, y + 16, lbl, tp->bar_text, 2);
            y += 58;
        }
        return;
    }

    /* Breadcrumb / Back header when inside a subfolder */
    if (app->cur_folder[0] != '\0') {
        fb_draw_button(fb, tp, 14, y, 88, 38, "< UP", false, false);
        char fdisp[64], short_f[24];
        app_pretty_folder_name(app->cur_folder, fdisp, sizeof(fdisp));
        trunc_str(fdisp, short_f, 14);
        fb_draw_text(fb, 112, y + 12, short_f, tp->accent, 2);
        y += 46;
    }

    /* Creation row: [+DRAW] [+MD] [+DIR] */
    fb_draw_button(fb, tp, 14,  y, 92, 40, "+DRAW", true, false);
    fb_draw_button(fb, tp, 112, y, 92, 40, "+MD", false, false);
    fb_draw_button(fb, tp, 210, y, 94, 40, "+DIR", false, false);
    y += 50;

    int total_items = app->folder_count + app->md_count + app->note_count;
    int footer_top = app->cur_folder[0] != '\0' ? (fb->h - 180) : (fb->h - 128);
    int row_h = 52;
    int visible_rows = (footer_top - 42 - y) / row_h;
    if (visible_rows < 1) visible_rows = 1;

    if (app->sidebar_scroll > total_items - visible_rows)
        app->sidebar_scroll = total_items - visible_rows;
    if (app->sidebar_scroll < 0) app->sidebar_scroll = 0;

    int drawn = 0;
    for (int idx = app->sidebar_scroll; idx < total_items && drawn < visible_rows; idx++, drawn++) {
        int ry = y + drawn * row_h;
        if (idx < app->folder_count) {
            FolderMeta *fm = &app->folders[idx];
            fb_fill_rect(fb, 10, ry, SIDEBAR_W - 22, 46, tp->bar_bg);
            fb_stroke_rect(fb, 10, ry, SIDEBAR_W - 22, 46, 1, tp->bar_border);
            fb_fill_rect(fb, 18, ry + 14, 22, 16, tp->accent);
            fb_fill_rect(fb, 18, ry + 10, 10, 5, tp->accent);
            char lbl[32];
            trunc_str(fm->display, lbl, 18);
            fb_draw_text(fb, 48, ry + 9, lbl, tp->bar_text, 2);
            char meta[32];
            snprintf(meta, sizeof(meta), "%d item%s", fm->item_count, fm->item_count == 1 ? "" : "s");
            fb_draw_text(fb, 48, ry + 29, meta, tp->bar_muted, 1);
        } else if (idx < app->folder_count + app->md_count) {
            MdMeta *mm = &app->md_notes[idx - app->folder_count];
            bool sel = app->md.active &&
                       strcmp(mm->filename, app->md.filename) == 0 &&
                       strcmp(app->cur_folder, app->md.folder) == 0;
            if (sel) {
                fb_fill_rect(fb, 10, ry, SIDEBAR_W - 22, 46, tp->sidebar_sel);
                fb_fill_rect(fb, 10, ry, 5, 46, tp->accent);
            }
            fb_fill_rect(fb, 18, ry + 12, 32, 22, tp->bar_border);
            fb_draw_text(fb, 22, ry + 16, "MD", tp->accent, 2);
            char lbl[32];
            trunc_str(mm->title, lbl, 17);
            fb_draw_text(fb, 58, ry + 16, lbl, tp->bar_text, 2);
        } else {
            NoteMeta *nm = &app->notes[idx - app->folder_count - app->md_count];
            bool sel = !app->md.active &&
                       strcmp(nm->id, app->doc.id) == 0 &&
                       strcmp(app->cur_folder, app->doc.folder) == 0;
            if (sel) {
                fb_fill_rect(fb, 10, ry, SIDEBAR_W - 22, 46, tp->sidebar_sel);
                fb_fill_rect(fb, 10, ry, 5, 46, tp->accent);
            }
            char lbl[32];
            trunc_str(nm->title, lbl, 19);
            fb_draw_text(fb, 22, ry + 9, lbl, tp->bar_text, 2);
            char meta[32];
            snprintf(meta, sizeof(meta), "DRAW  %d page%s", nm->page_count, nm->page_count == 1 ? "" : "s");
            fb_draw_text(fb, 22, ry + 29, meta, tp->bar_muted, 1);
        }
    }

    /* Sidebar pager if more items than fit */
    if (total_items > visible_rows) {
        int py = footer_top - 40;
        fb_draw_button(fb, tp, 14,  py, 140, 34, "^ MORE", false, false);
        fb_draw_button(fb, tp, 162, py, 142, 34, "v MORE", false, false);
    }

    /* Bottom actions */
    int fy = fb->h - 62;
    if (app->cur_folder[0] != '\0') {
        fb_draw_button(fb, tp, 14, fy - 102, SIDEBAR_W - 30, 42, "RENAME FOLDER", false, false);
    }
    fb_draw_button(fb, tp, 14,  fy - 52, 140, 42, "RENAME", false, false);
    fb_draw_button(fb, tp, 162, fy - 52, 142, 42, "MOVE", false, false);

    const char *del_lbl = app->confirm_delete ? "SURE?" :
        ((app->cur_folder[0] != '\0' && total_items == 0) ? "DEL DIR" : "DELETE");
    fb_draw_button(fb, tp, 14,  fy, 140, 46, del_lbl, false, app->confirm_delete);
    fb_draw_button(fb, tp, 162, fy, 142, 46, "EXIT", false, false);
}

void ui_radial_close(App *app) {
    if (!app->radial.open) return;
    app->radial.open = false;
    app->radial.hover_wedge = -1;
    app->radial.hover_color = -1;
    app_redraw_all(app);
}

void ui_radial_open_at(App *app, int x, int y) {
    if (app->md.active) return;
    app_finish_active_stroke(app);
    int clip = app_canvas_clip_x0(app);
    int min_x = clip + RADIAL_OUTER_R + 16;
    int max_x = app->fb.w - RADIAL_OUTER_R - 16;
    int min_y = TOOLBAR_H + RADIAL_OUTER_R + 16;
    int max_y = app->fb.h - RADIAL_OUTER_R - 74;
    if (x < min_x) x = min_x;
    if (x > max_x) x = max_x;
    if (y < min_y) y = min_y;
    if (y > max_y) y = max_y;

    app->radial.open = true;
    app->radial.cx = x;
    app->radial.cy = y;
    app->radial.hover_wedge = -1;
    app->radial.hover_color = -1;
    app->radial_opened_by_hold = true;
    app_redraw_all(app);
}

void ui_radial_execute_wedge(App *app, int w) {
    Page *pg = &app->doc.pages[app->doc.cur_page];
    switch (w) {
        case 0: app->tool = TOOL_PEN; prefs_save(app); break;
        case 1: app->tool = TOOL_BRUSH; prefs_save(app); break;
        case 2: app->tool = TOOL_PENCIL; prefs_save(app); break;
        case 3: app->tool = TOOL_HIGHLIGHTER; prefs_save(app); break;
        case 4: app->tool = TOOL_ERASER; prefs_save(app); break;
        case 5: app->size_idx = (uint8_t)((app->size_idx + 1) % 3); prefs_save(app); break;
        case 6:
            if (page_undo(pg)) { app->doc.dirty = true; app_save_current(app); }
            break;
        case 7:
            if (page_redo(pg)) { app->doc.dirty = true; app_save_current(app); }
            break;
        default: break;
    }
}

static bool handle_radial_tap(App *app, int x, int y) {
    if (!app->radial.open) return false;
    int w = radial_hit_wedge(&app->radial, x, y);
    if (w >= 0) { ui_radial_execute_wedge(app, w); ui_radial_close(app); return true; }
    int c = radial_hit_color(&app->radial, x, y);
    if (c >= 0) {
        app->color_idx = (uint8_t)c;
        if (app->tool == TOOL_ERASER) app->tool = TOOL_PEN;
        prefs_save(app);
        ui_radial_close(app);
        return true;
    }
    ui_radial_close(app);
    return true;
}

static bool handle_toolbar_tap(App *app, int x, int y) {
    int by = 9, bh = 40;
    if (hit_rect(x, y, 12, by, 114, bh)) {
        app->sidebar_open = !app->sidebar_open;
        app->move_mode = false;
        if (app->sidebar_open) {
            app_save_current(app);
            app_refresh_browser(app);
        }
        app_redraw_all(app);
        return true;
    }

    if (app->md.active) {
        if (hit_rect(x, y, 950, by, 116, bh)) {
            osk_open(&app->settings.osk, OSK_MD_ADD_TASK, "ADD CHECKLIST TASK:", "", NULL);
            app_redraw_all(app);
            return true;
        }
        if (hit_rect(x, y, 1076, by, 116, bh)) {
            osk_open(&app->settings.osk, OSK_MD_ADD_LINE, "APPEND LINE TO NOTE:", "", NULL);
            app_redraw_all(app);
            return true;
        }
        if (hit_rect(x, y, 1210, by, 72, bh)) {
            md_scroll(&app->md, -480, app->fb.h - TOOLBAR_H);
            app_redraw_all(app);
            return true;
        }
        if (hit_rect(x, y, 1290, by, 72, bh)) {
            md_scroll(&app->md, 480, app->fb.h - TOOLBAR_H);
            app_redraw_all(app);
            return true;
        }
        if (hit_rect(x, y, 1380, by, 100, bh)) {
            app->doc.theme = (uint8_t)((app->doc.theme + 1) % THEME_COUNT);
            app->pref_theme = (ThemeKind)app->doc.theme;
            prefs_save(app);
            app_redraw_all(app);
            return true;
        }
        if (hit_rect(x, y, 1490, by, 122, bh)) {
            char id[64], def_title[64];
            storage_next_id(STORAGE_DIR, app->cur_folder, id, sizeof(id), def_title, sizeof(def_title));
            osk_open(&app->settings.osk, OSK_NEW_NOTE, "NEW DRAWING NAME:", def_title, NULL);
            app_redraw_all(app);
            return true;
        }
        if (hit_rect(x, y, 1622, by, 72, bh)) {
            app->settings.open = true;
            app->settings.confirm_action = 0;
            app->settings.status_msg[0] = '\0';
            settings_refresh(&app->settings);
            app_redraw_all(app);
            return true;
        }
        return true;
    }

    if (!hit_rect(x, y, 1230, by, 92, bh)) app->confirm_clear = false;
    app->confirm_delete = false;

    if (hit_rect(x, y, 146, by, 102, bh)) {
        if (app->tool == TOOL_PEN) app->tool = TOOL_BRUSH;
        else if (app->tool == TOOL_BRUSH) app->tool = TOOL_PENCIL;
        else app->tool = TOOL_PEN;
        prefs_save(app);
        ui_draw_toolbar(app);
        return true;
    }
    if (hit_rect(x, y, 254, by, 58, bh)) { app->tool = TOOL_HIGHLIGHTER; prefs_save(app); ui_draw_toolbar(app); return true; }
    if (hit_rect(x, y, 318, by, 92, bh)) { app->tool = TOOL_ERASER; prefs_save(app); ui_draw_toolbar(app); return true; }

    if (hit_rect(x, y, 430, by, 44, bh)) { app->size_idx = 0; prefs_save(app); ui_draw_toolbar(app); return true; }
    if (hit_rect(x, y, 480, by, 44, bh)) { app->size_idx = 1; prefs_save(app); ui_draw_toolbar(app); return true; }
    if (hit_rect(x, y, 530, by, 44, bh)) { app->size_idx = 2; prefs_save(app); ui_draw_toolbar(app); return true; }

    for (int i = 0; i < 5; i++) {
        int cx = 614 + i * 44, cy = TOOLBAR_H / 2;
        int dx = x - cx, dy = y - cy;
        if (dx * dx + dy * dy <= 22 * 22) {
            app->color_idx = (uint8_t)i;
            if (app->tool == TOOL_ERASER) app->tool = TOOL_PEN;
            prefs_save(app);
            ui_draw_toolbar(app);
            return true;
        }
    }

    Page *pg = &app->doc.pages[app->doc.cur_page];
    if (hit_rect(x, y, 832, by, 86, bh)) {
        if (page_undo(pg)) { app->doc.dirty = true; app_save_current(app); app_redraw_all(app); }
        return true;
    }
    if (hit_rect(x, y, 924, by, 86, bh)) {
        if (page_redo(pg)) { app->doc.dirty = true; app_save_current(app); app_redraw_all(app); }
        return true;
    }
    if (hit_rect(x, y, 1018, by, 100, bh)) {
        app->doc.paper = (uint8_t)((app->doc.paper + 1) % PAPER_COUNT);
        app->pref_paper = (PaperKind)app->doc.paper;
        app->doc.dirty = true;
        app_save_current(app);
        prefs_save(app);
        app_redraw_all(app);
        return true;
    }
    if (hit_rect(x, y, 1124, by, 100, bh)) {
        app->doc.theme = (uint8_t)((app->doc.theme + 1) % THEME_COUNT);
        app->pref_theme = (ThemeKind)app->doc.theme;
        app->doc.dirty = true;
        app_save_current(app);
        prefs_save(app);
        app_redraw_all(app);
        return true;
    }
    if (hit_rect(x, y, 1230, by, 92, bh)) {
        if (!app->confirm_clear) {
            app->confirm_clear = true;
            ui_draw_toolbar(app);
        } else {
            app->confirm_clear = false;
            page_clear(pg);
            app->selected_img = -1;
            app->selected_text = -1;
            app->doc.dirty = true;
            app_save_current(app);
            app_redraw_all(app);
        }
        return true;
    }
    if (hit_rect(x, y, 1346, by, 44, bh)) {
        if (app->doc.cur_page > 0) {
            app->doc.cur_page--;
            app->selected_img = -1;
            app->selected_text = -1;
            app->obj_edit_mode = false;
            app->doc.dirty = true;
            app_save_current(app);
            app_redraw_all(app);
        }
        return true;
    }
    if (hit_rect(x, y, 1482, by, 44, bh)) {
        if (app->doc.cur_page + 1 < app->doc.page_count) {
            app->doc.cur_page++;
            app->selected_img = -1;
            app->selected_text = -1;
            app->obj_edit_mode = false;
            app->doc.dirty = true;
            app_save_current(app);
            app_redraw_all(app);
        }
        return true;
    }
    if (hit_rect(x, y, 1530, by, 48, bh)) {
        if (app->doc.page_count < MAX_PAGES) {
            page_init(&app->doc.pages[app->doc.page_count]);
            app->doc.cur_page = app->doc.page_count;
            app->doc.page_count++;
            app->selected_img = -1;
            app->selected_text = -1;
            app->obj_edit_mode = false;
            app->doc.dirty = true;
            app_save_current(app);
            app_redraw_all(app);
        }
        return true;
    }
    if (hit_rect(x, y, 1582, by, 54, bh)) {
        app_finish_active_stroke(app);
        osk_open(&app->settings.osk, OSK_NEW_TEXTBOX, "NEW TEXT BOX:", "", NULL);
        app_redraw_all(app);
        return true;
    }
    if (hit_rect(x, y, 1640, by, 66, bh)) {
        app_finish_active_stroke(app);
        if (app->obj_edit_mode || app->selected_img >= 0 || app->selected_text >= 0) {
            app->obj_edit_mode = false;
            app->selected_img = -1;
            app->selected_text = -1;
            app->obj_drag = OBJ_DRAG_NONE;
        } else {
            app->obj_edit_mode = true;
            if (pg->text_count > 0) app->selected_text = (int)pg->text_count - 1;
            else if (pg->img_count > 0) app->selected_img = (int)pg->img_count - 1;
        }
        app_redraw_all(app);
        return true;
    }
    if (hit_rect(x, y, 1710, by, 58, bh)) {
        app_finish_active_stroke(app);
        app_save_current(app);
        app->radial.open = false;
        app->selected_img = -1;
        app->selected_text = -1;
        app->obj_edit_mode = false;
        app->settings.open = true;
        app->settings.confirm_action = 0;
        app->settings.status_msg[0] = '\0';
        settings_refresh(&app->settings);
        app_redraw_all(app);
        return true;
    }
    return true;
}

static bool handle_sidebar_move_tap(App *app, int x, int y) {
    int my = TOOLBAR_H + 12;
    if (hit_rect(x, y, SIDEBAR_W - 116, my, 102, 42)) {
        app->move_mode = false;
        ui_draw_sidebar(app);
        return true;
    }
    my += 58;
    if (hit_rect(x, y, 10, my, SIDEBAR_W - 22, 50)) {
        if (!app->md.active) {
            app_save_current(app);
            char new_id[64] = {0};
            if (storage_move_note(STORAGE_DIR, app->doc.folder, app->doc.id, "", new_id, sizeof(new_id))) {
                app->doc.folder[0] = '\0';
                snprintf(app->doc.id, sizeof(app->doc.id), "%s", new_id);
                app->cur_folder[0] = '\0';
                prefs_save(app);
            }
        }
        app->move_mode = false;
        app_refresh_browser(app);
        ui_draw_sidebar(app);
        return true;
    }
    my += 58;
    int max_my = app->fb.h - 24;
    for (int i = 0; i < app->folder_count && my + 54 <= max_my; i++) {
        if (hit_rect(x, y, 10, my, SIDEBAR_W - 22, 50)) {
            if (!app->md.active) {
                app_save_current(app);
                char rel[256];
                if (app->cur_folder[0]) snprintf(rel, sizeof(rel), "%s/%s", app->cur_folder, app->folders[i].name);
                else snprintf(rel, sizeof(rel), "%s", app->folders[i].name);
                char new_id[64] = {0};
                if (storage_move_note(STORAGE_DIR, app->doc.folder, app->doc.id, rel, new_id, sizeof(new_id))) {
                    snprintf(app->doc.folder, sizeof(app->doc.folder), "%s", rel);
                    snprintf(app->doc.id, sizeof(app->doc.id), "%s", new_id);
                    snprintf(app->cur_folder, sizeof(app->cur_folder), "%s", rel);
                    prefs_save(app);
                }
            }
            app->move_mode = false;
            app_refresh_browser(app);
            ui_draw_sidebar(app);
            return true;
        }
        my += 58;
    }
    return true;
}

static void pop_folder_level(char *folder) {
    char *slash = strrchr(folder, '/');
    if (slash) *slash = '\0';
    else folder[0] = '\0';
}

static bool handle_sidebar_tap(App *app, int x, int y) {
    if (app->move_mode) return handle_sidebar_move_tap(app, x, y);

    int fy = app->fb.h - 62;
    if (!hit_rect(x, y, 14, fy, 140, 46)) app->confirm_delete = false;
    app->confirm_clear = false;

    int ry = TOOLBAR_H + 12;
    if (app->cur_folder[0] != '\0') {
        if (hit_rect(x, y, 14, ry, 88, 38)) {
            pop_folder_level(app->cur_folder);
            app->sidebar_scroll = 0;
            app->confirm_delete = false;
            app_refresh_browser(app);
            ui_draw_sidebar(app);
            return true;
        }
        ry += 46;
    }

    /* Creation row: [+DRAW] [+MD] [+DIR] */
    if (hit_rect(x, y, 14, ry, 92, 40)) {
        char id[64], def_title[64];
        storage_next_id(STORAGE_DIR, app->cur_folder, id, sizeof(id), def_title, sizeof(def_title));
        osk_open(&app->settings.osk, OSK_NEW_NOTE, "NEW DRAWING NAME:", def_title, NULL);
        app_redraw_all(app);
        return true;
    }
    if (hit_rect(x, y, 112, ry, 92, 40)) {
        osk_open(&app->settings.osk, OSK_NEW_MD, "NEW MARKDOWN NOTE:", "", NULL);
        app_redraw_all(app);
        return true;
    }
    if (hit_rect(x, y, 210, ry, 94, 40)) {
        osk_open(&app->settings.osk, OSK_NEW_FOLDER, "NEW FOLDER NAME:", "", NULL);
        app_redraw_all(app);
        return true;
    }
    ry += 50;

    int total_items = app->folder_count + app->md_count + app->note_count;
    int footer_top = app->cur_folder[0] != '\0' ? (app->fb.h - 180) : (app->fb.h - 128);
    int row_h = 52;
    int visible_rows = (footer_top - 42 - ry) / row_h;
    if (visible_rows < 1) visible_rows = 1;

    /* Sidebar pager */
    if (total_items > visible_rows) {
        int py = footer_top - 40;
        if (hit_rect(x, y, 14, py, 140, 34)) {
            app->sidebar_scroll -= 5;
            if (app->sidebar_scroll < 0) app->sidebar_scroll = 0;
            ui_draw_sidebar(app);
            return true;
        }
        if (hit_rect(x, y, 162, py, 142, 34)) {
            app->sidebar_scroll += 5;
            ui_draw_sidebar(app);
            return true;
        }
    }

    /* Bottom action buttons */
    if (app->cur_folder[0] != '\0' && hit_rect(x, y, 14, fy - 102, SIDEBAR_W - 30, 42)) {
        char fdisp[64];
        app_pretty_folder_name(app->cur_folder, fdisp, sizeof(fdisp));
        osk_open(&app->settings.osk, OSK_RENAME_FOLDER, "RENAME FOLDER:", fdisp, NULL);
        app_redraw_all(app);
        return true;
    }
    if (hit_rect(x, y, 14, fy - 52, 140, 42)) {
        if (!app->md.active) {
            osk_open(&app->settings.osk, OSK_RENAME_NOTE, "RENAME CURRENT DRAWING:", app->doc.title, NULL);
            app_redraw_all(app);
        }
        return true;
    }
    if (hit_rect(x, y, 162, fy - 52, 142, 42)) {
        if (!app->md.active) {
            app_refresh_browser(app);
            app->move_mode = true;
            ui_draw_sidebar(app);
        }
        return true;
    }
    if (hit_rect(x, y, 14, fy, 140, 46)) {
        if (!app->confirm_delete) {
            app->confirm_delete = true;
            ui_draw_sidebar(app);
        } else {
            app->confirm_delete = false;
            if (app->cur_folder[0] != '\0' && total_items == 0) {
                storage_delete_folder(STORAGE_DIR, app->cur_folder);
                pop_folder_level(app->cur_folder);
                app->sidebar_scroll = 0;
                app_refresh_browser(app);
                ui_draw_sidebar(app);
            } else if (app->md.active) {
                storage_delete_md(STORAGE_DIR, app->md.folder, app->md.filename);
                md_free(&app->md);
                app_open_or_create_first_note(app);
                app_redraw_all(app);
            } else {
                storage_delete(STORAGE_DIR, app->doc.folder, app->doc.id);
                doc_free(&app->doc);
                app_open_or_create_first_note(app);
                app_redraw_all(app);
            }
        }
        return true;
    }
    if (hit_rect(x, y, 162, fy, 142, 46)) {
        app_save_current(app);
        app->quit = true;
        return true;
    }

    /* Item list rows */
    int drawn = 0;
    for (int idx = app->sidebar_scroll; idx < total_items && drawn < visible_rows; idx++, drawn++) {
        int item_y = ry + drawn * row_h;
        if (!hit_rect(x, y, 10, item_y, SIDEBAR_W - 22, 46)) continue;

        if (idx < app->folder_count) {
            char next_dir[256];
            if (app->cur_folder[0])
                snprintf(next_dir, sizeof(next_dir), "%s/%s", app->cur_folder, app->folders[idx].name);
            else
                snprintf(next_dir, sizeof(next_dir), "%s", app->folders[idx].name);
            snprintf(app->cur_folder, sizeof(app->cur_folder), "%s", next_dir);
            app->sidebar_scroll = 0;
            app->confirm_delete = false;
            app_refresh_browser(app);
            ui_draw_sidebar(app);
            return true;
        } else if (idx < app->folder_count + app->md_count) {
            MdMeta *mm = &app->md_notes[idx - app->folder_count];
            app_save_current(app);
            if (md_load(&app->md, STORAGE_DIR, app->cur_folder, mm->filename)) {
                app->selected_img = -1;
                app->selected_text = -1;
                app->obj_edit_mode = false;
                app->sidebar_open = false;
                prefs_save(app);
                app_redraw_all(app);
            }
            return true;
        } else {
            NoteMeta *nm = &app->notes[idx - app->folder_count - app->md_count];
            app_save_current(app);
            md_free(&app->md);
            storage_load(STORAGE_DIR, app->cur_folder, nm->id, &app->doc);
            app->doc.theme = (uint8_t)app->pref_theme;
            app->doc.paper = (uint8_t)app->pref_paper;
            app->selected_img = -1;
            app->selected_text = -1;
            app->obj_edit_mode = false;
            app->sidebar_open = false;
            prefs_save(app);
            app_redraw_all(app);
            return true;
        }
    }
    return true;
}

bool ui_handle_tap(App *app, int x, int y) {
    if (app->settings.open || app->settings.osk.open) {
        char text[256] = {0};
        OskPurpose purpose = OSK_NONE;
        SettingsAction act = settings_handle_tap(&app->fb, &app->settings, x, y,
                                                 &purpose, text, sizeof(text));
        if (act == SET_ACT_SLEEP) { app_toggle_sleep(app); return true; }
        if (act == SET_ACT_OSK_FAST) {
            osk_render_fast(&app->fb, (ThemeKind)app->doc.theme, &app->settings.osk);
            return true;
        }
        if (act == SET_ACT_OSK_DONE) {
            if (purpose == OSK_RENAME_NOTE && text[0]) {
                snprintf(app->doc.title, sizeof(app->doc.title), "%s", text);
                app->doc.dirty = true;
                app_save_current(app);
            } else if (purpose == OSK_NEW_NOTE) {
                app_save_current(app);
                md_free(&app->md);
                char id[64], def_title[64];
                storage_next_id(STORAGE_DIR, app->cur_folder, id, sizeof(id), def_title, sizeof(def_title));
                const char *title = text[0] ? text : def_title;
                doc_free(&app->doc);
                doc_init_new(&app->doc, app->cur_folder, id, title, app->pref_paper, app->pref_theme);
                storage_save(STORAGE_DIR, &app->doc);
                app->doc.dirty = false;
                prefs_save(app);
                app_refresh_browser(app);
                app->sidebar_open = false;
                app->selected_img = -1;
                app->selected_text = -1;
                app->obj_edit_mode = false;
            } else if (purpose == OSK_NEW_MD && text[0]) {
                app_save_current(app);
                if (md_create(&app->md, STORAGE_DIR, app->cur_folder, text)) {
                    prefs_save(app);
                    app_refresh_browser(app);
                    app->sidebar_open = false;
                }
            } else if (purpose == OSK_MD_ADD_TASK && text[0] && app->md.active) {
                char line[300];
                snprintf(line, sizeof(line), "- [ ] %s", text);
                md_append_line(&app->md, STORAGE_DIR, line);
            } else if (purpose == OSK_MD_ADD_LINE && text[0] && app->md.active) {
                md_append_line(&app->md, STORAGE_DIR, text);
            } else if (purpose == OSK_NEW_FOLDER) {
                char fname[64] = {0};
                if (storage_create_folder(STORAGE_DIR, app->cur_folder, text, fname, sizeof(fname))) {
                    char next_dir[256];
                    if (app->cur_folder[0]) snprintf(next_dir, sizeof(next_dir), "%s/%s", app->cur_folder, fname);
                    else snprintf(next_dir, sizeof(next_dir), "%s", fname);
                    snprintf(app->cur_folder, sizeof(app->cur_folder), "%s", next_dir);
                    app->sidebar_scroll = 0;
                    app_refresh_browser(app);
                }
            } else if (purpose == OSK_RENAME_FOLDER && text[0]) {
                char new_rel[256] = {0};
                if (storage_rename_folder(STORAGE_DIR, app->cur_folder, text, new_rel, sizeof(new_rel))) {
                    if (strcmp(app->doc.folder, app->cur_folder) == 0)
                        snprintf(app->doc.folder, sizeof(app->doc.folder), "%s", new_rel);
                    snprintf(app->cur_folder, sizeof(app->cur_folder), "%s", new_rel);
                    app_refresh_browser(app);
                }
            } else if (purpose == OSK_NEW_TEXTBOX && text[0]) {
                Page *pg = &app->doc.pages[app->doc.cur_page];
                float y0 = (float)(TOOLBAR_H + 96 + (int)(pg->text_count % 6) * 80);
                if (page_add_text(pg, 220.0f, y0, 560.0f, 2, text)) {
                    app->selected_img = -1;
                    app->selected_text = (int)pg->text_count - 1;
                    app->obj_edit_mode = true;
                    app->doc.dirty = true;
                    app_save_current(app);
                }
            } else if (purpose == OSK_EDIT_TEXTBOX) {
                Page *pg = &app->doc.pages[app->doc.cur_page];
                if (app->selected_text >= 0 && (size_t)app->selected_text < pg->text_count) {
                    if (text[0]) {
                        PageText *pt = &pg->texts[app->selected_text];
                        snprintf(pt->text, sizeof(pt->text), "%s", text);
                        page_text_recalc_height(pt);
                    } else {
                        page_remove_text(pg, (size_t)app->selected_text);
                        app->selected_text = -1;
                    }
                    app->doc.dirty = true;
                    app_save_current(app);
                }
            }
        }
        if (act == SET_ACT_CLOSE) prefs_save(app);
        if (act != SET_ACT_NONE) app_redraw_all(app);
        return true;
    }

    if (app->radial.open) return handle_radial_tap(app, x, y);
    if (y < TOOLBAR_H) return handle_toolbar_tap(app, x, y);
    if (app->sidebar_open && x < SIDEBAR_W) return handle_sidebar_tap(app, x, y);
    return false;
}
