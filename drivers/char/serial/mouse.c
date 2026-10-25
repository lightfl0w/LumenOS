#include "drivers/char/serial/mouse.h"
#include "drivers/driver_ops.h"
#include "arch/x86_64/irq.h"

#include "drivers/input/keyboard/keyboard.h"
#include "kernel/input/evdev.h"
#include "arch/asm_func.h"

#define KBD_DATA 0x60
#define KBD_STATUS 0x64
#define KBD_CMD 0x64

static struct evdev_dev *evdev_mouse;

static uint8_t pkt[3];
static int idx = 0;

static void ctrl_wait_write(void) {
    while (inb(KBD_STATUS) & 0x02) {
    }
}

static void ctrl_wait_read(void) {
    while (!(inb(KBD_STATUS) & 0x01)) {
    }
}

static void aux_write(uint8_t v) {
    ctrl_wait_write();
    outb(KBD_CMD, 0xD4);
    ctrl_wait_write();
    outb(KBD_DATA, v);
}

static uint8_t aux_read(void) {
    ctrl_wait_read();
    return inb(KBD_DATA);
}

static int mouse_drv(void) {
    mouse_init();
    return 0;
}

DRIVER_REGISTER("mouse", 15, mouse_drv);

void mouse_init(void) {
    keyboard_flush_pending();

    evdev_mouse = evdev_create("ps2mouse", BUS_I8042, 0x1f, 0x0002, "/dev/input/event1");
    evdev_enable_rel(evdev_mouse, REL_X);
    evdev_enable_rel(evdev_mouse, REL_Y);
    evdev_enable_rel(evdev_mouse, REL_WHEEL);
    evdev_enable_key(evdev_mouse, BTN_LEFT);
    evdev_enable_key(evdev_mouse, BTN_RIGHT);
    evdev_enable_key(evdev_mouse, BTN_MIDDLE);

    ctrl_wait_write();
    outb(KBD_CMD, 0xA8);

    ctrl_wait_write();
    outb(KBD_CMD, 0x20);
    ctrl_wait_read();
    uint8_t status = inb(KBD_DATA);
    status |= 0x02;
    status &= (uint8_t)~0x20;
    ctrl_wait_write();
    outb(KBD_CMD, 0x60);
    ctrl_wait_write();
    outb(KBD_DATA, status);

    aux_write(0xF6);
    (void)aux_read();
    aux_write(0xF4);
    (void)aux_read();
}

void mouse_handler(void) {
    uint8_t b = inb(KBD_DATA);

    if (idx == 0 && !(b & 0x08))
        return;
    pkt[idx++] = b;
    if (idx < 3)
        return;
    idx = 0;

    int dx = (int)pkt[1] - ((int)(pkt[0] << 4) & 0x100);
    int dy = (int)pkt[2] - ((int)(pkt[0] << 3) & 0x100);
    uint8_t buttons = pkt[0] & 0x07;

    if (evdev_mouse != NULL) {
        evdev_report(evdev_mouse, EV_REL, REL_X, dx);
        evdev_report(evdev_mouse, EV_REL, REL_Y, -dy);
        evdev_report(evdev_mouse, EV_KEY, BTN_LEFT, buttons & 1 ? 1 : 0);
        evdev_report(evdev_mouse, EV_KEY, BTN_RIGHT, buttons & 2 ? 1 : 0);
        evdev_report(evdev_mouse, EV_KEY, BTN_MIDDLE, buttons & 4 ? 1 : 0);
        evdev_report(evdev_mouse, EV_SYN, SYN_REPORT, 0);
    }
}

static void mouse_handler_irq(uint8_t vector) {
    mouse_handler();
}
IRQ_REGISTER(IRQ_MOUSE, mouse_handler_irq, "mouse");
