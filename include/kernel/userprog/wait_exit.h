#ifndef WAIT_EXIT_H
#define WAIT_EXIT_H

#include "kernel/sched/thread.h"
#include <stdint.h>

pid_t sys_wait(int32_t *status);
pid_t sys_wait4(int32_t pid, int32_t *status, uint32_t options);

#define WAIT_WNOHANG 1u
void sys_exit(int32_t status);

void proc_exit(struct TASK *t, int status);

#ifndef __ASSEMBLER__
struct LINUX_SIGINFO;
int sys_waitid(int idtype, int32_t id, struct LINUX_SIGINFO *info, uint32_t options);
#endif

#endif
void kill_orphan_children(int32_t parent_pid);
