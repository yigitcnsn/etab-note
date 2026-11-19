#ifndef NOTEOS_INPUT_H
#define NOTEOS_INPUT_H

#include "app.h"

int  input_open(App *app);
void input_close(App *app);
bool input_poll_once(App *app, int timeout_ms);

#endif
