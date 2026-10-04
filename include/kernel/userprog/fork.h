#ifndef FORK_H
#define FORK_H
#include "kernel/asm/stub.h"
#include "kernel/sched/thread.h"
#include <stdint.h>
pid_t sys_fork(struct ARCH_REGS *r);
int copy_user_space(struct TASK *parent, struct TASK *child);
#endif
