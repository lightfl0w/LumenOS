#include "kernel/boot_info.h"
#include "arch/x86_64/boot/mb2.h"

static struct BOOT_INFO g_boot;

const struct BOOT_INFO *boot_info(void) {
    const struct MB2_INFO *m = mb2_get();
    {
        g_boot.valid = m->valid;
        g_boot.cmdline = m->cmdline;
        g_boot.loader_name = m->boot_loader_name;
        g_boot.mem_lower_kb = m->mem_lower;
        g_boot.mem_upper_kb = m->mem_upper;
        g_boot.has_mmap = m->has_mmap;
        g_boot.mmap_count = m->mmap_count;
        for (uint32_t i = 0; i < m->mmap_count && i < BOOT_MAX_MMAP; i++) {
            g_boot.mmap[i].addr = m->mmap[i].addr;
            g_boot.mmap[i].len = m->mmap[i].len;
            g_boot.mmap[i].type = m->mmap[i].type;
        }
        g_boot.has_framebuffer = m->has_framebuffer;
        g_boot.framebuffer.addr = m->framebuffer.framebuffer_addr;
        g_boot.framebuffer.pitch = m->framebuffer.framebuffer_pitch;
        g_boot.framebuffer.width = m->framebuffer.framebuffer_width;
        g_boot.framebuffer.height = m->framebuffer.framebuffer_height;
        g_boot.framebuffer.bpp = m->framebuffer.framebuffer_bpp;
        for (int k = 0; k < 6; k++)
            g_boot.framebuffer.color_info[k] = m->framebuffer.color_info[k];
        g_boot.mem_top = mb2_mem_top();
        g_boot.has_bootdev = m->has_bootdev;
        g_boot.biosdev = m->biosdev;
        g_boot.partition = m->partition;
    }
    return &g_boot;
}
