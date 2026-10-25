#ifndef FS_CHARDEV_H
#define FS_CHARDEV_H
#include <stdint.h>

struct FILE;

#define CHARDEV_POLL_READABLE 1u
#define CHARDEV_POLL_WRITABLE 2u

struct CHARDEV_OPS {
    int (*open)(struct FILE *file, uint32_t dev);
    void (*release)(struct FILE *file);
    int32_t (*read)(struct FILE *file, void *buf, int32_t count);
    int32_t (*write)(struct FILE *file, const void *buf, int32_t count);
    int32_t (*ioctl)(struct FILE *file, uint32_t cmd, uint64_t arg);
    uint32_t (*poll)(struct FILE *file);
};

int chardev_register(uint32_t major, const struct CHARDEV_OPS *ops);
const struct CHARDEV_OPS *chardev_lookup(uint32_t major);
uint32_t file_chardev_dev(const struct FILE *file);
const struct CHARDEV_OPS *file_chardev_ops(const struct FILE *file);

#endif
