#define _GNU_SOURCE
#include "noteos.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <math.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

#define STORAGE_DIR "/data/noteos/notes"
#define PREFS_PATH  "/data/noteos/prefs.conf"

static volatile sig_atomic_t g_quit = 0;

static void on_sig(int sig) {
    (void)sig;
    g_quit = 1;
}

typedef struct {
    FB fb;
    NoteDoc doc;

    /* Folder browser state */
    char cur_folder[64]; /* "" = root */
    FolderMeta folders[MAX_FOLDERS];
    int folder_count;
    NoteMeta notes[MAX_NOTES];
    int note_count;

    bool sidebar_open;
    bool move_mode;
    bool confirm_delete;
    bool confirm_clear;
    bool sleeping;
    int saved_brightness;

    ToolKind tool;
    uint8_t color_idx;
    uint8_t size_idx;

    /* Preferences persisted across reboots */
    PaperKind pref_paper;
    ThemeKind pref_theme;
    char pref_folder[64];
    char pref_note[64];

    /* Radial menu on stylus button */
    RadialMenu radial;
    bool radial_opened_by_hold;

    /* OS Settings + On-Screen Keyboard */
    SettingsState settings;

    /* Live stroke state */
    bool drawing;
    Stroke active_stroke;
    float smooth_p;

    /* Wacom stylus state */
    int max_x, max_y, max_p;
    int raw_x, raw_y, raw_p;
    bool tool_pen, tool_rubber, btn_stylus, btn_stylus_prev, pen_touch;
    bool pen_was_down;

    /* Capacitive touch (UI only — never draws on canvas) */
    int touch_x, touch_y;
    bool touch_down, touch_was_down;

    time_t last_status_tick;
} App;

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
    if (fgets(buf, sizeof(buf), f)) {
        fclose(f);
        return strncmp(buf, "Charging", 8) == 0;
    }
    fclose(f);
    return false;
}

static int canvas_clip_x0(const App *app) {
    return app->sidebar_open ? SIDEBAR_W : 0;
}

static void pretty_folder_name(const char *name, char *out, size_t sz) {
    size_t i = 0;
    for (; name[i] && i + 1 < sz; i++) {
        out[i] = (name[i] == '_') ? ' ' : (char)toupper((unsigned char)name[i]);
    }
    out[i] = '\0';
}

static void draw_button(FB *fb, const ThemePalette *tp, int x, int y, int w, int h,
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
    int scale = 2;
    int tw = fb_text_width(label, scale);
    int tx = x + (w - tw) / 2;
    int ty = y + (h - 7 * scale) / 2;
    fb_draw_text(fb, tx, ty, label, fg, scale);
}

static void draw_toolbar(App *app) {
    FB *fb = &app->fb;
    const ThemePalette *tp = &THEMES[app->doc.theme];
    const char *paper_names[PAPER_COUNT] = { "LINED", "GRID", "BLANK" };

    fb_fill_rect(fb, 0, 0, fb->w, TOOLBAR_H, tp->bar_bg);
    fb_fill_rect(fb, 0, TOOLBAR_H - 2, fb->w, 2, tp->bar_border);

    int by = 9, bh = 40;
    draw_button(fb, tp, 12, by, 114, bh, "NOTES", app->sidebar_open, false);

    fb_fill_rect(fb, 136, 12, 2, 34, tp->bar_border);

    /* Primary drawing tool button shows PEN / BRUSH / PENCIL and cycles on repeat tap */
    const char *pen_lbl = "PEN";
    bool is_pen_family = (app->tool == TOOL_PEN || app->tool == TOOL_BRUSH || app->tool == TOOL_PENCIL);
    if (app->tool == TOOL_BRUSH) pen_lbl = "BRUSH";
    else if (app->tool == TOOL_PENCIL) pen_lbl = "PENCIL";
    draw_button(fb, tp, 146, by, 102, bh, pen_lbl, is_pen_family, false);
    draw_button(fb, tp, 254, by, 58, bh, "HI", app->tool == TOOL_HIGHLIGHTER, false);
    draw_button(fb, tp, 318, by, 92, bh, "ERASE", app->tool == TOOL_ERASER, false);

    fb_fill_rect(fb, 420, 12, 2, 34, tp->bar_border);

    /* Sizes */
    draw_button(fb, tp, 430, by, 44, bh, "S", app->size_idx == 0, false);
    draw_button(fb, tp, 480, by, 44, bh, "M", app->size_idx == 1, false);
    draw_button(fb, tp, 530, by, 44, bh, "L", app->size_idx == 2, false);

    fb_fill_rect(fb, 584, 12, 2, 34, tp->bar_border);

    /* 5 color swatches */
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

    /* Actions */
    Page *pg = &app->doc.pages[app->doc.cur_page];
    draw_button(fb, tp, 832, by, 86, bh, "UNDO", false, false);
    if (pg->count == 0) fb_stroke_rect(fb, 832, by, 86, bh, 2, tp->bar_bg);
    draw_button(fb, tp, 924, by, 86, bh, "REDO", false, false);
    if (pg->redo_count == 0) fb_stroke_rect(fb, 924, by, 86, bh, 2, tp->bar_bg);

    draw_button(fb, tp, 1018, by, 100, bh, paper_names[app->doc.paper], false, false);
    draw_button(fb, tp, 1124, by, 100, bh, tp->name, false, false);
    draw_button(fb, tp, 1230, by, 92, bh, app->confirm_clear ? "SURE?" : "CLEAR", false, app->confirm_clear);

    fb_fill_rect(fb, 1334, 12, 2, 34, tp->bar_border);

    /* Page navigation */
    draw_button(fb, tp, 1346, by, 44, bh, "<", false, false);
    char pstr[32];
    snprintf(pstr, sizeof(pstr), "%d/%d", app->doc.cur_page + 1, app->doc.page_count);
    int ptw = fb_text_width(pstr, 2);
    fb_draw_text(fb, 1394 + (84 - ptw) / 2, 22, pstr, tp->bar_text, 2);
    draw_button(fb, tp, 1482, by, 44, bh, ">", false, false);
    draw_button(fb, tp, 1532, by, 58, bh, "+P", false, false);
    draw_button(fb, tp, 1598, by, 78, bh, "SET", app->settings.open, false);

    /* Battery + clock */
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

static void draw_sidebar(App *app) {
    if (!app->sidebar_open) return;
    FB *fb = &app->fb;
    const ThemePalette *tp = &THEMES[app->doc.theme];

    fb_fill_rect(fb, 0, TOOLBAR_H, SIDEBAR_W, fb->h - TOOLBAR_H, tp->sidebar_bg);
    fb_fill_rect(fb, SIDEBAR_W - 2, TOOLBAR_H, 2, fb->h - TOOLBAR_H, tp->bar_border);

    int y = TOOLBAR_H + 14;
    if (app->move_mode) {
        fb_draw_text(fb, 18, y + 12, "MOVE NOTE TO:", tp->accent, 2);
        draw_button(fb, tp, SIDEBAR_W - 116, y, 102, 42, "CANCEL", false, false);
        y += 58;

        /* Destination 0: Root */
        bool is_root = (app->doc.folder[0] == '\0');
        fb_fill_rect(fb, 10, y, SIDEBAR_W - 22, 50, is_root ? tp->sidebar_sel : tp->bar_bg);
        fb_stroke_rect(fb, 10, y, SIDEBAR_W - 22, 50, 1, is_root ? tp->accent : tp->bar_border);
        fb_draw_text(fb, 24, y + 16, "ALL NOTES (ROOT)", tp->bar_text, 2);
        y += 58;

        int max_y = fb->h - 24;
        for (int i = 0; i < app->folder_count && y + 54 <= max_y; i++) {
            bool cur = (strcmp(app->folders[i].name, app->doc.folder) == 0);
            fb_fill_rect(fb, 10, y, SIDEBAR_W - 22, 50, cur ? tp->sidebar_sel : tp->bar_bg);
            fb_stroke_rect(fb, 10, y, SIDEBAR_W - 22, 50, 1, cur ? tp->accent : tp->bar_border);
            fb_fill_rect(fb, 20, y + 16, 22, 16, tp->accent);
            fb_fill_rect(fb, 20, y + 12, 10, 5, tp->accent);
            fb_draw_text(fb, 52, y + 16, app->folders[i].display, tp->bar_text, 2);
            y += 58;
        }
        return;
    }

    if (app->cur_folder[0] == '\0') {
        /* Root view: [+ NOTE] and [+ DIR] side by side */
        draw_button(fb, tp, 14, y, 140, 44, "+ NOTE", true, false);
        draw_button(fb, tp, 162, y, 142, 44, "+ FOLDER", false, false);
        y += 58;
    } else {
        /* Inside folder: [< BACK] + folder title + [+ NOTE] */
        draw_button(fb, tp, 14, y, 96, 42, "< BACK", false, false);
        char fdisp[64];
        pretty_folder_name(app->cur_folder, fdisp, sizeof(fdisp));
        fb_draw_text(fb, 122, y + 14, fdisp, tp->accent, 2);
        y += 52;
        draw_button(fb, tp, 14, y, SIDEBAR_W - 30, 44, "+ NEW NOTE HERE", true, false);
        y += 56;
    }

    int max_y = app->cur_folder[0] != '\0' ? (fb->h - 186) : (fb->h - 134);

    /* Folders (only at root) */
    if (app->cur_folder[0] == '\0') {
        for (int i = 0; i < app->folder_count && y + 54 <= max_y; i++) {
            fb_fill_rect(fb, 10, y, SIDEBAR_W - 22, 50, tp->bar_bg);
            fb_stroke_rect(fb, 10, y, SIDEBAR_W - 22, 50, 1, tp->bar_border);
            /* Small folder accent icon */
            fb_fill_rect(fb, 20, y + 14, 24, 18, tp->accent);
            fb_fill_rect(fb, 20, y + 10, 12, 5, tp->accent);
            fb_draw_text(fb, 54, y + 10, app->folders[i].display, tp->bar_text, 2);
            char meta[32];
            snprintf(meta, sizeof(meta), "%d NOTE%s",
                     app->folders[i].note_count, app->folders[i].note_count == 1 ? "" : "S");
            fb_draw_text(fb, 54, y + 32, meta, tp->bar_muted, 1);
            y += 56;
        }
    }

    /* Notes in current folder */
    for (int i = 0; i < app->note_count && y + 54 <= max_y; i++) {
        bool sel = (strcmp(app->notes[i].id, app->doc.id) == 0 &&
                    strcmp(app->cur_folder, app->doc.folder) == 0);
        if (sel) {
            fb_fill_rect(fb, 10, y, SIDEBAR_W - 22, 50, tp->sidebar_sel);
            fb_fill_rect(fb, 10, y, 5, 50, tp->accent);
        }
        fb_draw_text(fb, 24, y + 10, app->notes[i].title, tp->bar_text, 2);
        char meta[32];
        snprintf(meta, sizeof(meta), "%d PAGE%s",
                 app->notes[i].page_count, app->notes[i].page_count == 1 ? "" : "S");
        fb_draw_text(fb, 24, y + 32, meta, tp->bar_muted, 1);
        y += 56;
    }

    int fy = fb->h - 66;
    if (app->cur_folder[0] != '\0') {
        draw_button(fb, tp, 14, fy - 106, SIDEBAR_W - 30, 44, "RENAME FOLDER", false, false);
        draw_button(fb, tp, 14, fy - 54, 140, 44, "REN NOTE", false, false);
        draw_button(fb, tp, 162, fy - 54, 142, 44, "MOVE", false, false);
    } else {
        draw_button(fb, tp, 14, fy - 54, 140, 44, "RENAME", false, false);
        draw_button(fb, tp, 162, fy - 54, 142, 44, "MOVE", false, false);
    }
    const char *del_lbl = app->confirm_delete ? "SURE?" :
        ((app->cur_folder[0] != '\0' && app->note_count == 0) ? "DEL DIR" : "DELETE");
    draw_button(fb, tp, 14, fy, 140, 48, del_lbl, false, app->confirm_delete);
    draw_button(fb, tp, 162, fy, 142, 48, "EXIT", false, false);
}

static void redraw_all(App *app) {
    if (app->settings.open) {
        settings_render(&app->fb, (ThemeKind)app->doc.theme, &app->settings);
        return;
    }
    render_page(&app->fb, &app->doc, canvas_clip_x0(app));
    draw_sidebar(app);
    draw_toolbar(app);
    if (app->radial.open) {
        render_radial_menu(&app->fb, (ThemeKind)app->doc.theme, &app->radial,
                           app->tool, app->size_idx, app->color_idx);
    }
    if (app->settings.osk.open) {
        settings_render(&app->fb, (ThemeKind)app->doc.theme, &app->settings);
    }
}

static void refresh_browser(App *app) {
    app->folder_count = storage_list_folders(STORAGE_DIR, app->folders, MAX_FOLDERS);
    app->note_count = storage_list(STORAGE_DIR, app->cur_folder, app->notes, MAX_NOTES);
}

static void save_current(App *app) {
    if (app->doc.dirty) {
        if (storage_save(STORAGE_DIR, &app->doc)) {
            app->doc.dirty = false;
            refresh_browser(app);
        }
    }
}

static void prefs_save(const App *app) {
    char tmp[128];
    snprintf(tmp, sizeof(tmp), "%s.tmp", PREFS_PATH);
    FILE *f = fopen(tmp, "w");
    if (!f) return;
    fprintf(f, "theme=%d\n", (int)app->pref_theme);
    fprintf(f, "paper=%d\n", (int)app->pref_paper);
    fprintf(f, "tool=%d\n", (int)app->tool);
    fprintf(f, "color=%d\n", (int)app->color_idx);
    fprintf(f, "size=%d\n", (int)app->size_idx);
    fprintf(f, "brightness=%d\n", fb_get_backlight());
    fprintf(f, "folder=%s\n", app->doc.folder);
    fprintf(f, "note=%s\n", app->doc.id);
    fflush(f);
    fsync(fileno(f));
    fclose(f);
    rename(tmp, PREFS_PATH);
}

static void prefs_load(App *app) {
    app->pref_paper = PAPER_LINED;
    app->pref_theme = THEME_PARCHMENT;
    app->pref_folder[0] = '\0';
    app->pref_note[0] = '\0';

    FILE *f = fopen(PREFS_PATH, "r");
    if (!f) return;
    char line[128];
    while (fgets(line, sizeof(line), f)) {
        size_t n = strlen(line);
        while (n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r')) line[--n] = '\0';
        int v = 0;
        if (sscanf(line, "theme=%d", &v) == 1 && v >= 0 && v < THEME_COUNT) app->pref_theme = (ThemeKind)v;
        else if (sscanf(line, "paper=%d", &v) == 1 && v >= 0 && v < PAPER_COUNT) app->pref_paper = (PaperKind)v;
        else if (sscanf(line, "tool=%d", &v) == 1 && v >= 0 && v <= TOOL_PENCIL) app->tool = (ToolKind)v;
        else if (sscanf(line, "color=%d", &v) == 1 && v >= 0 && v < 5) app->color_idx = (uint8_t)v;
        else if (sscanf(line, "size=%d", &v) == 1 && v >= 0 && v < 3) app->size_idx = (uint8_t)v;
        else if (sscanf(line, "brightness=%d", &v) == 1 && v >= 15 && v <= 255) {
            app->saved_brightness = v;
            fb_set_backlight(v);
        } else if (strncmp(line, "folder=", 7) == 0) {
            snprintf(app->pref_folder, sizeof(app->pref_folder), "%s", line + 7);
        } else if (strncmp(line, "note=", 5) == 0) {
            snprintf(app->pref_note, sizeof(app->pref_note), "%s", line + 5);
        }
    }
    fclose(f);
}

static void open_or_create_first_note(App *app) {
    if (app->pref_note[0] &&
        storage_load(STORAGE_DIR, app->pref_folder, app->pref_note, &app->doc)) {
        snprintf(app->cur_folder, sizeof(app->cur_folder), "%s", app->pref_folder);
        app->doc.theme = (uint8_t)app->pref_theme;
        app->doc.paper = (uint8_t)app->pref_paper;
        refresh_browser(app);
        return;
    }
    refresh_browser(app);
    if (app->note_count > 0 &&
        storage_load(STORAGE_DIR, app->cur_folder, app->notes[0].id, &app->doc)) {
        app->doc.theme = (uint8_t)app->pref_theme;
        app->doc.paper = (uint8_t)app->pref_paper;
        return;
    }
    char id[64], title[64];
    storage_next_id(STORAGE_DIR, app->cur_folder, id, sizeof(id), title, sizeof(title));
    doc_init_new(&app->doc, app->cur_folder, id, title, app->pref_paper, app->pref_theme);
    storage_save(STORAGE_DIR, &app->doc);
    app->doc.dirty = false;
    refresh_browser(app);
}

static void finish_active_stroke(App *app) {
    if (!app->drawing) return;
    app->drawing = false;
    if (app->active_stroke.count > 0) {
        Page *pg = &app->doc.pages[app->doc.cur_page];
        page_push_stroke(pg, app->active_stroke);
        memset(&app->active_stroke, 0, sizeof(app->active_stroke));
        app->doc.dirty = true;
        save_current(app);
    } else {
        stroke_free(&app->active_stroke);
    }
}

static void close_radial(App *app) {
    if (!app->radial.open) return;
    app->radial.open = false;
    app->radial.hover_wedge = -1;
    app->radial.hover_color = -1;
    redraw_all(app);
}

static void open_radial_at(App *app, int x, int y) {
    finish_active_stroke(app);
    int clip = canvas_clip_x0(app);
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
    redraw_all(app);
}

static void execute_radial_wedge(App *app, int w) {
    Page *pg = &app->doc.pages[app->doc.cur_page];
    switch (w) {
        case 0: app->tool = TOOL_PEN; break;
        case 1: app->tool = TOOL_BRUSH; break;
        case 2: app->tool = TOOL_PENCIL; break;
        case 3: app->tool = TOOL_HIGHLIGHTER; break;
        case 4: app->tool = TOOL_ERASER; break;
        case 5: app->size_idx = (uint8_t)((app->size_idx + 1) % 3); break;
        case 6:
            if (page_undo(pg)) {
                app->doc.dirty = true;
                save_current(app);
            }
            break;
        case 7:
            if (page_redo(pg)) {
                app->doc.dirty = true;
                save_current(app);
            }
            break;
        default: break;
    }
}

static bool handle_radial_tap(App *app, int x, int y) {
    if (!app->radial.open) return false;
    int w = radial_hit_wedge(&app->radial, x, y);
    if (w >= 0) {
        execute_radial_wedge(app, w);
        close_radial(app);
        return true;
    }
    int c = radial_hit_color(&app->radial, x, y);
    if (c >= 0) {
        app->color_idx = (uint8_t)c;
        if (app->tool == TOOL_ERASER) app->tool = TOOL_PEN;
        close_radial(app);
        return true;
    }
    /* Tap center or outside -> dismiss */
    close_radial(app);
    return true;
}

static bool hit(int px, int py, int x, int y, int w, int h) {
    return px >= x && px < x + w && py >= y && py < y + h;
}

static void toggle_sleep(App *app);

static bool handle_ui_tap(App *app, int x, int y) {
    if (app->settings.open || app->settings.osk.open) {
        char text[64] = {0};
        OskPurpose purpose = OSK_NONE;
        int act = settings_handle_tap(&app->fb, &app->settings, x, y, &purpose, text, sizeof(text));
        if (act == SET_ACT_SLEEP) {
            toggle_sleep(app);
            return true;
        }
        if (act == SET_ACT_OSK_DONE) {
            if (purpose == OSK_RENAME_NOTE && text[0]) {
                snprintf(app->doc.title, sizeof(app->doc.title), "%s", text);
                app->doc.dirty = true;
                save_current(app);
            } else if (purpose == OSK_NEW_NOTE) {
                save_current(app);
                char id[64], def_title[64];
                PaperKind pk = (PaperKind)app->doc.paper;
                ThemeKind tk = (ThemeKind)app->doc.theme;
                storage_next_id(STORAGE_DIR, app->cur_folder, id, sizeof(id), def_title, sizeof(def_title));
                const char *title = text[0] ? text : def_title;
                doc_free(&app->doc);
                doc_init_new(&app->doc, app->cur_folder, id, title, pk, tk);
                storage_save(STORAGE_DIR, &app->doc);
                app->doc.dirty = false;
                prefs_save(app);
                refresh_browser(app);
                app->sidebar_open = false;
            } else if (purpose == OSK_NEW_FOLDER) {
                char fname[64] = {0};
                if (storage_create_folder(STORAGE_DIR, text, fname, sizeof(fname))) {
                    snprintf(app->cur_folder, sizeof(app->cur_folder), "%s", fname);
                    refresh_browser(app);
                }
            } else if (purpose == OSK_RENAME_FOLDER && text[0]) {
                char new_name[64] = {0};
                if (storage_rename_folder(STORAGE_DIR, app->cur_folder, text, new_name, sizeof(new_name))) {
                    if (strcmp(app->doc.folder, app->cur_folder) == 0) {
                        snprintf(app->doc.folder, sizeof(app->doc.folder), "%s", new_name);
                    }
                    snprintf(app->cur_folder, sizeof(app->cur_folder), "%s", new_name);
                    refresh_browser(app);
                }
            }
        }
        if (act == SET_ACT_OSK_FAST) {
            settings_render_osk_fast(&app->fb, (ThemeKind)app->doc.theme, &app->settings);
            return true;
        }
        if (act == SET_ACT_CLOSE) {
            prefs_save(app);
        }
        if (act != SET_ACT_NONE) {
            redraw_all(app);
        }
        return true;
    }

    if (app->radial.open) {
        return handle_radial_tap(app, x, y);
    }

    /* Top toolbar */
    if (y < TOOLBAR_H) {
        int by = 9, bh = 40;
        if (!hit(x, y, 1230, by, 92, bh)) app->confirm_clear = false;
        app->confirm_delete = false;

        if (hit(x, y, 12, by, 114, bh)) {
            app->sidebar_open = !app->sidebar_open;
            app->move_mode = false;
            if (app->sidebar_open) {
                save_current(app);
                refresh_browser(app);
            }
            redraw_all(app);
            return true;
        }
        if (hit(x, y, 146, by, 102, bh)) {
            if (app->tool == TOOL_PEN) app->tool = TOOL_BRUSH;
            else if (app->tool == TOOL_BRUSH) app->tool = TOOL_PENCIL;
            else app->tool = TOOL_PEN;
            draw_toolbar(app);
            return true;
        }
        if (hit(x, y, 254, by, 58, bh)) { app->tool = TOOL_HIGHLIGHTER; draw_toolbar(app); return true; }
        if (hit(x, y, 318, by, 92, bh)) { app->tool = TOOL_ERASER; draw_toolbar(app); return true; }

        if (hit(x, y, 430, by, 44, bh)) { app->size_idx = 0; draw_toolbar(app); return true; }
        if (hit(x, y, 480, by, 44, bh)) { app->size_idx = 1; draw_toolbar(app); return true; }
        if (hit(x, y, 530, by, 44, bh)) { app->size_idx = 2; draw_toolbar(app); return true; }

        for (int i = 0; i < 5; i++) {
            int cx = 614 + i * 44;
            int cy = TOOLBAR_H / 2;
            int dx = x - cx, dy = y - cy;
            if (dx * dx + dy * dy <= 22 * 22) {
                app->color_idx = (uint8_t)i;
                if (app->tool == TOOL_ERASER) app->tool = TOOL_PEN;
                draw_toolbar(app);
                return true;
            }
        }

        Page *pg = &app->doc.pages[app->doc.cur_page];
        if (hit(x, y, 832, by, 86, bh)) {
            if (page_undo(pg)) {
                app->doc.dirty = true;
                save_current(app);
                redraw_all(app);
            }
            return true;
        }
        if (hit(x, y, 924, by, 86, bh)) {
            if (page_redo(pg)) {
                app->doc.dirty = true;
                save_current(app);
                redraw_all(app);
            }
            return true;
        }
        if (hit(x, y, 1018, by, 100, bh)) {
            app->doc.paper = (uint8_t)((app->doc.paper + 1) % PAPER_COUNT);
            app->pref_paper = (PaperKind)app->doc.paper;
            app->doc.dirty = true;
            save_current(app);
            prefs_save(app);
            redraw_all(app);
            return true;
        }
        if (hit(x, y, 1124, by, 100, bh)) {
            app->doc.theme = (uint8_t)((app->doc.theme + 1) % THEME_COUNT);
            app->pref_theme = (ThemeKind)app->doc.theme;
            app->doc.dirty = true;
            save_current(app);
            prefs_save(app);
            redraw_all(app);
            return true;
        }
        if (hit(x, y, 1230, by, 92, bh)) {
            if (!app->confirm_clear) {
                app->confirm_clear = true;
                draw_toolbar(app);
            } else {
                app->confirm_clear = false;
                page_clear(pg);
                app->doc.dirty = true;
                save_current(app);
                redraw_all(app);
            }
            return true;
        }
        if (hit(x, y, 1346, by, 44, bh)) {
            if (app->doc.cur_page > 0) {
                app->doc.cur_page--;
                app->doc.dirty = true;
                save_current(app);
                redraw_all(app);
            }
            return true;
        }
        if (hit(x, y, 1482, by, 44, bh)) {
            if (app->doc.cur_page + 1 < app->doc.page_count) {
                app->doc.cur_page++;
                app->doc.dirty = true;
                save_current(app);
                redraw_all(app);
            }
            return true;
        }
        if (hit(x, y, 1532, by, 58, bh)) {
            if (app->doc.page_count < MAX_PAGES) {
                page_init(&app->doc.pages[app->doc.page_count]);
                app->doc.cur_page = app->doc.page_count;
                app->doc.page_count++;
                app->doc.dirty = true;
                save_current(app);
                redraw_all(app);
            }
            return true;
        }
        if (hit(x, y, 1598, by, 78, bh)) {
            finish_active_stroke(app);
            save_current(app);
            app->radial.open = false;
            app->settings.open = true;
            app->settings.confirm_action = 0;
            app->settings.status_msg[0] = '\0';
            settings_refresh(&app->settings);
            redraw_all(app);
            return true;
        }
        return true;
    }

    /* Sidebar */
    if (app->sidebar_open && x < SIDEBAR_W) {
        int fy = app->fb.h - 66;
        if (app->move_mode) {
            int my = TOOLBAR_H + 14;
            if (hit(x, y, SIDEBAR_W - 116, my, 102, 42)) {
                app->move_mode = false;
                draw_sidebar(app);
                return true;
            }
            my += 58;
            /* Destination 0: Root ("") */
            if (hit(x, y, 10, my, SIDEBAR_W - 22, 50)) {
                save_current(app);
                char new_id[64] = {0};
                if (storage_move_note(STORAGE_DIR, app->doc.folder, app->doc.id, "", new_id, sizeof(new_id))) {
                    app->doc.folder[0] = '\0';
                    snprintf(app->doc.id, sizeof(app->doc.id), "%s", new_id);
                    app->cur_folder[0] = '\0';
                    prefs_save(app);
                }
                app->move_mode = false;
                refresh_browser(app);
                draw_sidebar(app);
                return true;
            }
            my += 58;
            int max_my = app->fb.h - 24;
            for (int i = 0; i < app->folder_count && my + 54 <= max_my; i++) {
                if (hit(x, y, 10, my, SIDEBAR_W - 22, 50)) {
                    save_current(app);
                    char new_id[64] = {0};
                    if (storage_move_note(STORAGE_DIR, app->doc.folder, app->doc.id,
                                          app->folders[i].name, new_id, sizeof(new_id))) {
                        snprintf(app->doc.folder, sizeof(app->doc.folder), "%s", app->folders[i].name);
                        snprintf(app->doc.id, sizeof(app->doc.id), "%s", new_id);
                        snprintf(app->cur_folder, sizeof(app->cur_folder), "%s", app->folders[i].name);
                        prefs_save(app);
                    }
                    app->move_mode = false;
                    refresh_browser(app);
                    draw_sidebar(app);
                    return true;
                }
                my += 58;
            }
            return true;
        }

        if (!hit(x, y, 14, fy, 140, 48)) app->confirm_delete = false;
        app->confirm_clear = false;

        int ry = TOOLBAR_H + 14;
        if (app->cur_folder[0] == '\0') {
            /* [+ NOTE] -> open keyboard for name */
            if (hit(x, y, 14, ry, 140, 44)) {
                char id[64], def_title[64];
                storage_next_id(STORAGE_DIR, "", id, sizeof(id), def_title, sizeof(def_title));
                settings_open_osk(&app->settings, OSK_NEW_NOTE, "NEW NOTE NAME:", def_title, NULL);
                redraw_all(app);
                return true;
            }
            /* [+ FOLDER] -> open keyboard for folder name */
            if (hit(x, y, 162, ry, 142, 44)) {
                settings_open_osk(&app->settings, OSK_NEW_FOLDER, "NEW FOLDER NAME:", "", NULL);
                redraw_all(app);
                return true;
            }
            ry += 58;
        } else {
            /* [< BACK] */
            if (hit(x, y, 14, ry, 96, 42)) {
                app->cur_folder[0] = '\0';
                app->confirm_delete = false;
                refresh_browser(app);
                draw_sidebar(app);
                return true;
            }
            ry += 52;
            /* [+ NEW NOTE HERE] -> open keyboard for name */
            if (hit(x, y, 14, ry, SIDEBAR_W - 30, 44)) {
                char id[64], def_title[64];
                storage_next_id(STORAGE_DIR, app->cur_folder, id, sizeof(id), def_title, sizeof(def_title));
                settings_open_osk(&app->settings, OSK_NEW_NOTE, "NEW NOTE NAME:", def_title, NULL);
                redraw_all(app);
                return true;
            }
            ry += 56;
        }

        /* RENAME & MOVE buttons above footer */
        if (app->cur_folder[0] != '\0') {
            if (hit(x, y, 14, fy - 106, SIDEBAR_W - 30, 44)) {
                char fdisp[64];
                pretty_folder_name(app->cur_folder, fdisp, sizeof(fdisp));
                settings_open_osk(&app->settings, OSK_RENAME_FOLDER, "RENAME FOLDER:", fdisp, NULL);
                redraw_all(app);
                return true;
            }
        }
        if (hit(x, y, 14, fy - 54, 140, 44)) {
            settings_open_osk(&app->settings, OSK_RENAME_NOTE, "RENAME CURRENT NOTE:", app->doc.title, NULL);
            redraw_all(app);
            return true;
        }
        if (hit(x, y, 162, fy - 54, 142, 44)) {
            refresh_browser(app);
            app->move_mode = true;
            draw_sidebar(app);
            return true;
        }

        /* Footer: DELETE / DEL DIR and EXIT */
        if (hit(x, y, 14, fy, 140, 48)) {
            if (!app->confirm_delete) {
                app->confirm_delete = true;
                draw_sidebar(app);
            } else {
                app->confirm_delete = false;
                if (app->cur_folder[0] != '\0' && app->note_count == 0) {
                    storage_delete_folder(STORAGE_DIR, app->cur_folder);
                    app->cur_folder[0] = '\0';
                    refresh_browser(app);
                    draw_sidebar(app);
                } else {
                    storage_delete(STORAGE_DIR, app->doc.folder, app->doc.id);
                    doc_free(&app->doc);
                    open_or_create_first_note(app);
                    redraw_all(app);
                }
            }
            return true;
        }
        if (hit(x, y, 162, fy, 142, 48)) {
            save_current(app);
            g_quit = 1;
            return true;
        }

        int max_y = app->cur_folder[0] != '\0' ? (app->fb.h - 186) : (app->fb.h - 134);

        /* Folder rows (root only) */
        if (app->cur_folder[0] == '\0') {
            for (int i = 0; i < app->folder_count && ry + 54 <= max_y; i++) {
                if (hit(x, y, 10, ry, SIDEBAR_W - 22, 50)) {
                    snprintf(app->cur_folder, sizeof(app->cur_folder), "%s", app->folders[i].name);
                    app->confirm_delete = false;
                    refresh_browser(app);
                    draw_sidebar(app);
                    return true;
                }
                ry += 56;
            }
        }

        /* Note rows */
        for (int i = 0; i < app->note_count && ry + 54 <= max_y; i++) {
            if (hit(x, y, 10, ry, SIDEBAR_W - 22, 50)) {
                if (strcmp(app->notes[i].id, app->doc.id) != 0 ||
                    strcmp(app->cur_folder, app->doc.folder) != 0) {
                    save_current(app);
                    storage_load(STORAGE_DIR, app->cur_folder, app->notes[i].id, &app->doc);
                    app->doc.theme = (uint8_t)app->pref_theme;
                    app->doc.paper = (uint8_t)app->pref_paper;
                    prefs_save(app);
                }
                app->sidebar_open = false;
                redraw_all(app);
                return true;
            }
            ry += 56;
        }
        return true;
    }

    return false;
}

static void toggle_sleep(App *app) {
    if (!app->sleeping) {
        finish_active_stroke(app);
        app->radial.open = false;
        save_current(app);
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
        redraw_all(app);
        fb_present(&app->fb, false, 0, 0, false, canvas_clip_x0(app));
        if (app->fb.double_buf) {
            fb_present(&app->fb, false, 0, 0, false, canvas_clip_x0(app));
        }
    }
}

int main(void) {
    signal(SIGINT, on_sig);
    signal(SIGTERM, on_sig);

    App app;
    memset(&app, 0, sizeof(app));
    app.tool = TOOL_PEN;
    app.color_idx = 0;
    app.size_idx = 1;
    app.saved_brightness = 190;
    app.radial.hover_wedge = -1;
    app.radial.hover_color = -1;

    if (storage_init(STORAGE_DIR) != 0) {
        perror("storage_init");
    }
    if (fb_open(&app.fb) != 0) {
        return 1;
    }

    int fd_pen   = open("/dev/input/event1", O_RDONLY | O_NONBLOCK);
    int fd_touch = open("/dev/input/event4", O_RDONLY | O_NONBLOCK);
    int fd_key   = open("/dev/input/event0", O_RDONLY | O_NONBLOCK);
    if (fd_pen < 0) {
        perror("open /dev/input/event1");
        fb_close(&app.fb);
        return 1;
    }

    struct input_absinfo ax = {0}, ay = {0}, ap = {0};
    ioctl(fd_pen, EVIOCGABS(ABS_X), &ax);
    ioctl(fd_pen, EVIOCGABS(ABS_Y), &ay);
    ioctl(fd_pen, EVIOCGABS(ABS_PRESSURE), &ap);
    app.max_x = ax.maximum > 0 ? ax.maximum : 21696;
    app.max_y = ay.maximum > 0 ? ay.maximum : 13560;
    app.max_p = ap.maximum > 0 ? ap.maximum : 1023;

    prefs_load(&app);
    open_or_create_first_note(&app);
    redraw_all(&app);
    fb_present(&app.fb, false, 0, 0, false, canvas_clip_x0(&app));
    if (app.fb.double_buf) {
        fb_present(&app.fb, false, 0, 0, false, canvas_clip_x0(&app));
    }
    app.last_status_tick = time(NULL);

    struct pollfd pfds[3];
    pfds[0].fd = fd_pen;   pfds[0].events = POLLIN;
    pfds[1].fd = fd_touch; pfds[1].events = POLLIN;
    pfds[2].fd = fd_key;   pfds[2].events = POLLIN;

    while (!g_quit) {
        int pr = poll(pfds, 3, 1000);
        if (pr < 0) {
            if (errno == EINTR) continue;
            break;
        }

        bool need_present = false;

        /* 1. Hardware Keys */
        if (fd_key >= 0 && (pfds[2].revents & POLLIN)) {
            struct input_event ev;
            while (read(fd_key, &ev, sizeof(ev)) == (ssize_t)sizeof(ev)) {
                if (ev.type == EV_KEY && ev.value == 1) {
                    if (ev.code == KEY_POWER) {
                        toggle_sleep(&app);
                    } else if (!app.sleeping && ev.code == KEY_VOLUMEUP) {
                        fb_set_backlight(fb_get_backlight() + 30);
                    } else if (!app.sleeping && ev.code == KEY_VOLUMEDOWN) {
                        int b = fb_get_backlight() - 30;
                        if (b < 15) b = 15;
                        fb_set_backlight(b);
                    }
                }
            }
        }

        if (app.sleeping) {
            struct input_event ev;
            if (pfds[0].revents & POLLIN) while (read(fd_pen, &ev, sizeof(ev)) > 0) {}
            if (fd_touch >= 0 && (pfds[1].revents & POLLIN)) while (read(fd_touch, &ev, sizeof(ev)) > 0) {}
            continue;
        }

        /* 2. Capacitive Touch — UI & Radial Menu only (never draws on canvas) */
        if (fd_touch >= 0 && (pfds[1].revents & POLLIN)) {
            struct input_event ev;
            while (read(fd_touch, &ev, sizeof(ev)) == (ssize_t)sizeof(ev)) {
                if (ev.type == EV_ABS) {
                    if (ev.code == ABS_MT_POSITION_X || ev.code == ABS_X) app.touch_x = ev.value;
                    else if (ev.code == ABS_MT_POSITION_Y || ev.code == ABS_Y) app.touch_y = ev.value;
                    else if (ev.code == ABS_MT_TRACKING_ID && ev.value < 0) app.touch_down = false;
                } else if (ev.type == EV_KEY && ev.code == BTN_TOUCH) {
                    app.touch_down = (ev.value != 0);
                } else if (ev.type == EV_SYN && ev.code == SYN_REPORT) {
                    if (app.touch_down && !app.touch_was_down && !app.tool_pen && !app.tool_rubber) {
                        if (handle_ui_tap(&app, app.touch_x, app.touch_y)) {
                            need_present = true;
                        }
                    } else if (!app.touch_down && app.touch_was_down && app.settings.osk.open) {
                        settings_osk_release(&app.fb, (ThemeKind)app.doc.theme, &app.settings);
                        need_present = true;
                    }
                    app.touch_was_down = app.touch_down;
                }
            }
        }

        /* 3. Wacom EMR Stylus */
        if (pfds[0].revents & POLLIN) {
            struct input_event ev;
            while (read(fd_pen, &ev, sizeof(ev)) == (ssize_t)sizeof(ev)) {
                if (ev.type == EV_ABS) {
                    if (ev.code == ABS_X) app.raw_x = ev.value;
                    else if (ev.code == ABS_Y) app.raw_y = ev.value;
                    else if (ev.code == ABS_PRESSURE) app.raw_p = ev.value;
                } else if (ev.type == EV_KEY) {
                    if (ev.code == BTN_TOOL_PEN) {
                        app.tool_pen = (ev.value != 0);
                        if (!app.tool_pen) finish_active_stroke(&app);
                    } else if (ev.code == BTN_TOOL_RUBBER) {
                        app.tool_rubber = (ev.value != 0);
                        if (!app.tool_rubber) finish_active_stroke(&app);
                    } else if (ev.code == BTN_STYLUS || ev.code == BTN_STYLUS2) {
                        app.btn_stylus = (ev.value != 0);
                    } else if (ev.code == BTN_TOUCH) {
                        app.pen_touch = (ev.value != 0);
                        if (!app.pen_touch) finish_active_stroke(&app);
                    }
                } else if (ev.type == EV_SYN && ev.code == SYN_REPORT) {
                    float sx = (float)app.raw_x * (float)(app.fb.w - 1) / (float)app.max_x;
                    float sy = (float)app.raw_y * (float)(app.fb.h - 1) / (float)app.max_y;
                    float pf = (float)app.raw_p / (float)app.max_p;
                    bool down = app.pen_touch || app.raw_p > 0;
                    int clip_x0 = canvas_clip_x0(&app);

                    if (app.settings.open || app.settings.osk.open) {
                        if (down && !app.pen_was_down) {
                            handle_ui_tap(&app, (int)sx, (int)sy);
                            need_present = true;
                        } else if (!down && app.pen_was_down && app.settings.osk.open) {
                            settings_osk_release(&app.fb, (ThemeKind)app.doc.theme, &app.settings);
                            need_present = true;
                        }
                        app.pen_was_down = down;
                        app.btn_stylus_prev = app.btn_stylus;
                        continue;
                    }

                    /* Stylus side button edge detection -> Radial Circular Menu */
                    if (app.btn_stylus && !app.btn_stylus_prev) {
                        if (app.radial.open) {
                            close_radial(&app);
                        } else {
                            open_radial_at(&app, (int)sx, (int)sy);
                        }
                        app.btn_stylus_prev = true;
                        app.pen_was_down = down;
                        need_present = true;
                        continue;
                    }
                    if (!app.btn_stylus && app.btn_stylus_prev) {
                        app.btn_stylus_prev = false;
                        if (app.radial.open && app.radial_opened_by_hold) {
                            /* If user dragged to a wedge/color while holding button, select on release! */
                            int hw = radial_hit_wedge(&app.radial, (int)sx, (int)sy);
                            int hc = radial_hit_color(&app.radial, (int)sx, (int)sy);
                            if (hw >= 0) {
                                execute_radial_wedge(&app, hw);
                                close_radial(&app);
                                need_present = true;
                                continue;
                            } else if (hc >= 0) {
                                app.color_idx = (uint8_t)hc;
                                if (app.tool == TOOL_ERASER) app.tool = TOOL_PEN;
                                close_radial(&app);
                                need_present = true;
                                continue;
                            }
                            /* Released in center -> stay open for tap selection */
                            app.radial_opened_by_hold = false;
                        }
                    }

                    /* While radial menu is open: highlight hovered wedge/color & handle taps */
                    if (app.radial.open) {
                        int hw = radial_hit_wedge(&app.radial, (int)sx, (int)sy);
                        int hc = radial_hit_color(&app.radial, (int)sx, (int)sy);
                        if (hw != app.radial.hover_wedge || hc != app.radial.hover_color) {
                            app.radial.hover_wedge = hw;
                            app.radial.hover_color = hc;
                            render_radial_menu(&app.fb, (ThemeKind)app.doc.theme, &app.radial,
                                               app.tool, app.size_idx, app.color_idx);
                        }
                        if (down && !app.pen_was_down) {
                            handle_radial_tap(&app, (int)sx, (int)sy);
                        }
                        app.pen_was_down = down;
                        need_present = true;
                        continue;
                    }

                    bool erase_mode = app.tool_rubber || (app.tool == TOOL_ERASER);

                    if (down && !app.pen_was_down) {
                        if (sy < TOOLBAR_H || (app.sidebar_open && sx < SIDEBAR_W)) {
                            handle_ui_tap(&app, (int)sx, (int)sy);
                            app.pen_was_down = true;
                            need_present = true;
                            continue;
                        }
                        if (app.confirm_clear || app.confirm_delete) {
                            app.confirm_clear = false;
                            app.confirm_delete = false;
                            draw_toolbar(&app);
                            draw_sidebar(&app);
                        }
                        if (!erase_mode) {
                            memset(&app.active_stroke, 0, sizeof(app.active_stroke));
                            app.active_stroke.tool = (uint8_t)app.tool;
                            app.active_stroke.color_idx = app.color_idx;
                            app.active_stroke.size_idx = app.size_idx;
                            app.smooth_p = pf;
                            app.drawing = true;
                        }
                    }

                    if (down && sy >= TOOLBAR_H && sx >= (float)clip_x0) {
                        if (erase_mode) {
                            finish_active_stroke(&app);
                            Page *pg = &app.doc.pages[app.doc.cur_page];
                            if (page_erase_at(pg, sx, sy, 20.0f)) {
                                app.doc.dirty = true;
                                render_page(&app.fb, &app.doc, clip_x0);
                                draw_toolbar(&app);
                            }
                        } else if (app.drawing) {
                            app.smooth_p = app.smooth_p * 0.55f + pf * 0.45f;
                            StrokePoint np = { sx, sy, app.smooth_p };
                            uint32_t cnt = app.active_stroke.count;
                            if (cnt == 0) {
                                stroke_push_pt(&app.active_stroke, np.x, np.y, np.p);
                                render_stroke_segment(&app.fb, (ThemeKind)app.doc.theme,
                                                      app.active_stroke.tool,
                                                      app.active_stroke.color_idx,
                                                      app.active_stroke.size_idx,
                                                      np, np, clip_x0);
                            } else {
                                StrokePoint prev = app.active_stroke.pts[cnt - 1];
                                float dx = np.x - prev.x, dy = np.y - prev.y;
                                if (dx * dx + dy * dy >= 0.6f) {
                                    stroke_push_pt(&app.active_stroke, np.x, np.y, np.p);
                                    render_stroke_segment(&app.fb, (ThemeKind)app.doc.theme,
                                                          app.active_stroke.tool,
                                                          app.active_stroke.color_idx,
                                                          app.active_stroke.size_idx,
                                                          prev, np, clip_x0);
                                }
                            }
                        }
                    } else if (!down) {
                        if (app.drawing) finish_active_stroke(&app);
                        if (app.doc.dirty && !app.drawing) save_current(&app);
                    }

                    app.pen_was_down = down;
                    need_present = true;
                }
            }
        }

        /* Periodic battery/clock refresh */
        time_t now = time(NULL);
        if (now - app.last_status_tick >= 20) {
            app.last_status_tick = now;
            if (app.settings.open) {
                settings_refresh(&app.settings);
                redraw_all(&app);
            } else {
                draw_toolbar(&app);
                if (app.radial.open) {
                    render_radial_menu(&app.fb, (ThemeKind)app.doc.theme, &app.radial,
                                       app.tool, app.size_idx, app.color_idx);
                }
            }
            need_present = true;
        }

        if (need_present) {
            int hx = (int)((float)app.raw_x * (float)(app.fb.w - 1) / (float)app.max_x);
            int hy = (int)((float)app.raw_y * (float)(app.fb.h - 1) / (float)app.max_y);
            bool hover = !app.settings.open && !app.settings.osk.open &&
                         (app.tool_pen || app.tool_rubber) && !(app.pen_touch || app.raw_p > 0);
            bool erase_cursor = app.tool_rubber || (app.tool == TOOL_ERASER);
            fb_present(&app.fb, hover, hx, hy, erase_cursor, canvas_clip_x0(&app));
        }
    }

    finish_active_stroke(&app);
    save_current(&app);
    prefs_save(&app);
    doc_free(&app.doc);
    fb_close(&app.fb);
    close(fd_pen);
    if (fd_touch >= 0) close(fd_touch);
    if (fd_key >= 0) close(fd_key);
    return 0;
}
