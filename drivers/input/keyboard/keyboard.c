#include "drivers/input/keyboard/keyboard.h"
#include "drivers/char/serial/console/io.h"
#include "drivers/char/serial/tty.h"
#include "drivers/driver_ops.h"
#include "arch/x86_64/irq.h"
#include "kernel/input/evdev.h"

#include "arch/asm_func.h"
#include "kernel/sched/thread.h"

#define KEYBOARD_DATA 0x60
#define KEYBOARD_STATUS 0x64

void keyboard_flush_pending(void) {
    while (inb(KEYBOARD_STATUS) & 0x01) {
        (void)inb(KEYBOARD_DATA);
    }
}

static int keyboard_drv(void) {
    keyboard_init();
    return 0;
}

DRIVER_REGISTER("keyboard", 10, keyboard_drv);

#define SC_SHIFT_L_DOWN 0x2A
#define SC_SHIFT_R_DOWN 0x36
#define SC_SHIFT_L_UP 0xAA
#define SC_SHIFT_R_UP 0xB6
#define SC_CAPS_DOWN 0x3A
#define SC_CTRL_L_DOWN 0x1D
#define SC_CTRL_L_UP 0x9D
#define SC_ALT_L_DOWN 0x38
#define SC_ALT_L_UP 0xB8
#define SC_C_DOWN 0x2E

#define KBD_CHAR_CTRL_U (1)
#define KBD_CHAR_CTRL_L (12)

struct TTY_IOQUEUE keyboard_ioq;

static struct evdev_dev *evdev_kbd;

static const uint16_t sc_to_key[128] = {
    [1] = KEY_ESC,
    [2] = KEY_1, [3] = KEY_2, [4] = KEY_3, [5] = KEY_4, [6] = KEY_5, [7] = KEY_6,
    [8] = KEY_7, [9] = KEY_8, [10] = KEY_9, [11] = KEY_0,
    [12] = KEY_MINUS, [13] = KEY_EQUAL, [14] = KEY_BACKSPACE, [15] = KEY_TAB,
    [16] = KEY_Q, [17] = KEY_W, [18] = KEY_E, [19] = KEY_R, [20] = KEY_T, [21] = KEY_Y,
    [22] = KEY_U, [23] = KEY_I, [24] = KEY_O, [25] = KEY_P,
    [26] = KEY_LEFTBRACE, [27] = KEY_RIGHTBRACE, [28] = KEY_ENTER, [29] = KEY_LEFTCTRL,
    [30] = KEY_A, [31] = KEY_S, [32] = KEY_D, [33] = KEY_F, [34] = KEY_G, [35] = KEY_H,
    [36] = KEY_J, [37] = KEY_K, [38] = KEY_L,
    [39] = KEY_SEMICOLON, [40] = KEY_APOSTROPHE, [41] = KEY_GRAVE, [42] = KEY_LEFTSHIFT,
    [43] = KEY_BACKSLASH,
    [44] = KEY_Z, [45] = KEY_X, [46] = KEY_C, [47] = KEY_V, [48] = KEY_B, [49] = KEY_N,
    [50] = KEY_M,
    [51] = KEY_COMMA, [52] = KEY_DOT, [53] = KEY_SLASH, [54] = KEY_RIGHTSHIFT,
    [55] = KEY_KPASTERISK, [56] = KEY_LEFTALT, [57] = KEY_SPACE, [58] = KEY_CAPSLOCK,
    [59] = KEY_F1, [60] = KEY_F2, [61] = KEY_F3, [62] = KEY_F4, [63] = KEY_F5,
    [64] = KEY_F6, [65] = KEY_F7, [66] = KEY_F8, [67] = KEY_F9, [68] = KEY_F10,
    [69] = KEY_NUMLOCK, [70] = KEY_SCROLLLOCK,
    [71] = KEY_KP7, [72] = KEY_KP8, [73] = KEY_KP9, [74] = KEY_KPMINUS,
    [75] = KEY_KP4, [76] = KEY_KP5, [77] = KEY_KP6, [78] = KEY_KPPLUS,
    [79] = KEY_KP1, [80] = KEY_KP2, [81] = KEY_KP3, [82] = KEY_KP0, [83] = KEY_KPDOT,
    [87] = KEY_F11, [88] = KEY_F12,
};

void keyboard_init(void) {
    ioq_init(&keyboard_ioq);
    keyboard_flush_pending();

    evdev_kbd = evdev_create("atkbd", BUS_I8042, 0x1f, 0x0001, "/dev/input/event0");
    for (int sc = 0; sc < 128; sc++) {
        if (sc_to_key[sc] != 0) {
            evdev_enable_key(evdev_kbd, sc_to_key[sc]);
        }
    }
    evdev_enable_key(evdev_kbd, KEY_RIGHTSHIFT);
}

static const char keymap[2][128] = {
    {0,   0x1b, '1', '2', '3', '4', '5', '6', '7',  '8', '9', '0',  '-',  '=', 0x08, '\t',
     'q', 'w',  'e', 'r', 't', 'y', 'u', 'i', 'o',  'p', '[', ']',  '\n', 0,   'a',  's',
     'd', 'f',  'g', 'h', 'j', 'k', 'l', ';', '\'', '`', 0,   '\\', 'z',  'x', 'c',  'v',
     'b', 'n',  'm', ',', '.', '/', 0,   '*', 0,    ' ', 0,   0,    0,    0,   0,    0,
     0,   0,    0,   0,   0,   0,   0,   0,   0,    0,   0,   0,    0,    0,   0,    0,
     0,   0,    0,   0,   0,   0,   0,   0,   0,    0,   0,   0,    0,    0,   0,    0,
     0,   0,    0,   0,   0,   0,   0,   0,   0,    0,   0,   0,    0,    0,   0,    0,
     0,   0,    0,   0,   0,   0,   0,   0,   0,    0,   0,   0,    0,    0,   0,    0},
    {0,   0x1b, '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_',  '+', 0x08, '\t',
     'Q', 'W',  'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', '\n', 0,   'A',  'S',
     'D', 'F',  'G', 'H', 'J', 'K', 'L', ':', '"', '~', 0,   '|', 'Z',  'X', 'C',  'V',
     'B', 'N',  'M', '<', '>', '?', 0,   '*', 0,   ' ', 0,   0,   0,    0,   0,    0,
     0,   0,    0,   0,   0,   0,   0,   0,   0,    0,   0,   0,    0,   0,    0,
     0,   0,    0,   0,   0,   0,   0,   0,   0,    0,   0,   0,    0,   0,    0,
     0,   0,    0,   0,   0,   0,   0,   0,   0,    0,   0,   0,    0,   0,    0,
     0,   0,    0,   0,   0,   0,   0,   0,   0,    0,   0,   0,    0,   0,    0}};

static uint8_t shift = 0;
static uint8_t caps = 0;
static uint8_t ctrl = 0;
static uint8_t alt = 0;

char keyboard_translate(uint8_t scancode, int shifted) {
    if (scancode >= 128)
        return 0;
    char c = keymap[shifted ? 1 : 0][scancode];
    if (caps && c >= 'a' && c <= 'z')
        c -= 32;
    return c;
}

static void keyboard_report_evdev(uint8_t sc) {
    if (evdev_kbd == NULL) {
        return;
    }
    uint16_t code = sc_to_key[sc & 0x7f];
    if (code == 0) {
        return;
    }
    evdev_report(evdev_kbd, EV_KEY, code, (sc & 0x80) ? 0 : 1);
    evdev_report(evdev_kbd, EV_SYN, SYN_REPORT, 0);
}

void keyboard_handler(void) {
    if (!(inb(KEYBOARD_STATUS) & 0x01)) {
        return;
    }
    uint8_t sc = inb(KEYBOARD_DATA);

    if (sc == SC_SHIFT_L_DOWN || sc == SC_SHIFT_R_DOWN) {
        shift = 1;
    } else if (sc == SC_SHIFT_L_UP || sc == SC_SHIFT_R_UP) {
        shift = 0;
    } else if (sc == SC_CAPS_DOWN) {
        caps = !caps;
    } else if (sc == SC_CTRL_L_DOWN) {
        ctrl = 1;
    } else if (sc == SC_CTRL_L_UP) {
        ctrl = 0;
    } else if (sc == SC_ALT_L_DOWN) {
        alt = 1;
    } else if (sc == SC_ALT_L_UP) {
        alt = 0;
    }

    keyboard_report_evdev(sc);

    if (sc == SC_SHIFT_L_DOWN || sc == SC_SHIFT_R_DOWN)
        return;
    if (sc == SC_SHIFT_L_UP || sc == SC_SHIFT_R_UP)
        return;
    if (sc == SC_CAPS_DOWN)
        return;
    if (sc == SC_CTRL_L_DOWN || sc == SC_CTRL_L_UP)
        return;
    if (sc == SC_ALT_L_DOWN || sc == SC_ALT_L_UP)
        return;
    if (sc & 0x80) {
        return;
    }
    if (sc >= 128) {
        return;
    }

    if (ctrl && sc == SC_C_DOWN) {
        tty_sigint_foreground();
        return;
    }

    if (ctrl && sc < 0x3b) {
        char c = 0;
        if (sc == KBD_SC_U)
            c = (char)KBD_CHAR_CTRL_U;
        else if (sc == KBD_SC_L)
            c = (char)KBD_CHAR_CTRL_L;
        if (c) {
            if (!ioq_full(&keyboard_ioq))
                ioq_putchar(&keyboard_ioq, c);
            return;
        }
    }

    char c = keymap[shift ? 1 : 0][sc];
    if (caps && c >= 'a' && c <= 'z')
        c -= 32;

    if (c && !ioq_full(&keyboard_ioq)) {
        ioq_putchar(&keyboard_ioq, c);
    }
}

static void keyboard_handler_irq(uint8_t vector) {
    keyboard_handler();
}
IRQ_REGISTER(IRQ_KEYBOARD, keyboard_handler_irq, "keyboard");
