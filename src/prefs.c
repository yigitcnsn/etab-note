#define _GNU_SOURCE
#include "prefs.h"
#include "fb.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>

void prefs_save(const App *app) {
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
    if (app->md.active) {
        fprintf(f, "folder=%s\n", app->md.folder);
        fprintf(f, "note=%s\n", app->md.filename);
    } else {
        fprintf(f, "folder=%s\n", app->doc.folder);
        fprintf(f, "note=%s\n", app->doc.id);
    }

    fflush(f);
    fsync(fileno(f));
    fclose(f);
    rename(tmp, PREFS_PATH);
}

void prefs_load(App *app) {
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
        if (sscanf(line, "theme=%d", &v) == 1 && v >= 0 && v < THEME_COUNT) {
            app->pref_theme = (ThemeKind)v;
        } else if (sscanf(line, "paper=%d", &v) == 1 && v >= 0 && v < PAPER_COUNT) {
            app->pref_paper = (PaperKind)v;
        } else if (sscanf(line, "tool=%d", &v) == 1 && v >= 0 && v <= TOOL_PENCIL) {
            app->tool = (ToolKind)v;
        } else if (sscanf(line, "color=%d", &v) == 1 && v >= 0 && v < 5) {
            app->color_idx = (uint8_t)v;
        } else if (sscanf(line, "size=%d", &v) == 1 && v >= 0 && v < 3) {
            app->size_idx = (uint8_t)v;
        } else if (sscanf(line, "brightness=%d", &v) == 1 && v >= 15 && v <= 255) {
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
