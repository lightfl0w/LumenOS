#include "arch/x86_64/syscall/entry.h"
#include "arch/x86_64/cpu/gdt.h"
#include "arch/x86_64/mmu.h"
#include "arch/x86_64/msr.h"

#define MSR_EFER 0xC0000080ull
#define EFER_NXE (1ull << 11)

int arch_enable_nx(void) {
    uint64_t efer = asm_rdmsr(MSR_EFER);
    asm_wrmsr(MSR_EFER, efer | EFER_NXE);
    return (asm_rdmsr(MSR_EFER) & EFER_NXE) != 0;
}

void *arch_thread_entry(void) {
    return (void *)intr_exit;
}

void arch_user_enter(struct ARCH_REGS *regs) {
    __asm__ volatile("mov %0, %%ds; mov %0, %%es; mov %0, %%fs;" ::"r"((uint16_t)SELECTOR_U_DATA)
                     : "memory");
    __asm__ volatile("mov %0, %%rsp; jmp intr_exit" : : "r"(regs) : "memory");
}
