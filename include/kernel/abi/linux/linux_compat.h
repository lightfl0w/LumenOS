#ifndef COREZ_LINUX_COMPAT_H
#define COREZ_LINUX_COMPAT_H

#include "arch/asm/stub.h"
#include <stdint.h>

#define COMPAT_SYSCALL_BASE 0x50000

#include "uapi/linux_abi.h"

int64_t linux_compat_handler(struct ARCH_REGS *r);

#endif
