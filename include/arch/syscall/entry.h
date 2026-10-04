#ifndef ARCH_SYSCALL_ENTRY_H
#define ARCH_SYSCALL_ENTRY_H

#if defined(CONFIG_ARCH_X86_64)
#include "arch/x86_64/syscall/entry.h"
#elif defined(CONFIG_ARCH_AARCH64)
#error "aarch64 syscall entry not implemented"
#endif

#endif
