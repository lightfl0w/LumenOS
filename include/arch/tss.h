#ifndef ARCH_TSS_H
#define ARCH_TSS_H

#if defined(CONFIG_ARCH_X86_64)
#include "arch/x86_64/cpu/tss.h"
#elif defined(CONFIG_ARCH_AARCH64)
#error "aarch64 TSS not implemented"
#endif

#endif
