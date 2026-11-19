#ifndef NOTEOS_MDNS_H
#define NOTEOS_MDNS_H

#include <stdint.h>

typedef struct {
    int fd;
    uint32_t last_ip;
} MdnsState;

int  mdns_open(MdnsState *ms);
void mdns_close(MdnsState *ms);
void mdns_handle_readable(MdnsState *ms);
void mdns_announce(MdnsState *ms);

#endif
