#ifndef ARCH_PERCPU_H
#define ARCH_PERCPU_H

#if defined(CONFIG_ARCH_X86_64)
#include "arch/x86_64/percpu.h"
#elif defined(CONFIG_ARCH_AARCH64)
#error "aarch64 per-CPU area not yet implemented: arch/arm64/percpu.h"
#endif

#endif
