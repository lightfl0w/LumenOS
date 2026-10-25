#include "uapi/evdev.h"
#include "kernel/input/evdev.h"

#include "arch/time/pit.h"
#include "arch/x86_64/irq/interrupt/interrupt.h"
#include "fs/chardev.h"
#include "fs/devfs.h"
#include "fs/file.h"
#include "kernel/sched/thread.h"
#include "kernel/sync/sync.h"
#include "drivers/char/serial/rtc.h"
#include "lib/malloc/kmalloc.h"
#include "lib/string/str.h"

#include <stdint.h>

#define EV_VERSION 0x010001
#define EVDEV_MAX_DEVICES 8
#define EVDEV_RING 128

struct evdev_client {
    struct evdev_dev *dev;
    uint32_t next;
    struct evdev_client *link;
};

struct evdev_dev {
    uint8_t used;
    uint16_t minor;
    char name[32];
    struct input_id id;
    uint8_t key_cap[INPUT_KEY_MAX / 8];
    uint8_t key_state[INPUT_KEY_MAX / 8];
    uint8_t rel_cap[INPUT_REL_MAX / 8];
    struct input_event ring[EVDEV_RING];
    uint32_t head;
    struct SCHED_LOCK lock;
    struct TASK *waiter;
    struct FILE *grab;
};

static struct evdev_dev evdev_devs[EVDEV_MAX_DEVICES];

static int evdev_open(struct FILE *file, uint32_t devnum);
static void evdev_release(struct FILE *file);
static int32_t evdev_read(struct FILE *file, void *buf, int32_t count);
static int32_t evdev_ioctl(struct FILE *file, uint32_t cmd, uint64_t arg);
static uint32_t evdev_poll(struct FILE *file);

static const struct CHARDEV_OPS evdev_ops = {evdev_open,  evdev_release, evdev_read,
                                              NULL,        evdev_ioctl,   evdev_poll};

static void evdev_bit_set(uint8_t *map, uint32_t bit) {
    map[bit / 8] |= (uint8_t)(1u << (bit % 8));
}

static void evdev_bit_clear(uint8_t *map, uint32_t bit) {
    map[bit / 8] &= (uint8_t)~(1u << (bit % 8));
}

static int evdev_bit_test(const uint8_t *map, uint32_t bit) {
    return (map[bit / 8] >> (bit % 8)) & 1;
}

static void evdev_now(struct input_event *ev) {
    ev->tv_sec = (int64_t)rtc_unix_time();
    ev->tv_usec = (int64_t)((tick % PIT_HZ) * (1000000u / PIT_HZ));
}

static void evdev_wakeup(struct evdev_dev *dev) {
    struct TASK *w = dev->waiter;
    dev->waiter = 0;
    if (w != NULL) {
        thread_unblock(w);
    }
}

void evdev_report(struct evdev_dev *dev, uint16_t type, uint16_t code, int32_t value) {
    if (dev == NULL || !dev->used) {
        return;
    }
    lock_acquire(&dev->lock);
    struct input_event *ev = &dev->ring[dev->head % EVDEV_RING];
    evdev_now(ev);
    ev->type = type;
    ev->code = code;
    ev->value = value;
    dev->head++;
    if (type == EV_KEY && code <= KEY_MAX) {
        if (value) {
            evdev_bit_set(dev->key_state, code);
        } else {
            evdev_bit_clear(dev->key_state, code);
        }
    }
    evdev_wakeup(dev);
    lock_release(&dev->lock);
}

static int evdev_major_registered;

struct evdev_dev *evdev_create(const char *name, uint16_t bustype, uint16_t vendor,
                               uint16_t product, const char *devnode) {
    struct evdev_dev *dev = NULL;
    for (int i = 0; i < EVDEV_MAX_DEVICES; i++) {
        if (!evdev_devs[i].used) {
            dev = &evdev_devs[i];
            break;
        }
    }
    if (dev == NULL) {
        return NULL;
    }
    memset(dev, 0, sizeof(*dev));
    dev->used = 1;
    dev->minor = EVDEV_EVENT_BASE + (uint16_t)(dev - evdev_devs);
    strncpy(dev->name, name, sizeof(dev->name) - 1);
    dev->id.bustype = bustype;
    dev->id.vendor = vendor;
    dev->id.product = product;
    dev->id.version = 1;
    lock_init(&dev->lock);
    if (!evdev_major_registered) {
        chardev_register(EVDEV_MAJOR, &evdev_ops);
        evdev_major_registered = 1;
    }
    devfs_add_char(devnode, EVDEV_MAJOR, dev->minor);
    return dev;
}

void evdev_enable_key(struct evdev_dev *dev, uint16_t code) {
    if (dev != NULL && code <= KEY_MAX) {
        evdev_bit_set(dev->key_cap, code);
    }
}

void evdev_enable_rel(struct evdev_dev *dev, uint16_t code) {
    if (dev != NULL && code <= REL_MAX) {
        evdev_bit_set(dev->rel_cap, code);
    }
}

static struct evdev_dev *evdev_by_minor(uint16_t minor) {
    for (int i = 0; i < EVDEV_MAX_DEVICES; i++) {
        if (evdev_devs[i].used && evdev_devs[i].minor == minor) {
            return &evdev_devs[i];
        }
    }
    return NULL;
}

static uint32_t evdev_avail(struct evdev_dev *dev, struct evdev_client *client) {
    return dev->head - client->next;
}

static int evdev_open(struct FILE *file, uint32_t devnum) {
    struct evdev_dev *dev = evdev_by_minor((uint16_t)(devnum & 0xffu));
    if (dev == NULL) {
        return -1;
    }
    if (dev->grab != NULL && dev->grab != file) {
        return -1;
    }
    struct evdev_client *client = kmalloc(sizeof(*client));
    if (client == NULL) {
        return -1;
    }
    lock_acquire(&dev->lock);
    client->dev = dev;
    client->next = dev->head;
    client->link = NULL;
    file->dev_priv = client;
    lock_release(&dev->lock);
    return 0;
}

static void evdev_release(struct FILE *file) {
    struct evdev_client *client = (struct evdev_client *)file->dev_priv;
    if (client == NULL) {
        return;
    }
    struct evdev_dev *dev = client->dev;
    lock_acquire(&dev->lock);
    if (dev->grab == file) {
        dev->grab = NULL;
    }
    file->dev_priv = NULL;
    lock_release(&dev->lock);
    kfree(client);
}

static int32_t evdev_read(struct FILE *file, void *buf, int32_t count) {
    struct evdev_client *client = (struct evdev_client *)file->dev_priv;
    if (client == NULL || count < (int32_t)sizeof(struct input_event)) {
        return -22;
    }
    struct evdev_dev *dev = client->dev;
    uint32_t want = (uint32_t)count / sizeof(struct input_event);
    lock_acquire(&dev->lock);
    if (dev->head - client->next > EVDEV_RING) {
        client->next = dev->head - EVDEV_RING;
    }
    while (evdev_avail(dev, client) == 0) {
        if (file->fd_nonblock) {
            lock_release(&dev->lock);
            return -11;
        }
        dev->waiter = current;
        uint32_t bf = thread_block_prepare(TASK_BLOCKED);
        lock_release(&dev->lock);
        thread_block_commit(bf);
        lock_acquire(&dev->lock);
    }
    uint32_t avail = evdev_avail(dev, client);
    uint32_t n = want < avail ? want : avail;
    for (uint32_t i = 0; i < n; i++) {
        memcpy((uint8_t *)buf + i * sizeof(struct input_event),
               &dev->ring[(client->next + i) % EVDEV_RING], sizeof(struct input_event));
    }
    client->next += n;
    lock_release(&dev->lock);
    return (int32_t)(n * sizeof(struct input_event));
}

static uint32_t evdev_poll(struct FILE *file) {
    struct evdev_client *client = (struct evdev_client *)file->dev_priv;
    if (client == NULL) {
        return 0;
    }
    struct evdev_dev *dev = client->dev;
    uint32_t rv = CHARDEV_POLL_WRITABLE;
    lock_acquire(&dev->lock);
    if (evdev_avail(dev, client) > 0) {
        rv |= CHARDEV_POLL_READABLE;
    }
    lock_release(&dev->lock);
    return rv;
}

static int32_t evdev_ioctl_bits(struct evdev_dev *dev, uint32_t cmd, uint64_t arg) {
    uint32_t size = _IOC_SIZE(cmd);
    uint32_t nr = _IOC_NR(cmd);
    const uint8_t *src = NULL;
    uint32_t src_len = 0;
    if (nr == 0x20u + EV_KEY) {
        src = dev->key_cap;
        src_len = sizeof(dev->key_cap);
    } else if (nr == 0x20u + EV_REL) {
        src = dev->rel_cap;
        src_len = sizeof(dev->rel_cap);
    } else {
        return 0;
    }
    uint32_t n = size < src_len ? size : src_len;
    memset((void *)(uintptr_t)arg, 0, size);
    memcpy((void *)(uintptr_t)arg, src, n);
    return 0;
}

static int32_t evdev_ioctl_version(uint64_t arg) {
    int32_t ver = EV_VERSION;
    memcpy((void *)(uintptr_t)arg, &ver, sizeof(ver));
    return 0;
}

static int32_t evdev_ioctl_id(const struct evdev_dev *dev, uint64_t arg) {
    memcpy((void *)(uintptr_t)arg, &dev->id, sizeof(dev->id));
    return 0;
}

static int32_t evdev_ioctl_name(const struct evdev_dev *dev, uint32_t cmd, uint64_t arg) {
    uint32_t size = _IOC_SIZE(cmd);
    uint32_t len = (uint32_t)strlen(dev->name) + 1;
    memcpy((void *)(uintptr_t)arg, dev->name, len < size ? len : size);
    return 0;
}

static int32_t evdev_ioctl_keystate(const struct evdev_dev *dev, uint32_t cmd, uint64_t arg) {
    uint32_t size = _IOC_SIZE(cmd);
    uint32_t n = size < sizeof(dev->key_state) ? size : sizeof(dev->key_state);
    memset((void *)(uintptr_t)arg, 0, size);
    memcpy((void *)(uintptr_t)arg, dev->key_state, n);
    return 0;
}

static int32_t evdev_ioctl_grab(struct FILE *file, struct evdev_dev *dev, uint64_t arg) {
    int32_t grab = 0;
    memcpy(&grab, (const void *)(uintptr_t)arg, sizeof(grab));
    lock_acquire(&dev->lock);
    if (grab) {
        if (dev->grab != NULL && dev->grab != file) {
            lock_release(&dev->lock);
            return -16;
        }
        dev->grab = file;
    } else if (dev->grab == file) {
        dev->grab = NULL;
    }
    lock_release(&dev->lock);
    return 0;
}

static int32_t evdev_ioctl(struct FILE *file, uint32_t cmd, uint64_t arg) {
    struct evdev_client *client = (struct evdev_client *)file->dev_priv;
    if (client == NULL) {
        return -25;
    }
    struct evdev_dev *dev = client->dev;
    uint32_t dir = _IOC_DIR(cmd);
    uint32_t nr = _IOC_NR(cmd);

    if (cmd == EVIOCGVERSION) {
        return evdev_ioctl_version(arg);
    }
    if (cmd == EVIOCGID) {
        return evdev_ioctl_id(dev, arg);
    }
    if (dir == _IOC_READ && nr >= 0x20u) {
        return evdev_ioctl_bits(dev, cmd, arg);
    }
    if (dir == _IOC_READ && nr == 0x06u) {
        return evdev_ioctl_name(dev, cmd, arg);
    }
    if (dir == _IOC_READ && nr == 0x18u) {
        return evdev_ioctl_keystate(dev, cmd, arg);
    }
    if (cmd == EVIOCGRAB) {
        return evdev_ioctl_grab(file, dev, arg);
    }
    return -25;
}

