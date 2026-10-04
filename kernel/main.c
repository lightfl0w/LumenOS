#include "arch/bootinfo.h"
#include "arch/early.h"
#include "arch/interrupt/idt.h"
#include "arch/interrupt/interrupt.h"
#include "arch/irqchip.h"
#include "arch/seg.h"
#include "arch/smp.h"
#include "arch/tss.h"
#include "drivers/block/ata/ide.h"
#include "drivers/char/serial/console/io.h"
#include "drivers/char/serial/mouse.h"
#include "drivers/driver_ops.h"
#include "drivers/input/keyboard/keyboard.h"
#include "fs/fs.h"
#include "kernel/asm_func.h"
#include "kernel/assert.h"
#include "kernel/boot_info.h"
#include "kernel/gui/gfx.h"
#include "kernel/sched/thread.h"
#include "kernel/ssp.h"
#include "kernel/syscall/futex.h"
#include "kernel/syscall/syscall.h"
#include "kernel/time/pit.h"
#include "kernel/userprog/exec.h"
#include "kernel/userprog/process.h"
#include "lib/malloc/kmalloc.h"
#include "lib/png/png.h"
#include "lib/rand/rand.h"
#include "mm/pool.h"
#include "net/net.h"
#include "user/libc/stdio.h"
#include "lib/string/str.h"
#include "user/libc/syscall.h"

extern const uint8_t _binary__root_LumenOS_logo_png_start[];
extern const uint8_t _binary__root_LumenOS_logo_png_end[];

#define VRAM_VIRT 0x80000000UL

static void show_boot_logo(void) {
    const struct BOOT_INFO *bi = boot_info();
    const struct BOOT_FRAMEBUFFER *fb = &bi->framebuffer;
    int fw = (int)fb->width;
    int fh = (int)fb->height;

    const uint8_t *logo_data = _binary__root_LumenOS_logo_png_start;
    uint32_t logo_len = (uint32_t)(_binary__root_LumenOS_logo_png_end
                                 - _binary__root_LumenOS_logo_png_start);

    struct PNG_IMAGE logo_img;
    memset(&logo_img, 0, sizeof(logo_img));
    int ret = png_decode(logo_data, logo_len, &logo_img);
    if (ret != PNG_OK || logo_img.pixels == NULL ||
        logo_img.w <= 0 || logo_img.h <= 0) {
        return;
    }

    int src_w = logo_img.w;
    int src_h = logo_img.h;

    int dw = src_w / 4;
    int dh = src_h / 4;
    if (dw < 1) dw = 1;
    if (dh < 1) dh = 1;

    int dest_x = (fw - dw) / 2;
    int dest_y = (fh - dh) / 2;
    if (dest_x < 0) dest_x = 0;
    if (dest_y < 0) dest_y = 0;

    struct GFX_CANVAS logo_canvas = {
        .pixels = logo_img.pixels,
        .pitch  = src_w * 4,
        .w      = src_w,
        .h      = src_h,
        .bytes  = (size_t)src_w * (size_t)src_h * 4u,
    };

    uint32_t fb_bytes = (uint32_t)fb->pitch * (uint32_t)fh;
    uintptr_t vram_virt =
        (uintptr_t)VRAM_VIRT + ((uintptr_t)fb->addr & 0x1FFFFFUL);
    struct GFX_CANVAS fb_canvas = {
        .pixels = (uint32_t *)(void *)vram_virt,
        .pitch  = fb->pitch,
        .w      = fw,
        .h      = fh,
        .bytes  = fb_bytes,
    };

    gfx_blit_scale(&fb_canvas, dest_x, dest_y, dw, dh,
                   &logo_canvas, 0, 0, src_w, src_h);

    png_image_free(&logo_img);
}

void drivers_init(int min_level, int max_level) {
    struct DRIVER_OPS table[16];
    int n = 0;
    for (const struct DRIVER_OPS *d = __drivers_start; d != __drivers_end && n < 16; ++d) {
        if (d->level < min_level || d->level > max_level)
            continue;
        int j = n;
        while (j > 0 && table[j - 1].level > d->level) {
            table[j] = table[j - 1];
            --j;
        }
        table[j] = *d;
        ++n;
    }
    for (int i = 0; i < n; ++i) {
        if (table[i].init)
            table[i].init();
    }
}

void kmain(uint32_t magic, void *mbi_ptr, uint32_t kphys) {
    arch_early_init();
    kernel_kphys = kphys;

    stack_canary_init();
    rand_init();

    mb2_init(magic, mbi_ptr);
    const struct BOOT_INFO *bi = boot_info();
    const struct BOOT_FRAMEBUFFER *fb = &bi->framebuffer;
    int fw = (int)fb->width;
    int fh = (int)fb->height;
    int fbpp = (int)fb->bpp;
    if (fbpp <= 0)
        fbpp = 32;
    int fpitch = (int)fb->pitch;
    if (fpitch <= 0)
        fpitch = fw * (fbpp / 8);
    uint32_t bytes = (uint32_t)fpitch * (uint32_t)fh;
    uintptr_t vram_virt = (uintptr_t)VRAM_VIRT + ((uintptr_t)fb->addr & 0x1FFFFFUL);
    io_init((uint8_t *)vram_virt, fw, fh, bytes, fpitch, fbpp);
    struct GFX_FB_FORMAT fmt = {
        fbpp,
        fb->color_info[0],
        fb->color_info[1],
        fb->color_info[2],
        fb->color_info[3],
        fb->color_info[4],
        fb->color_info[5],
    };
    gfx_set_fb_format(&fmt);

    if (bi->cmdline != NULL) {
        for (const char *c = bi->cmdline; *c; c++) {
            if (c[0] == 'v' && c[1] == 'e' && c[2] == 'r' && c[3] == 'b' && c[4] == 'o' &&
                c[5] == 's' && c[6] == 'e') {
                console_set_verbose(1);
                break;
            }
        }
    }
    mb2_dump();
    kprintf_v("[diag] magic=%#x mbi=%#x fb: %dx%d bpp=%d pitch=%d addr=%#x\n", magic,
              (uint32_t)(uintptr_t)mbi_ptr, fw, fh, fbpp, fpitch, (uint32_t)fb->addr);
    kprintf_v("[diag] vram virt=%#x rgb masks r=%d/%d g=%d/%d b=%d/%d\n", (uint32_t)vram_virt,
              fmt.r_pos, fmt.r_bits, fmt.g_pos, fmt.g_bits, fmt.b_pos, fmt.b_bits);

    kprintf_v("[init] mm\n");
    mm_init();

    kprintf_v("[init] kheap\n");
    kheap_init();
    kheap_selftest();

    io_clear_screen();
    show_boot_logo();

    gdt_init();

    percpu_init();
    set_current((struct TASK *)0);

    tss_init();
    idt_init();

    syscall_init();
    futex_init();

    acpi_init();
    pit_init(PIT_HZ);
    if (apic_init() != 0) {
        kprintf("[WARN] apic_init failed, fallback PIC\n");
        pic_init();
    }
    drivers_init(0, 19);
    thread_init();

    drivers_init(20, 99);
    filesys_init();
    smp_init();
    if (net_enable)
        net_init();

    process_execute("/bin/shell.elf", "shell");
    for (;;) {
        cpu_idle();
        thread_yield();
    }
}
