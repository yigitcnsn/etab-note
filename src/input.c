#define _GNU_SOURCE
#include "input.h"
#include "drop.h"
#include "fb.h"
#include "mdns.h"
#include "osk.h"
#include "prefs.h"
#include "render.h"
#include "settings.h"
#include "storage.h"
#include "ui.h"

#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

int input_open(App *app) {
    app->fd_pen   = open("/dev/input/event1", O_RDONLY | O_NONBLOCK);
    app->fd_touch = open("/dev/input/event4", O_RDONLY | O_NONBLOCK);
    app->fd_key   = open("/dev/input/event0", O_RDONLY | O_NONBLOCK);
    if (app->fd_pen < 0) {
        perror("open /dev/input/event1");
        return -1;
    }

    struct input_absinfo ax = {0}, ay = {0}, ap = {0};
    ioctl(app->fd_pen, EVIOCGABS(ABS_X), &ax);
    ioctl(app->fd_pen, EVIOCGABS(ABS_Y), &ay);
    ioctl(app->fd_pen, EVIOCGABS(ABS_PRESSURE), &ap);
    app->max_x = ax.maximum > 0 ? ax.maximum : 21696;
    app->max_y = ay.maximum > 0 ? ay.maximum : 13560;
    app->max_p = ap.maximum > 0 ? ap.maximum : 1023;
    return 0;
}

void input_close(App *app) {
    if (app->fd_pen >= 0) close(app->fd_pen);
    if (app->fd_touch >= 0) close(app->fd_touch);
    if (app->fd_key >= 0) close(app->fd_key);
    app->fd_pen = app->fd_touch = app->fd_key = -1;
}

static bool hit_f(float px, float py, float x, float y, float w, float h) {
    return px >= x && px <= x + w && py >= y && py <= y + h;
}

/* Handles move / resize / delete of images when selected or in PIC edit mode */
static bool handle_img_down(App *app, float x, float y) {
    Page *pg = &app->doc.pages[app->doc.cur_page];
    if (app->selected_img >= 0 && (size_t)app->selected_img < pg->img_count) {
        PageImage *im = &pg->images[app->selected_img];
        /* Top-right red [X] delete badge */
        if (hit_f(x, y, im->x + im->w - 44.0f, im->y - 4.0f, 48.0f, 48.0f)) {
            page_remove_image(pg, (size_t)app->selected_img);
            app->selected_img = -1;
            app->img_drag = IMG_DRAG_NONE;
            app->doc.dirty = true;
            app_save_current(app);
            app_redraw_all(app);
            return true;
        }
        /* Bottom-right resize handle */
        if (hit_f(x, y, im->x + im->w - 48.0f, im->y + im->h - 48.0f, 56.0f, 56.0f)) {
            app->img_drag = IMG_DRAG_RESIZE;
            app->drag_aspect = (im->h > 1.0f) ? (im->w / im->h) : 1.0f;
            return true;
        }
    }

    /* Hit test images topmost first */
    for (int i = (int)pg->img_count - 1; i >= 0; i--) {
        PageImage *im = &pg->images[i];
        if (hit_f(x, y, im->x, im->y, im->w, im->h)) {
            app->selected_img = i;
            app->img_drag = IMG_DRAG_MOVE;
            app->drag_off_x = x - im->x;
            app->drag_off_y = y - im->y;
            app_redraw_all(app);
            return true;
        }
    }

    /* Tapped outside any image -> lock & deselect */
    if (app->selected_img >= 0 || app->img_edit_mode) {
        app->selected_img = -1;
        app->img_edit_mode = false;
        app->img_drag = IMG_DRAG_NONE;
        app_redraw_all(app);
        return true;
    }
    return false;
}

static bool handle_img_move(App *app, float x, float y) {
    if (app->img_drag == IMG_DRAG_NONE || app->selected_img < 0) return false;
    Page *pg = &app->doc.pages[app->doc.cur_page];
    if ((size_t)app->selected_img >= pg->img_count) return false;
    PageImage *im = &pg->images[app->selected_img];

    if (app->img_drag == IMG_DRAG_MOVE) {
        float nx = x - app->drag_off_x;
        float ny = y - app->drag_off_y;
        if (nx < 0.0f) nx = 0.0f;
        if (ny < (float)TOOLBAR_H) ny = (float)TOOLBAR_H;
        if (nx + im->w > (float)app->fb.w) nx = (float)app->fb.w - im->w;
        if (ny + im->h > (float)app->fb.h) ny = (float)app->fb.h - im->h;
        im->x = nx;
        im->y = ny;
    } else if (app->img_drag == IMG_DRAG_RESIZE) {
        float nw = x - im->x;
        if (nw < 100.0f) nw = 100.0f;
        if (im->x + nw > (float)app->fb.w) nw = (float)app->fb.w - im->x;
        float nh = nw / app->drag_aspect;
        if (nh < 80.0f) { nh = 80.0f; nw = nh * app->drag_aspect; }
        if (im->y + nh > (float)app->fb.h) {
            nh = (float)app->fb.h - im->y;
            nw = nh * app->drag_aspect;
        }
        im->w = nw;
        im->h = nh;
    }
    app->doc.dirty = true;
    app_redraw_all(app);
    return true;
}

static void handle_img_up(App *app) {
    if (app->img_drag != IMG_DRAG_NONE) {
        app->img_drag = IMG_DRAG_NONE;
        app_save_current(app);
    }
}

static void handle_drop_event(App *app, const DropEvent *ev) {
    Page *pg = &app->doc.pages[app->doc.cur_page];
    if (ev->kind == DROP_IMAGE) {
        float x = (float)(app->fb.w / 2 - 280);
        float y = (float)(TOOLBAR_H + 140 + (int)(pg->img_count % 4) * 32);
        if (page_add_image(pg, ev->image_filename, x, y, 0.0f, 0.0f)) {
            PageImage *im = &pg->images[pg->img_count - 1];
            im->x = ((float)app->fb.w - im->w) * 0.5f;
            if (im->x < 20.0f) im->x = 20.0f;
            app->selected_img = (int)pg->img_count - 1;
            app->img_edit_mode = true;
            app->doc.dirty = true;
            app_save_current(app);
            snprintf(app->toast_msg, sizeof(app->toast_msg), "IMAGE DROPPED! DRAG TO MOVE/RESIZE");
            app->toast_until = time(NULL) + 4;
        }
    } else if (ev->kind == DROP_TEXT) {
        float y = (float)(TOOLBAR_H + 64 + (int)(pg->text_count % 8) * 90);
        if (page_add_text(pg, 140.0f, y, 2, ev->text)) {
            app->doc.dirty = true;
            app_save_current(app);
            snprintf(app->toast_msg, sizeof(app->toast_msg), "TEXT DROPPED ONTO PAGE!");
            app->toast_until = time(NULL) + 3;
        }
    }
    if (!app->sleeping) app_redraw_all(app);
}

static void handle_keys(App *app) {
    struct input_event ev;
    while (read(app->fd_key, &ev, sizeof(ev)) == (ssize_t)sizeof(ev)) {
        if (ev.type == EV_KEY && ev.value == 1) {
            if (ev.code == KEY_POWER) {
                app_toggle_sleep(app);
            } else if (!app->sleeping && ev.code == KEY_VOLUMEUP) {
                fb_set_backlight(fb_get_backlight() + 30);
                prefs_save(app);
            } else if (!app->sleeping && ev.code == KEY_VOLUMEDOWN) {
                int b = fb_get_backlight() - 30;
                if (b < 15) b = 15;
                fb_set_backlight(b);
                prefs_save(app);
            }
        }
    }
}

static bool handle_touch(App *app) {
    bool need_present = false;
    struct input_event ev;
    while (read(app->fd_touch, &ev, sizeof(ev)) == (ssize_t)sizeof(ev)) {
        if (ev.type == EV_ABS) {
            if (ev.code == ABS_MT_POSITION_X || ev.code == ABS_X) app->touch_x = ev.value;
            else if (ev.code == ABS_MT_POSITION_Y || ev.code == ABS_Y) app->touch_y = ev.value;
            else if (ev.code == ABS_MT_TRACKING_ID && ev.value < 0) app->touch_down = false;
        } else if (ev.type == EV_KEY && ev.code == BTN_TOUCH) {
            app->touch_down = (ev.value != 0);
        } else if (ev.type == EV_SYN && ev.code == SYN_REPORT) {
            if (app->touch_down && !app->touch_was_down && !app->tool_pen && !app->tool_rubber) {
                if (ui_handle_tap(app, app->touch_x, app->touch_y)) {
                    need_present = true;
                } else if ((app->selected_img >= 0 || app->img_edit_mode) &&
                           app->touch_y >= TOOLBAR_H && app->touch_x >= app_canvas_clip_x0(app)) {
                    if (handle_img_down(app, (float)app->touch_x, (float)app->touch_y)) need_present = true;
                }
            } else if (app->touch_down && app->img_drag != IMG_DRAG_NONE) {
                if (handle_img_move(app, (float)app->touch_x, (float)app->touch_y)) need_present = true;
            } else if (!app->touch_down && app->touch_was_down) {
                if (app->img_drag != IMG_DRAG_NONE) handle_img_up(app);
                if (app->settings.osk.open) {
                    osk_release(&app->fb, (ThemeKind)app->doc.theme, &app->settings.osk);
                    need_present = true;
                }
            }
            app->touch_was_down = app->touch_down;
        }
    }
    return need_present;
}

static bool handle_stylus(App *app) {
    bool need_present = false;
    struct input_event ev;
    while (read(app->fd_pen, &ev, sizeof(ev)) == (ssize_t)sizeof(ev)) {
        if (ev.type == EV_ABS) {
            if (ev.code == ABS_X) app->raw_x = ev.value;
            else if (ev.code == ABS_Y) app->raw_y = ev.value;
            else if (ev.code == ABS_PRESSURE) app->raw_p = ev.value;
        } else if (ev.type == EV_KEY) {
            if (ev.code == BTN_TOOL_PEN) {
                app->tool_pen = (ev.value != 0);
                if (!app->tool_pen) { app_finish_active_stroke(app); handle_img_up(app); }
            } else if (ev.code == BTN_TOOL_RUBBER) {
                app->tool_rubber = (ev.value != 0);
                if (!app->tool_rubber) app_finish_active_stroke(app);
            } else if (ev.code == BTN_STYLUS || ev.code == BTN_STYLUS2) {
                app->btn_stylus = (ev.value != 0);
            } else if (ev.code == BTN_TOUCH) {
                app->pen_touch = (ev.value != 0);
                if (!app->pen_touch) { app_finish_active_stroke(app); handle_img_up(app); }
            }
        } else if (ev.type == EV_SYN && ev.code == SYN_REPORT) {
            float sx = (float)app->raw_x * (float)(app->fb.w - 1) / (float)app->max_x;
            float sy = (float)app->raw_y * (float)(app->fb.h - 1) / (float)app->max_y;
            float pf = (float)app->raw_p / (float)app->max_p;
            bool down = app->pen_touch || app->raw_p > 0;
            int clip_x0 = app_canvas_clip_x0(app);

            if (app->settings.open || app->settings.osk.open) {
                if (down && !app->pen_was_down) {
                    ui_handle_tap(app, (int)sx, (int)sy);
                    need_present = true;
                } else if (!down && app->pen_was_down && app->settings.osk.open) {
                    osk_release(&app->fb, (ThemeKind)app->doc.theme, &app->settings.osk);
                    need_present = true;
                }
                app->pen_was_down = down;
                app->btn_stylus_prev = app->btn_stylus;
                continue;
            }

            /* Stylus side button -> Radial Circular Menu */
            if (app->btn_stylus && !app->btn_stylus_prev) {
                if (app->radial.open) ui_radial_close(app);
                else ui_radial_open_at(app, (int)sx, (int)sy);
                app->btn_stylus_prev = true;
                app->pen_was_down = down;
                need_present = true;
                continue;
            }
            if (!app->btn_stylus && app->btn_stylus_prev) {
                app->btn_stylus_prev = false;
                if (app->radial.open && app->radial_opened_by_hold) {
                    int hw = radial_hit_wedge(&app->radial, (int)sx, (int)sy);
                    int hc = radial_hit_color(&app->radial, (int)sx, (int)sy);
                    if (hw >= 0) {
                        ui_radial_execute_wedge(app, hw);
                        ui_radial_close(app);
                        need_present = true;
                        continue;
                    } else if (hc >= 0) {
                        app->color_idx = (uint8_t)hc;
                        if (app->tool == TOOL_ERASER) app->tool = TOOL_PEN;
                        prefs_save(app);
                        ui_radial_close(app);
                        need_present = true;
                        continue;
                    }
                    app->radial_opened_by_hold = false;
                }
            }

            if (app->radial.open) {
                int hw = radial_hit_wedge(&app->radial, (int)sx, (int)sy);
                int hc = radial_hit_color(&app->radial, (int)sx, (int)sy);
                if (hw != app->radial.hover_wedge || hc != app->radial.hover_color) {
                    app->radial.hover_wedge = hw;
                    app->radial.hover_color = hc;
                    render_radial_menu(&app->fb, (ThemeKind)app->doc.theme, &app->radial,
                                       app->tool, app->size_idx, app->color_idx);
                }
                if (down && !app->pen_was_down) ui_handle_tap(app, (int)sx, (int)sy);
                app->pen_was_down = down;
                need_present = true;
                continue;
            }

            /* Image move/resize/delete while in PIC edit mode or image selected */
            if (app->selected_img >= 0 || app->img_edit_mode) {
                if (down && !app->pen_was_down) {
                    if (sy < TOOLBAR_H || (app->sidebar_open && sx < SIDEBAR_W)) {
                        ui_handle_tap(app, (int)sx, (int)sy);
                    } else {
                        handle_img_down(app, sx, sy);
                    }
                    app->pen_was_down = true;
                    need_present = true;
                    continue;
                } else if (down && app->img_drag != IMG_DRAG_NONE) {
                    handle_img_move(app, sx, sy);
                    app->pen_was_down = true;
                    need_present = true;
                    continue;
                } else if (!down && app->img_drag != IMG_DRAG_NONE) {
                    handle_img_up(app);
                }
            }

            bool erase_mode = app->tool_rubber || (app->tool == TOOL_ERASER);
            if (down && !app->pen_was_down) {
                if (sy < TOOLBAR_H || (app->sidebar_open && sx < SIDEBAR_W)) {
                    ui_handle_tap(app, (int)sx, (int)sy);
                    app->pen_was_down = true;
                    need_present = true;
                    continue;
                }
                if (app->confirm_clear || app->confirm_delete) {
                    app->confirm_clear = false;
                    app->confirm_delete = false;
                    ui_draw_toolbar(app);
                    ui_draw_sidebar(app);
                }
                if (!erase_mode) {
                    memset(&app->active_stroke, 0, sizeof(app->active_stroke));
                    app->active_stroke.tool = (uint8_t)app->tool;
                    app->active_stroke.color_idx = app->color_idx;
                    app->active_stroke.size_idx = app->size_idx;
                    app->smooth_p = pf;
                    app->drawing = true;
                }
            }

            if (down && sy >= TOOLBAR_H && sx >= (float)clip_x0) {
                if (erase_mode) {
                    app_finish_active_stroke(app);
                    Page *pg = &app->doc.pages[app->doc.cur_page];
                    if (page_erase_at(pg, sx, sy, 20.0f)) {
                        app->doc.dirty = true;
                        render_page(&app->fb, &app->doc, clip_x0, app->selected_img);
                        ui_draw_toolbar(app);
                    }
                } else if (app->drawing) {
                    app->smooth_p = app->smooth_p * 0.55f + pf * 0.45f;
                    StrokePoint np = { sx, sy, app->smooth_p };
                    uint32_t cnt = app->active_stroke.count;
                    if (cnt == 0) {
                        stroke_push_pt(&app->active_stroke, np.x, np.y, np.p);
                        render_stroke_segment(&app->fb, (ThemeKind)app->doc.theme,
                                              app->active_stroke.tool, app->active_stroke.color_idx,
                                              app->active_stroke.size_idx, np, np, clip_x0);
                    } else {
                        StrokePoint prev = app->active_stroke.pts[cnt - 1];
                        float dx = np.x - prev.x, dy = np.y - prev.y;
                        if (dx * dx + dy * dy >= 0.6f) {
                            stroke_push_pt(&app->active_stroke, np.x, np.y, np.p);
                            render_stroke_segment(&app->fb, (ThemeKind)app->doc.theme,
                                                  app->active_stroke.tool, app->active_stroke.color_idx,
                                                  app->active_stroke.size_idx, prev, np, clip_x0);
                        }
                    }
                }
            } else if (!down) {
                if (app->drawing) app_finish_active_stroke(app);
                if (app->doc.dirty && !app->drawing) app_save_current(app);
            }

            app->pen_was_down = down;
            need_present = true;
        }
    }
    return need_present;
}

bool input_poll_once(App *app, int timeout_ms) {
    struct pollfd pfds[5] = {
        { .fd = app->fd_pen,         .events = POLLIN },
        { .fd = app->fd_touch,       .events = POLLIN },
        { .fd = app->fd_key,         .events = POLLIN },
        { .fd = app->mdns.fd,        .events = POLLIN },
        { .fd = app->drop.listen_fd, .events = POLLIN }
    };

    int pr = poll(pfds, 5, timeout_ms);
    if (pr < 0) return errno == EINTR;

    bool need_present = false;

    /* Network services stay active even when screen is sleeping */
    if (app->mdns.fd >= 0 && (pfds[3].revents & POLLIN)) {
        mdns_handle_readable(&app->mdns);
    }
    if (app->drop.listen_fd >= 0 && (pfds[4].revents & POLLIN)) {
        DropEvent dev;
        if (drop_accept_and_handle(&app->drop, &dev)) {
            handle_drop_event(app, &dev);
            if (!app->sleeping) need_present = true;
        }
    }

    if (app->fd_key >= 0 && (pfds[2].revents & POLLIN)) handle_keys(app);

    if (app->sleeping) {
        struct input_event ev;
        if (pfds[0].revents & POLLIN) while (read(app->fd_pen, &ev, sizeof(ev)) > 0) {}
        if (app->fd_touch >= 0 && (pfds[1].revents & POLLIN)) while (read(app->fd_touch, &ev, sizeof(ev)) > 0) {}
        return true;
    }

    if (app->fd_touch >= 0 && (pfds[1].revents & POLLIN)) {
        if (handle_touch(app)) need_present = true;
    }
    if (pfds[0].revents & POLLIN) {
        if (handle_stylus(app)) need_present = true;
    }

    time_t now = time(NULL);
    if (app->toast_msg[0] && now > app->toast_until) {
        app->toast_msg[0] = '\0';
        app_redraw_all(app);
        need_present = true;
    }

    if (now - app->last_status_tick >= 20) {
        app->last_status_tick = now;
        mdns_announce(&app->mdns);
        if (app->settings.open) {
            settings_refresh(&app->settings);
            app_redraw_all(app);
        } else {
            ui_draw_toolbar(app);
            if (app->radial.open) {
                render_radial_menu(&app->fb, (ThemeKind)app->doc.theme, &app->radial,
                                   app->tool, app->size_idx, app->color_idx);
            }
        }
        need_present = true;
    }

    if (need_present) {
        int hx = (int)((float)app->raw_x * (float)(app->fb.w - 1) / (float)app->max_x);
        int hy = (int)((float)app->raw_y * (float)(app->fb.h - 1) / (float)app->max_y);
        bool hover = !app->settings.open && !app->settings.osk.open &&
                     (app->tool_pen || app->tool_rubber) && !(app->pen_touch || app->raw_p > 0);
        bool erase_cursor = app->tool_rubber || (app->tool == TOOL_ERASER);
        fb_present(&app->fb, hover, hx, hy, erase_cursor, app_canvas_clip_x0(app));
    }
    return true;
}
