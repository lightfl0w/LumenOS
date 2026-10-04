#ifndef ARCH_SMP_H
#define ARCH_SMP_H

#if defined(CONFIG_ARCH_X86_64)
#include "arch/x86_64/cpu/smp.h"
#elif defined(CONFIG_ARCH_AARCH64)
#error "aarch64 SMP not implemented"
#endif

#endif
