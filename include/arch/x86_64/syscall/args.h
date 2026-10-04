#ifndef ARCH_X86_64_SYSCALL_ARGS_H
#define ARCH_X86_64_SYSCALL_ARGS_H

#include "arch/regs.h"
#include <stdint.h>

static inline uint64_t arch_sc_arg(const struct ARCH_REGS *r, int i) {
    switch (i) {
    case 0:
        return r->rbx;
    case 1:
        return r->rcx;
    case 2:
        return r->rdx;
    case 3:
        return r->rsi;
    case 4:
        return r->rdi;
    case 5:
        return r->r10;
    default:
        return 0;
    }
}
static inline uint64_t arch_sc_nr(const struct ARCH_REGS *r) {
    return r->rax;
}
static inline void arch_sc_set_ret(struct ARCH_REGS *r, uint64_t v) {
    r->rax = v;
}
static inline int arch_sc_from_kernel(const struct ARCH_REGS *r) {
    return (r->cs & 3) == 0;
}

static inline void arch_compat_normalize(struct ARCH_REGS *r) {
    r->rdi = r->rbx;
    r->rsi = r->rcx;
    r->r10 = r->esi;
    r->r8 = r->edi;
    r->r9 = r->ebp;
}

#endif
