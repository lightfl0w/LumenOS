#ifndef KERNEL_SYSCALL_ARGS_H
#define KERNEL_SYSCALL_ARGS_H

#include "arch/syscall/args.h"

#define SC_MAX_ARGS 6

#define SC_A(i) arch_sc_arg(r, (i))
#define SC_NR arch_sc_nr(r)
#define SC_RET(v) arch_sc_set_ret(r, (uint64_t)(v))

#endif
