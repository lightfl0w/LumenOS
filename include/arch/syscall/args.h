#ifndef ARCH_SYSCALL_ARGS_H
#define ARCH_SYSCALL_ARGS_H

#if defined(CONFIG_ARCH_X86_64)
#include "arch/x86_64/syscall/args.h"
#elif defined(CONFIG_ARCH_AARCH64)
#error "aarch64 syscall args not implemented"
#endif

#endif
