#include "arch/x86_64/syscall/init.h"
#include "arch/x86_64/cpu/gdt.h"
#include "arch/x86_64/msr.h"

#define MSR_EFER 0xC0000080ull
#define MSR_STAR 0xC0000081ull
#define MSR_LSTAR 0xC0000082ull
#define MSR_FMASK 0xC0000084ull
#define EFER_SCE (1ull << 0)

void arch_syscall_init(void) {
    extern void syscall_entry(void);
    uint64_t star = ((uint64_t)SELECTOR_USER64_CODE << 48) | ((uint64_t)SELECTOR_KERNEL_CODE << 32);
    asm_wrmsr(MSR_STAR, star);
    asm_wrmsr(MSR_LSTAR, (uint64_t)(uintptr_t)syscall_entry);
    asm_wrmsr(MSR_FMASK, 0x5700);
    asm_wrmsr(MSR_EFER, asm_rdmsr(MSR_EFER) | EFER_SCE);
}
