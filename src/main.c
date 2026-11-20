#define _GNU_SOURCE
#include "app.h"
#include "drop.h"
#include "fb.h"
#include "input.h"
#include "mdns.h"
#include "prefs.h"
#include "storage.h"

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static volatile sig_atomic_t g_sig_quit = 0;

static void on_signal(int sig) {
    (void)sig;
    g_sig_quit = 1;
}

int main(void) {
    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);
    signal(SIGPIPE, SIG_IGN);

    App app;
    memset(&app, 0, sizeof(app));
    app.tool = TOOL_PEN;
    app.size_idx = 1;
    app.saved_brightness = 190;
    app.selected_img = -1;
    app.selected_text = -1;
    app.radial.hover_wedge = -1;
    app.radial.hover_color = -1;
    app.fd_pen = app.fd_touch = app.fd_key = -1;
    app.mdns.fd = -1;
    app.drop.listen_fd = -1;

    if (storage_init(STORAGE_DIR) != 0) perror("storage_init");
    if (storage_init(IMAGES_DIR) != 0) perror("storage_init images");
    if (fb_open(&app.fb) != 0) return 1;
    if (input_open(&app) != 0) {
        fb_close(&app.fb);
        return 1;
    }

    /* Enable IEEE 802.11 Wi-Fi power-save on boot */
    if (access("/system/bin/wpa_cli", X_OK) == 0) {
        system("/system/bin/wpa_cli -p /data/misc/wifi/sockets -i wlan0 set ps 1 >/dev/null 2>&1");
    }

    mdns_open(&app.mdns);
    drop_open(&app.drop);

    prefs_load(&app);
    app_open_or_create_first_note(&app);
    app_redraw_all(&app);
    fb_present(&app.fb, false, 0, 0, false, app_canvas_clip_x0(&app));
    if (app.fb.double_buf) {
        fb_present(&app.fb, false, 0, 0, false, app_canvas_clip_x0(&app));
    }
    app.last_status_tick = time(NULL);
    app.last_activity_time = app.last_status_tick;

    while (!g_sig_quit && !app.quit) {
        if (!input_poll_once(&app, app.sleeping ? -1 : 1000)) break;
    }

    app_finish_active_stroke(&app);
    app_save_current(&app);
    prefs_save(&app);
    md_free(&app.md);
    doc_free(&app.doc);
    drop_close(&app.drop);
    mdns_close(&app.mdns);
    input_close(&app);
    fb_close(&app.fb);
    return 0;
}
