#ifndef ARCH_SYSCALL_INIT_H
#define ARCH_SYSCALL_INIT_H

#if defined(CONFIG_ARCH_X86_64)
#include "arch/x86_64/syscall/init.h"
#elif defined(CONFIG_ARCH_AARCH64)
#error "aarch64 syscall init not implemented"
#endif

#endif
