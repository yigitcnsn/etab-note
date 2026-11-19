#define _GNU_SOURCE
#include "app.h"
#include "fb.h"
#include "input.h"
#include "prefs.h"
#include "storage.h"

#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

static volatile sig_atomic_t g_sig_quit = 0;

static void on_signal(int sig) {
    (void)sig;
    g_sig_quit = 1;
}

int main(void) {
    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);

    App app;
    memset(&app, 0, sizeof(app));
    app.tool = TOOL_PEN;
    app.size_idx = 1;
    app.saved_brightness = 190;
    app.radial.hover_wedge = -1;
    app.radial.hover_color = -1;
    app.fd_pen = app.fd_touch = app.fd_key = -1;

    if (storage_init(STORAGE_DIR) != 0) perror("storage_init");
    if (fb_open(&app.fb) != 0) return 1;
    if (input_open(&app) != 0) {
        fb_close(&app.fb);
        return 1;
    }

    prefs_load(&app);
    app_open_or_create_first_note(&app);
    app_redraw_all(&app);
    fb_present(&app.fb, false, 0, 0, false, app_canvas_clip_x0(&app));
    if (app.fb.double_buf) {
        fb_present(&app.fb, false, 0, 0, false, app_canvas_clip_x0(&app));
    }
    app.last_status_tick = time(NULL);

    while (!g_sig_quit && !app.quit) {
        if (!input_poll_once(&app, 1000)) break;
    }

    app_finish_active_stroke(&app);
    app_save_current(&app);
    prefs_save(&app);
    doc_free(&app.doc);
    input_close(&app);
    fb_close(&app.fb);
    return 0;
}
