#ifndef UAPI_EVDEV_H
#define UAPI_EVDEV_H

#include <stdint.h>

#define EV_SYN 0x00
#define EV_KEY 0x01
#define EV_REL 0x02
#define EV_ABS 0x03
#define EV_MSC 0x04
#define EV_SW 0x05
#define EV_LED 0x11
#define EV_SND 0x12
#define EV_REP 0x14
#define EV_FF 0x15
#define EV_PWR 0x16
#define EV_FF_STATUS 0x17
#define EV_MAX 0x1f

#define SYN_REPORT 0
#define SYN_CONFIG 1
#define SYN_MT_REPORT 2
#define SYN_DROPPED 3

#define REL_X 0x00
#define REL_Y 0x01
#define REL_Z 0x02
#define REL_WHEEL 0x08
#define REL_MAX 0x0f

#define ABS_X 0x00
#define ABS_Y 0x01
#define ABS_MAX 0x3f

#define KEY_RESERVED 0
#define KEY_ESC 1
#define KEY_1 2
#define KEY_2 3
#define KEY_3 4
#define KEY_4 5
#define KEY_5 6
#define KEY_6 7
#define KEY_7 8
#define KEY_8 9
#define KEY_9 10
#define KEY_0 11
#define KEY_MINUS 12
#define KEY_EQUAL 13
#define KEY_BACKSPACE 14
#define KEY_TAB 15
#define KEY_Q 16
#define KEY_W 17
#define KEY_E 18
#define KEY_R 19
#define KEY_T 20
#define KEY_Y 21
#define KEY_U 22
#define KEY_I 23
#define KEY_O 24
#define KEY_P 25
#define KEY_LEFTBRACE 26
#define KEY_RIGHTBRACE 27
#define KEY_ENTER 28
#define KEY_LEFTCTRL 29
#define KEY_A 30
#define KEY_S 31
#define KEY_D 32
#define KEY_F 33
#define KEY_G 34
#define KEY_H 35
#define KEY_J 36
#define KEY_K 37
#define KEY_L 38
#define KEY_SEMICOLON 39
#define KEY_APOSTROPHE 40
#define KEY_GRAVE 41
#define KEY_LEFTSHIFT 42
#define KEY_BACKSLASH 43
#define KEY_Z 44
#define KEY_X 45
#define KEY_C 46
#define KEY_V 47
#define KEY_B 48
#define KEY_N 49
#define KEY_M 50
#define KEY_COMMA 51
#define KEY_DOT 52
#define KEY_SLASH 53
#define KEY_RIGHTSHIFT 54
#define KEY_KPASTERISK 55
#define KEY_LEFTALT 56
#define KEY_SPACE 57
#define KEY_CAPSLOCK 58
#define KEY_F1 59
#define KEY_F2 60
#define KEY_F3 61
#define KEY_F4 62
#define KEY_F5 63
#define KEY_F6 64
#define KEY_F7 65
#define KEY_F8 66
#define KEY_F9 67
#define KEY_F10 68
#define KEY_NUMLOCK 69
#define KEY_SCROLLLOCK 70
#define KEY_KP7 71
#define KEY_KP8 72
#define KEY_KP9 73
#define KEY_KPMINUS 74
#define KEY_KP4 75
#define KEY_KP5 76
#define KEY_KP6 77
#define KEY_KPPLUS 78
#define KEY_KP1 79
#define KEY_KP2 80
#define KEY_KP3 81
#define KEY_KP0 82
#define KEY_KPDOT 83
#define KEY_F11 87
#define KEY_F12 88
#define KEY_KPEQUAL 117
#define KEY_KPENTER 96
#define KEY_RIGHTCTRL 97
#define KEY_RIGHTALT 100
#define KEY_HOME 102
#define KEY_UP 103
#define KEY_PAGEUP 104
#define KEY_LEFT 105
#define KEY_RIGHT 106
#define KEY_END 107
#define KEY_DOWN 108
#define KEY_PAGEDOWN 109
#define KEY_INSERT 110
#define KEY_DELETE 111
#define KEY_KPJPCOMMA 95
#define KEY_KPSLASH 98
#define KEY_MUTE 113
#define KEY_VOLUMEDOWN 114
#define KEY_VOLUMEUP 115
#define KEY_COMPOSE 127

#define BTN_MOUSE 0x110
#define BTN_LEFT 0x110
#define BTN_RIGHT 0x111
#define BTN_MIDDLE 0x112
#define BTN_SIDE 0x113
#define BTN_EXTRA 0x114
#define BTN_MAX 0x11f

#define KEY_MAX 0x2ff
#define MSC_MAX 0x07
#define SW_MAX 0x0f
#define LED_MAX 0x0f
#define SND_MAX 0x07
#define FF_MAX 0x7f

#define BUS_PCI 0x01
#define BUS_ISA 0x02
#define BUS_USB 0x03
#define BUS_I8042 0x11

#define INPUT_KEY_MAX (KEY_MAX + 1)
#define INPUT_REL_MAX (REL_MAX + 1)

struct input_id {
    uint16_t bustype;
    uint16_t vendor;
    uint16_t product;
    uint16_t version;
};

struct input_event {
    int64_t tv_sec;
    int64_t tv_usec;
    uint16_t type;
    uint16_t code;
    int32_t value;
};

#define _IOC_NRBITS 8
#define _IOC_TYPEBITS 8
#define _IOC_SIZEBITS 14
#define _IOC_DIRBITS 2

#define _IOC(dir, type, nr, size) \
    (((uint32_t)(dir) << 30) | ((uint32_t)(size) << 16) | ((uint32_t)(type) << 8) | (uint32_t)(nr))

#define _IOC_WRITE 1u
#define _IOC_READ 2u

#define _IOC_DIR(nr) (((nr) >> 30) & 3u)
#define _IOC_TYPE(nr) (((nr) >> 8) & 0xffu)
#define _IOC_NR(nr) ((nr) & 0xffu)
#define _IOC_SIZE(nr) ((nr) >> 16 & 0x3fffu)

#define EVIOCGVERSION _IOC(_IOC_READ, 'E', 0x01, sizeof(int))
#define EVIOCGID _IOC(_IOC_READ, 'E', 0x02, sizeof(struct input_id))
#define EVIOCGREP _IOC(_IOC_READ, 'E', 0x03, 2 * sizeof(int))
#define EVIOCSREP _IOC(_IOC_WRITE, 'E', 0x03, 2 * sizeof(int))
#define EVIOCGKEY(len) _IOC(_IOC_READ, 'E', 0x18, (len))
#define EVIOCGNAME(len) _IOC(_IOC_READ, 'E', 0x06, (len))
#define EVIOCGPHYS(len) _IOC(_IOC_READ, 'E', 0x07, (len))
#define EVIOCGUNIQ(len) _IOC(_IOC_READ, 'E', 0x08, (len))
#define EVIOCGBIT(ev, len) _IOC(_IOC_READ, 'E', 0x20 + (ev), (len))
#define EVIOCGABS(abs) _IOC(_IOC_READ, 'E', 0x40 + (abs), sizeof(struct input_absinfo))
#define EVIOCSABS(abs) _IOC(_IOC_WRITE, 'E', 0xc0 + (abs), sizeof(struct input_absinfo))
#define EVIOCGRAB _IOC(_IOC_WRITE, 'E', 0x90, sizeof(int))

struct input_absinfo {
    int32_t value;
    int32_t minimum;
    int32_t maximum;
    int32_t fuzz;
    int32_t flat;
    int32_t resolution;
};

#define EVDEV_MAJOR 13
#define EVDEV_EVENT_BASE 64

#endif
