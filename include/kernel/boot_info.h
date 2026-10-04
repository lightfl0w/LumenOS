#ifndef KERNEL_BOOT_INFO_H
#define KERNEL_BOOT_INFO_H

#include <stdint.h>

#define BOOT_MEM_AVAILABLE 1

#define BOOT_MAX_MMAP 64

struct BOOT_MMAP_ENTRY {
    uint64_t addr;
    uint64_t len;
    uint32_t type;
};

struct BOOT_FRAMEBUFFER {
    uint64_t addr;
    uint32_t pitch;
    uint32_t width;
    uint32_t height;
    uint8_t bpp;
    uint8_t color_info[6];
};

struct BOOT_INFO {
    int valid;
    const char *cmdline;
    const char *loader_name;
    uint32_t mem_lower_kb;
    uint32_t mem_upper_kb;
    int has_mmap;
    uint32_t mmap_count;
    struct BOOT_MMAP_ENTRY mmap[BOOT_MAX_MMAP];
    int has_framebuffer;
    struct BOOT_FRAMEBUFFER framebuffer;
    uint64_t mem_top;
    int has_bootdev;
    uint32_t biosdev;
    uint32_t partition;
};

const struct BOOT_INFO *boot_info(void);

#endif
