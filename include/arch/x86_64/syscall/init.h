#ifndef ARCH_X86_64_SYSCALL_INIT_H
#define ARCH_X86_64_SYSCALL_INIT_H

void arch_syscall_init(void);

extern void syscall_0x80(void);
extern void syscall_entry(void);

#endif
