#ifndef ARCH_X86_MMU_H
#define ARCH_X86_MMU_H

#include "arch/x86_64/mm/paging.h"
#include <stdint.h>

extern uint64_t asm_read_cr0(void);
extern void asm_write_cr0(uint64_t cr0);
extern uint64_t asm_read_cr2(void);
extern uint64_t asm_read_cr3(void);
extern void asm_write_cr3(uint64_t cr3);
extern uint64_t asm_read_cr4(void);
extern void asm_write_cr4(uint64_t cr4);

#define ARCH_MSR_EFER 0xC0000080u

int arch_enable_nx(void);

static inline uint64_t arch_read_efer(void) {
    uint32_t lo, hi;
    __asm__ volatile("rdmsr" : "=a"(lo), "=d"(hi) : "c"(ARCH_MSR_EFER));
    return ((uint64_t)hi << 32) | lo;
}

static inline void arch_tlb_flush(uint64_t vaddr) {
    __asm__ volatile("invlpg (%0)" : : "r"(vaddr) : "memory");
}

#endif
