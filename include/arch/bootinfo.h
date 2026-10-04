#ifndef ARCH_BOOTINFO_H
#define ARCH_BOOTINFO_H

#if defined(CONFIG_ARCH_X86_64)
#include "arch/x86_64/boot/mb2.h"
#elif defined(CONFIG_ARCH_AARCH64)
#error "aarch64 boot info not implemented"
#endif

#endif
