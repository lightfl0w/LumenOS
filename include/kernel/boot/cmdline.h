#ifndef KERNEL_BOOT_CMDLINE_H
#define KERNEL_BOOT_CMDLINE_H

#include <stddef.h>
#include <stdint.h>

#define CMDLINE_MAX 256

void cmdline_init(const char *raw);
int cmdline_has_flag(const char *name);
int cmdline_get_value(const char *name, char *out, size_t out_size);
size_t cmdline_count(void);

#endif
