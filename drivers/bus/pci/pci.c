#include "drivers/bus/pci/pci.h"

#include "arch/asm_func.h"
#include "drivers/driver_ops.h"
#include "lib/printf/printf.h"

#define PCI_CONFIG_ADDR 0xCF8u
#define PCI_CONFIG_DATA 0xCFCu
#define PCI_ENABLE_BIT 0x80000000u
#define PCI_DEV_NR 32u
#define PCI_FUNC_NR 8u
#define PCI_MAX_DRIVERS 8

struct PCI_DRIVER {
    const char *name;
    int (*match)(uint32_t bdf, uint16_t vendor, uint16_t device, uint8_t base_class,
                 uint8_t subclass);
    int (*probe)(uint32_t bdf);
};

static struct PCI_DRIVER pci_drivers[PCI_MAX_DRIVERS];
static int pci_driver_count;

static uint32_t pci_addr_of(uint32_t bdf, uint8_t reg) {
    return PCI_ENABLE_BIT | PCI_BDF_BUS(bdf) << 16 | PCI_BDF_DEV(bdf) << 11 |
           PCI_BDF_FUNC(bdf) << 8 | (reg & 0xFCu);
}

uint32_t pci_config_read32(uint32_t bdf, uint8_t reg) {
    outl(PCI_CONFIG_ADDR, pci_addr_of(bdf, reg));
    return inl(PCI_CONFIG_DATA);
}

void pci_config_write32(uint32_t bdf, uint8_t reg, uint32_t val) {
    outl(PCI_CONFIG_ADDR, pci_addr_of(bdf, reg));
    outl(PCI_CONFIG_DATA, val);
}

uint16_t pci_config_read16(uint32_t bdf, uint8_t reg) {
    uint32_t aligned = pci_addr_of(bdf, (uint8_t)(reg & 0xFCu));
    uint32_t shift = (reg & 2u) ? 16u : 0u;

    outl(PCI_CONFIG_ADDR, aligned);
    return (uint16_t)(inl(PCI_CONFIG_DATA) >> shift);
}

int pci_find_class(uint8_t base_class, uint8_t subclass, uint32_t *bdf) {
    for (uint32_t dev = 0; dev < PCI_DEV_NR; dev++) {
        uint32_t slot = PCI_BDF(0, dev, 0);
        uint32_t vendor = pci_config_read32(slot, PCI_CFG_VENDOR);
        if ((vendor & 0xFFFFu) == 0xFFFFu) {
            continue;
        }
        uint32_t class = pci_config_read32(slot, PCI_CFG_CLASS);
        uint8_t cls = (uint8_t)(class >> 24);
        uint8_t sub = (uint8_t)(class >> 16);
        if (cls == base_class && sub == subclass) {
            *bdf = slot;
            return 0;
        }
    }
    return -1;
}

void pci_cmd_set(uint32_t bdf, uint32_t bits) {
    uint32_t cmd = pci_config_read32(bdf, PCI_CFG_CMD);

    pci_config_write32(bdf, PCI_CFG_CMD, cmd | bits);
}

int pci_read_bar(uint32_t bdf, uint8_t index, struct PCI_BAR *out) {
    uint8_t reg = (uint8_t)(PCI_CFG_BAR0 + index * 4);
    uint32_t lo = pci_config_read32(bdf, reg);
    if (lo == 0xFFFFFFFFu) {
        return -1;
    }

    uint8_t type = lo & 0x6u;
    uint32_t hi = 0;
    out->is_64 = (type == 0x4u && index < 5);
    if (out->is_64) {
        hi = pci_config_read32(bdf, (uint8_t)(reg + 4));
    }
    out->addr = ((uint64_t)hi << 32) | (lo & ~0xFu);
    out->is_io = (lo & 0x1u) != 0;

    pci_config_write32(bdf, reg, 0xFFFFFFFFu);
    uint32_t size = pci_config_read32(bdf, reg);
    pci_config_write32(bdf, reg, lo);
    if (out->is_64) {
        pci_config_write32(bdf, (uint8_t)(reg + 4), 0xFFFFFFFFu);
        uint32_t hi_size = pci_config_read32(bdf, (uint8_t)(reg + 4));
        pci_config_write32(bdf, (uint8_t)(reg + 4), hi);
        size |= hi_size;
    }

    out->size = (uint64_t)(~(size & ~0xFu)) + 1u;
    return 0;
}

int pci_register_driver(const char *name,
                        int (*match)(uint32_t bdf, uint16_t vendor, uint16_t device,
                                     uint8_t base_class, uint8_t subclass),
                        int (*probe)(uint32_t bdf)) {
    if (pci_driver_count >= PCI_MAX_DRIVERS) {
        return -1;
    }
    pci_drivers[pci_driver_count].name = name;
    pci_drivers[pci_driver_count].match = match;
    pci_drivers[pci_driver_count].probe = probe;
    pci_driver_count++;
    return 0;
}

static void pci_probe_device(uint32_t bdf) {
    uint32_t id = pci_config_read32(bdf, PCI_CFG_VENDOR);
    uint16_t vendor = (uint16_t)(id & 0xFFFFu);
    uint16_t device = (uint16_t)(id >> 16);
    if (vendor == 0xFFFFu) {
        return;
    }

    uint32_t class = pci_config_read32(bdf, PCI_CFG_CLASS);
    uint8_t base_class = (uint8_t)(class >> 24);
    uint8_t subclass = (uint8_t)(class >> 16);

    for (int i = 0; i < pci_driver_count; i++) {
        if (pci_drivers[i].match(bdf, vendor, device, base_class, subclass) == 0) {
            if (pci_drivers[i].probe(bdf) == 0) {
                kprintf("[pci] %s claimed %02x:%02x.%u\n", pci_drivers[i].name,
                        PCI_BDF_BUS(bdf), PCI_BDF_DEV(bdf), PCI_BDF_FUNC(bdf));
            }
            return;
        }
    }
}

static void pci_scan_bus(uint8_t bus) {
    for (uint32_t dev = 0; dev < PCI_DEV_NR; dev++) {
        uint32_t hdr = pci_config_read32(PCI_BDF(bus, dev, 0), PCI_CFG_VENDOR);
        if ((hdr & 0xFFFFu) == 0xFFFFu) {
            continue;
        }

        uint32_t type = pci_config_read32(PCI_BDF(bus, dev, 0), 0x0Cu) >> 16 & 0xFFu;
        if (type == 0x01u) {
            uint32_t sec = pci_config_read32(PCI_BDF(bus, dev, 0), 0x18u);
            pci_scan_bus((uint8_t)(sec >> 8));
            continue;
        }
        if (type != 0x00u) {
            continue;
        }

        uint8_t functions = 1;
        uint16_t ht = pci_config_read16(PCI_BDF(bus, dev, 0), 0x0Eu);
        if (ht & 0x80u) {
            functions = PCI_FUNC_NR;
        }
        for (uint8_t func = 0; func < functions; func++) {
            pci_probe_device(PCI_BDF(bus, dev, func));
        }
    }
}

static int pci_bus_init(void) {
    pci_scan_bus(0);
    return 0;
}

DRIVER_REGISTER("pci-bus", 12, pci_bus_init);
