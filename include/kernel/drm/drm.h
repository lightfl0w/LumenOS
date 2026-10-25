#ifndef KERNEL_DRM_DRM_H
#define KERNEL_DRM_DRM_H

#include <stdint.h>

struct FILE;
struct CHARDEV_OPS;

const struct CHARDEV_OPS *drm_chardev_ops(void);

int drm_fd_mmap(int32_t fd, uint32_t base, uint32_t pages, uint32_t prot, uint64_t offset);

#endif
