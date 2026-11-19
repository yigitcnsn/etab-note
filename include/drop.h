#ifndef NOTEOS_DROP_H
#define NOTEOS_DROP_H

#include "types.h"
#include <stdbool.h>

#define DROP_PORT 8080

typedef enum {
    DROP_NONE  = 0,
    DROP_IMAGE = 1,
    DROP_TEXT  = 2
} DropKind;

typedef struct {
    DropKind kind;
    char image_filename[64];
    char text[256];
} DropEvent;

typedef struct {
    int listen_fd;
} DropServer;

int  drop_open(DropServer *ds);
void drop_close(DropServer *ds);
bool drop_accept_and_handle(DropServer *ds, DropEvent *out_ev);

#endif
