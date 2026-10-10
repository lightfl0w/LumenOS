#ifndef PROCESS_H
#define PROCESS_H

#include "kernel/sched/thread.h"
#include "arch/process_if.h"
#include <stdint.h>

void start_process(void *arg);
void page_dir_activate(struct TASK *task);
void process_activate(struct TASK *task);
uint32_t *create_page_dir(void);
void free_user_space(struct TASK *t, uint32_t pml4_phys);
void task_release_space(struct TASK *t);
void space_ref(uint32_t pml4);
void space_detach_others(struct TASK *owner);
void create_user_vaddr_bitmap(struct TASK *user_prog);
void process_execute(char *path, char *name);

#endif
