#include "net/net.h"
#include "net/pci.h"
#include "net/pci.h"
#include "net/eth.h"
#include "drivers/char/serial/console/io.h"
#include "lib/string/str.h"
#include "mm/pool.h"

#include <stdint.h>

#define VIRTIO_VENDOR 0x1AF4u
#define VIRTIO_DEV_NET_LEGACY 0x1000u

#define VR_STATUS_ACK 1u
#define VR_STATUS_DRIVER 2u
#define VR_STATUS_DRIVER_OK 4u

#define VR_GUEST_FEATURES 0x04u
#define VR_QUEUE_PFN 0x08u
#define VR_QUEUE_NUM 0x0Cu
#define VR_QUEUE_SEL 0x0Eu
#define VR_QUEUE_NOTIFY 0x10u
#define VR_STATUS 0x12u
#define VR_MAC 0x14u

#define VIRTIO_NET_F_MAC 5u

#define VR_DESC_F_NEXT 1u
#define VR_DESC_F_WRITE 2u

#define VRQ_RX 0u
#define VRQ_TX 1u

#define VRQ_SIZE 128u
#define VRQ_RX_PAIRS 32u
#define VRQ_TX_PAIRS 16u
#define VR_BUF_LEN 2048u
#define VR_HDR_LEN 10u

struct VR_DESC {
    uint64_t addr;
    uint32_t len;
    uint16_t flags;
    uint16_t next;
};

struct VR_USED_ELEM {
    uint32_t id;
    uint32_t len;
};

struct VR_QUEUE {
    volatile struct VR_DESC *desc;
    volatile uint16_t *avail_flags;
    volatile uint16_t *avail_idx;
    volatile uint16_t *avail_ring;
    volatile uint16_t *used_flags;
    volatile uint16_t *used_idx;
    volatile struct VR_USED_ELEM *used_ring;
    uint16_t size;
    uint32_t consumed;
};

static uint16_t s_io;
static struct VR_QUEUE s_rxq;
static struct VR_QUEUE s_txq;

static uint8_t *s_rx_data[VRQ_RX_PAIRS];
static uint8_t *s_tx_data[VRQ_TX_PAIRS];
static uint16_t s_tx_free[VRQ_TX_PAIRS];
static uint16_t s_tx_free_top;

static inline void vr_outw(uint16_t off, uint16_t v) {
    __asm__ volatile("outw %0, %1" : : "a"(v), "Nd"((uint16_t)(s_io + off)));
}

static inline void vr_outb(uint16_t off, uint8_t v) {
    __asm__ volatile("outb %0, %1" : : "a"(v), "Nd"((uint16_t)(s_io + off)));
}

static inline uint8_t vr_inb(uint16_t off) {
    uint8_t v;
    __asm__ volatile("inb %1, %0" : "=a"(v) : "Nd"((uint16_t)(s_io + off)));
    return v;
}

static inline void vr_outl(uint16_t off, uint32_t v) {
    __asm__ volatile("outl %0, %1" : : "a"(v), "Nd"((uint16_t)(s_io + off)));
}

static inline void vr_outl_at(uint16_t off, uint32_t v) {
    __asm__ volatile("outl %0, %1" : : "a"(v), "Nd"((uint16_t)(s_io + off)));
}

static inline uint16_t vr_inw(uint16_t off) {
    uint16_t v;
    __asm__ volatile("inw %1, %0" : "=a"(v) : "Nd"((uint16_t)(s_io + off)));
    return v;
}

static inline uint32_t vr_inl(uint16_t off) {
    uint32_t v;
    __asm__ volatile("inl %1, %0" : "=a"(v) : "Nd"((uint16_t)(s_io + off)));
    return v;
}

static inline void vr_mfence(void) {
    __asm__ volatile("mfence" ::: "memory");
}

static void vq_init(struct VR_QUEUE *q, uint16_t qsel) {
    vr_outw(VR_QUEUE_SEL, qsel);
    q->size = vr_inw(VR_QUEUE_NUM);
    uint8_t *mem = (uint8_t *)get_kernel_pages(3);
    memset(mem, 0, 3 * 4096u);
    uint32_t phys = PHY_OF((uint32_t)mem);
    q->desc = (volatile struct VR_DESC *)mem;
    q->avail_flags = (volatile uint16_t *)(mem + 16u * q->size);
    q->avail_idx = q->avail_flags + 1;
    q->avail_ring = q->avail_flags + 2;
    uint32_t used_off = (16u * q->size + 6u + 2u * q->size + 4095u) & ~4095u;
    q->used_flags = (volatile uint16_t *)(mem + used_off);
    q->used_idx = q->used_flags + 1;
    q->used_ring = (volatile struct VR_USED_ELEM *)(mem + used_off + 4u);
    q->consumed = 0;
    vr_outl_at(VR_QUEUE_PFN, phys >> 12);
}

static void vq_push(struct VR_QUEUE *q, uint16_t head) {
    q->avail_ring[*q->avail_idx % q->size] = head;
    vr_mfence();
    (*q->avail_idx)++;
    vr_mfence();
}

static void vq_notify(uint16_t qsel) {
    vr_outw(VR_QUEUE_NOTIFY, qsel);
}

static uint32_t vq_pfn_readback(uint16_t qsel) {
    vr_outw(VR_QUEUE_SEL, qsel);
    uint32_t v;
    __asm__ volatile("inl %1, %0" : "=a"(v) : "Nd"((uint16_t)(s_io + VR_QUEUE_PFN)));
    return v;
}

static void vr_desc_pair(volatile struct VR_DESC *desc, uint16_t i, uint8_t *buf) {
    desc[2 * i].addr = PHY_OF((uint32_t)buf);
    desc[2 * i].len = VR_HDR_LEN;
    desc[2 * i].flags = VR_DESC_F_WRITE | VR_DESC_F_NEXT;
    desc[2 * i].next = (uint16_t)(2 * i + 1);
    desc[2 * i + 1].addr = PHY_OF((uint32_t)buf) + VR_HDR_LEN;
    desc[2 * i + 1].len = VR_BUF_LEN - VR_HDR_LEN;
    desc[2 * i + 1].flags = VR_DESC_F_WRITE;
    desc[2 * i + 1].next = 0xFFFFu;
}

static int vr_setup_rx(void) {
    uint8_t *pool = (uint8_t *)get_kernel_pages((VRQ_RX_PAIRS * VR_BUF_LEN + 4095u) / 4096u);
    if (pool == NULL) {
        return -1;
    }
    for (uint16_t i = 0; i < VRQ_RX_PAIRS; i++) {
        s_rx_data[i] = pool + i * VR_BUF_LEN;
        vr_desc_pair(s_rxq.desc, i, s_rx_data[i]);
        vq_push(&s_rxq, (uint16_t)(2 * i));
        vq_notify(VRQ_RX);
    }
    return 0;
}

int virtio_net_tx(NETIF *ifp, const void *frame, uint32_t len) {
    if (len > VR_BUF_LEN - VR_HDR_LEN) {
        len = VR_BUF_LEN - VR_HDR_LEN;
    }
    while (s_tx_free_top == 0) {
        uint16_t uidx = *s_txq.used_idx;
        while (s_txq.consumed != uidx) {
            uint32_t pair = s_txq.used_ring[s_txq.consumed % s_txq.size].id / 2u;
            s_tx_free[s_tx_free_top++] = (uint16_t)pair;
            s_txq.consumed++;
        }
    }
    uint16_t pair = s_tx_free[--s_tx_free_top];
    kprintf("[DBG] tx pair=%u len=%u free=%u\n", pair, len, s_tx_free_top);
    memcpy(s_tx_data[pair] + VR_HDR_LEN, frame, len);
    s_txq.desc[2 * pair].addr = PHY_OF((uint32_t)s_tx_data[pair]);
    s_txq.desc[2 * pair].len = VR_HDR_LEN;
    s_txq.desc[2 * pair].flags = VR_DESC_F_NEXT;
    s_txq.desc[2 * pair].next = (uint16_t)(2 * pair + 1);
    s_txq.desc[2 * pair + 1].addr = PHY_OF((uint32_t)s_tx_data[pair]) + VR_HDR_LEN;
    s_txq.desc[2 * pair + 1].len = len;
    s_txq.desc[2 * pair + 1].flags = 0;
    s_txq.desc[2 * pair + 1].next = 0xFFFFu;
    vq_push(&s_txq, (uint16_t)(2 * pair));
    vq_notify(VRQ_TX);
    return (int)len;
}

int virtio_net_rx(NETIF *ifp, void *buf, uint32_t maxlen) {
    if (s_rxq.consumed == *s_rxq.used_idx) {
        return 0;
    }
    uint32_t slot = s_rxq.consumed % s_rxq.size;
    uint32_t pair = s_rxq.used_ring[slot].id / 2u;
    uint32_t dlen = s_rxq.used_ring[slot].len;
    s_rxq.consumed++;
    dlen = dlen > VR_HDR_LEN ? dlen - VR_HDR_LEN : 0;
    if (dlen > maxlen) {
        dlen = maxlen;
    }
    memcpy(buf, s_rx_data[pair] + VR_HDR_LEN, dlen);
    vr_desc_pair(s_rxq.desc, (uint16_t)pair, s_rx_data[pair]);
    vq_push(&s_rxq, (uint16_t)(2 * pair));
    vq_notify(VRQ_RX);
    return (int)dlen;
}

int virtio_net_init(NETIF *ifp) {
    uint8_t bus, dev;
    if (!pci_find_device(VIRTIO_VENDOR, VIRTIO_DEV_NET_LEGACY, &bus, &dev)) {
        kprintf("[vnet] no PCI device\n");
        return -1;
    }
    uint32_t bar0 = pci_read32(bus, dev, 0x10);
    kprintf("[vnet] pci %u:%u bar0=%x\n", bus, dev, bar0);
    if ((bar0 & 1u) == 0) {
        kprintf("[vnet] BAR0 not IO\n");
        return -1;
    }
    s_io = (uint16_t)((bar0 & ~0x3u) & 0xFFFFu);
    pci_enable_bus_master(bus, dev, 0x7);

    vr_inl(0x00);
    vr_outb(VR_STATUS, 0);
    vr_outb(VR_STATUS, VR_STATUS_ACK);
    vr_outb(VR_STATUS, VR_STATUS_ACK | VR_STATUS_DRIVER);
    vr_outl(VR_GUEST_FEATURES, 1u << VIRTIO_NET_F_MAC);

    for (uint8_t i = 0; i < 6; i++) {
        ifp->mac[i] = vr_inb((uint16_t)(VR_MAC + i));
    }

    vq_init(&s_rxq, VRQ_RX);
    vq_init(&s_txq, VRQ_TX);
    kprintf("[vnet] rx pfn=%x tx pfn=%x\n", vq_pfn_readback(VRQ_RX), vq_pfn_readback(VRQ_TX));

    vr_outb(VR_STATUS, VR_STATUS_ACK | VR_STATUS_DRIVER | VR_STATUS_DRIVER_OK);

    if (vr_setup_rx() != 0) {
        return -1;
    }

    for (uint16_t i = 0; i < VRQ_TX_PAIRS; i++) {
        s_tx_data[i] = (uint8_t *)get_kernel_pages(1);
        memset(s_tx_data[i], 0, VR_HDR_LEN);
        s_tx_free[i] = i;
    }
    s_tx_free_top = VRQ_TX_PAIRS;
    ifp->tx = virtio_net_tx;
    ifp->rx = virtio_net_rx;
    return 0;
}
