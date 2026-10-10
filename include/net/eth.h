#ifndef NIITAN_NET_ETH_H
#define NIITAN_NET_ETH_H

#include "net/netif.h"
#include <stdint.h>

#define ETH_HDR_LEN 14
#define ETH_FRAME_MAX 1518

void eth_input(NETIF *ifp, const void *frame, uint32_t len);
int eth_output(NETIF *ifp, const uint8_t *dst, uint16_t ethertype, const void *payload,
               uint32_t len);

struct ETH_PROTO {
    uint16_t ethertype;
    void (*input)(NETIF *ifp, const uint8_t *payload, uint32_t len);
    const char *name;
};

extern const struct ETH_PROTO __net_ethproto_start[];
extern const struct ETH_PROTO __net_ethproto_end[];

#define ETH_PROTO_REGISTER(type_val, fn, name_str)                               \
    static const struct ETH_PROTO __ethproto_##fn                                \
        __attribute__((used, section(".net_ethproto"))) = {                          \
            .ethertype = (uint16_t)(type_val), .input = (fn), .name = (name_str) }

#endif
