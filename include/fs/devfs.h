#ifndef FS_DEVFS_H
#define FS_DEVFS_H
#include <stdint.h>

struct FILE;
struct FS_STAT;

int devfs_add_char(const char *path, uint32_t major, uint32_t minor);

int devfs_match(const char *path);
int devfs_open(const char *path, uint8_t flags);
int devfs_stat(const char *path, struct FS_STAT *buf);
uint32_t devfs_read(struct FILE *file, void *buf, uint32_t count);
uint32_t devfs_write(struct FILE *file, const void *buf, uint32_t count);
int32_t devfs_lseek(struct FILE *file, int32_t offset, uint8_t whence);
int devfs_fstat(struct FILE *file, struct FS_STAT *buf);
int32_t devfs_getdents64(struct FILE *file, void *dirp, uint32_t count);
void devfs_close(struct FILE *file);

#endif
