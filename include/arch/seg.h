#ifndef ARCH_SEG_H
#define ARCH_SEG_H

#if defined(CONFIG_ARCH_X86_64)
#include "arch/x86_64/syscall/seg.h"
#elif defined(CONFIG_ARCH_AARCH64)
#error "aarch64 segment selectors not implemented"
#endif

#endif
