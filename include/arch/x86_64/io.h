#ifndef ARCH_X86_IO_H
#define ARCH_X86_IO_H

#include "arch/x86_64/cpu.h"
#include <stdint.h>

extern void insw(uint16_t port, void *buf, int words);
extern void outsw(uint16_t port, const void *buf, int words);

#endif
