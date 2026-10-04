#include "kernel/sched/percpu.h"
#include "arch/seg.h"
#include "kernel/asm_func.h"
#include "lib/string/str.h"

void percpu_init(void) {
    for (uint32_t i = 0; i < NR_CPU; i++) {
        memset((void *)(uintptr_t)(PER_CPU_BASE + i * PERCPU_BLOCK), 0, PERCPU_BLOCK);
    }
    arch_set_gs_base(PER_CPU_BASE);
    arch_set_kernel_gs_base(PER_CPU_BASE);
    arch_load_gs(ARCH_SEG_PER_CPU);
    set_cpu_id(0);
    set_current((struct TASK *)0);
}