#ifndef ARCH_X86_64_SYSCALL_ENTRY_H
#define ARCH_X86_64_SYSCALL_ENTRY_H

#include "arch/regs.h"

extern void intr_exit(void);

void arch_user_enter(struct ARCH_REGS *regs);
void *arch_thread_entry(void);

#endif
