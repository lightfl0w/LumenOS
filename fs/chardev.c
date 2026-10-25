#include "fs/chardev.h"
#include "fs/file.h"
#include "fs/fs.h"
#include "drivers/char/serial/pty.h"
#include "drivers/char/serial/tty.h"
#include "drivers/driver_ops.h"
#include "lib/rand/rand.h"
#include "lib/string/str.h"
#include <stdint.h>

#define CHARDEV_MAX_MAJORS 256

#define DEV_ZERO 0x0105u
#define DEV_RANDOM 0x0108u
#define DEV_URANDOM 0x0109u
#define DEV_MEM 0x0101u

static const struct CHARDEV_OPS *g_chardevs[CHARDEV_MAX_MAJORS];

int chardev_register(uint32_t major, const struct CHARDEV_OPS *ops) {
    if (major >= CHARDEV_MAX_MAJORS || ops == NULL) {
        return -1;
    }
    g_chardevs[major] = ops;
    return 0;
}

const struct CHARDEV_OPS *chardev_lookup(uint32_t major) {
    if (major >= CHARDEV_MAX_MAJORS) {
        return NULL;
    }
    return g_chardevs[major];
}

uint32_t file_chardev_dev(const struct FILE *file) {
    if (file == NULL) {
        return 0;
    }
    if (file->dev_major != 0) {
        return ((uint32_t)file->dev_major << 8) | file->dev_minor;
    }
    if (file->fd_inode != NULL && fs_is_chardev(file->fd_inode)) {
        return fs_chardev_dev(file->fd_inode);
    }
    return 0;
}

const struct CHARDEV_OPS *file_chardev_ops(const struct FILE *file) {
    uint32_t dev = file_chardev_dev(file);
    if (dev == 0) {
        return NULL;
    }
    return chardev_lookup(dev >> 8);
}

static int32_t mem_read(struct FILE *file, void *buf, int32_t count) {
    uint32_t dev = file_chardev_dev(file);
    uint32_t n = (uint32_t)count;
    if (dev == DEV_ZERO) {
        memset(buf, 0, n);
        return count;
    }
    if (dev == DEV_RANDOM || dev == DEV_URANDOM || dev == DEV_MEM) {
        rand_bytes(buf, n);
        return count;
    }
    return 0;
}

static int32_t mem_write(struct FILE *file, const void *buf, int32_t count) {
    (void)file;
    (void)buf;
    return count;
}

static const struct CHARDEV_OPS mem_ops = {NULL, NULL, mem_read, mem_write, NULL, NULL};

static int32_t tty_read(struct FILE *file, void *buf, int32_t count) {
    (void)file;
    return (int32_t)TTY.read((char *)buf, (uint32_t)count);
}

static int32_t tty_write(struct FILE *file, const void *buf, int32_t count) {
    (void)file;
    return (int32_t)TTY.write((const char *)buf, (uint32_t)count);
}

static uint32_t tty_poll(struct FILE *file) {
    (void)file;
    return CHARDEV_POLL_READABLE | CHARDEV_POLL_WRITABLE;
}

static const struct CHARDEV_OPS tty_ops = {NULL, NULL, tty_read, tty_write, NULL, tty_poll};

static int pty_open(struct FILE *file, uint32_t dev) {
    return pty_chardev_open(file, dev);
}

static void pty_release(struct FILE *file) {
    pty_chardev_close(file);
}

static int32_t pty_read(struct FILE *file, void *buf, int32_t count) {
    return pty_chardev_read(file, buf, (uint32_t)count);
}

static int32_t pty_write(struct FILE *file, const void *buf, int32_t count) {
    return pty_chardev_write(file, buf, (uint32_t)count);
}

static int32_t pty_ioctl(struct FILE *file, uint32_t cmd, uint64_t arg) {
    return pty_chardev_ioctl(file, cmd, arg);
}

static const struct CHARDEV_OPS pty_ops = {pty_open,  pty_release, pty_read,
                                            pty_write, pty_ioctl,   NULL};

static int chardev_core_init(void) {
    chardev_register(1, &mem_ops);
    chardev_register(5, &tty_ops);
    chardev_register(PTY_MASTER_MAJOR, &pty_ops);
    chardev_register(PTY_SLAVE_MAJOR, &pty_ops);
    return 0;
}

DRIVER_REGISTER("chardev", 10, chardev_core_init);
