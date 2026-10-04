#ifndef ARCH_X86_64_SYSCALL_SEG_H
#define ARCH_X86_64_SYSCALL_SEG_H

#include "arch/x86_64/cpu/gdt.h"
#include <stdint.h>

#define ARCH_SEG_KERNEL_CODE SELECTOR_KERNEL_CODE
#define ARCH_SEG_KERNEL_DATA SELECTOR_KERNEL_DATA
#define ARCH_SEG_USER_DATA SELECTOR_U_DATA
#define ARCH_SEG_USER_CODE32 SELECTOR_U_CODE
#define ARCH_SEG_USER_CODE64 SELECTOR_USER64_CODE
#define ARCH_SEG_TLS SELECTOR_TLS
#define ARCH_SEG_PER_CPU SELECTOR_PER_CPU

static inline int arch_cs_is_user64(uint64_t cs) {
    return cs == SELECTOR_USER64_CODE;
}
static inline int arch_cs_is_user(uint64_t cs) {
    return cs == SELECTOR_USER64_CODE || cs == SELECTOR_U_CODE;
}
static inline int arch_ss_is_user(uint64_t ss) {
    return ss == SELECTOR_U_DATA;
}

#endif
