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
#include "lib/rand/rand.h"
#include "mm/pool.h"
#include "net/net.h"
#include "user/libc/stdio.h"
#include "user/libc/syscall.h"

#define VRAM_VIRT 0x80000000UL

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

    io_clear_screen();
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

    kprintf_v("[init] gdt\n");
    gdt_init();

    kprintf_v("[init] percpu\n");
    percpu_init();
    set_current((struct TASK *)0);

    kprintf_v("[init] tss\n");
    tss_init();

    kprintf_v("[init] idt\n");
    idt_init();

    kprintf_v("[init] syscall\n");
    syscall_init();
    futex_init();

    kprintf_v("[init] ppmode\n");
    kprintf("[OK] long mode (CR0.PG=1 CR4.PAE=1 EFER.LME=1 CS.L=1)\n");

    kprintf_v("[init] acpi\n");
    acpi_init();

    kprintf_v("[init] apic\n");
    pit_init(PIT_HZ);
    if (apic_init() != 0) {
        kprintf("[WARN] apic_init failed, fallback PIC\n");
        pic_init();
    }

    kprintf_v("[init] drivers char\n");
    drivers_init(0, 19);

    kprintf_v("[init] threads\n");
    thread_init();
    kprintf("[OK] kernel threads ready\n");

    set_text_color(10);
    kprintf("[OK] kernel init done, enable IRQs\n");

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
