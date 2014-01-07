#ifndef _SCREEN_NET_H
#define _SCREEN_NET_H

#include "screen.h"

#define SCREEN_NET_IF_COUNT (3)
#define SCREEN_NET_IF_BACKLOG (256)

extern const char *ifs[SCREEN_NET_IF_COUNT];

struct net_dev_t {
    const char *name;
    uint64_t tx_bytes_prev;
    uint64_t tx_kbytes_per_second[SCREEN_NET_IF_BACKLOG];
    uint64_t rx_bytes_prev;
    uint64_t rx_kbytes_per_second[SCREEN_NET_IF_BACKLOG];
};

struct screen_net_t {
    struct timespec last_update;
    struct net_dev_t devs[SCREEN_NET_IF_COUNT];
};

void screen_net_free(struct screen_t *screen);
void screen_net_hide(struct screen_t *screen);
void screen_net_init(struct screen_t *screen);
void screen_net_repaint(struct screen_t *screen);
void screen_net_show(struct screen_t *screen);

#endif
