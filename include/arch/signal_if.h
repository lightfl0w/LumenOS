#ifndef ARCH_SIGNAL_IF_H
#define ARCH_SIGNAL_IF_H

#include <stdint.h>

struct TASK;

#define SIGSEGV 11

void signal_terminate(struct TASK *t, int sig);

#endif
