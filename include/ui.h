#ifndef NOTEOS_UI_H
#define NOTEOS_UI_H

#include "app.h"

void ui_draw_toolbar(App *app);
void ui_draw_sidebar(App *app);

void ui_radial_open_at(App *app, int x, int y);
void ui_radial_close(App *app);
void ui_radial_execute_wedge(App *app, int wedge);

bool ui_handle_tap(App *app, int x, int y);

#endif
