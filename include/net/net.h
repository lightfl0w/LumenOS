#ifndef NIITAN_NET_H
#define NIITAN_NET_H
#include "kernel/nt_ping_reply.h"
#include "kernel/sync/sync.h"
#include "net/netif.h"
#include <stdint.h>
extern NETIF g_netif;
extern struct SCHED_LOCK net_lock;
extern int net_enable;
void net_init(void);
uint32_t net_now_ms(void);
int nt_icmp_send(uint32_t dst, uint16_t id, uint16_t seq);
int nt_icmp_recv(struct NET_PING_REPLY *out, int max);
struct TCP_PCB;
#define NET_TRACE_ENABLE 0
#if NET_TRACE_ENABLE
void nt_raw(char *ev, uint32_t a, uint32_t b);
void nt_log(char *ev, uint16_t sp, uint16_t dp, uint32_t seq, uint32_t ack, uint32_t len,
            uint32_t flags);
void nt_pcb(struct TCP_PCB *pcb, char *ev, uint32_t seq, uint32_t ack, uint32_t wnd, uint32_t len,
            uint32_t flags);
#else
#define nt_raw(...) ((void)0)
#define nt_log(...) ((void)0)
#define nt_pcb(...) ((void)0)
#endif
#endif
