#ifndef NIITAN_NET_E1000_H
#define NIITAN_NET_E1000_H

#include "net/netif.h"
#include <stdint.h>

#define E1000_VENDOR 0x8086
#define E1000_DEVICE 0x100E

int e1000_init(NETIF *ifp);
int e1000_tx(NETIF *ifp, const void *frame, uint32_t len);
int e1000_rx(NETIF *ifp, void *buf, uint32_t maxlen);

#endif
