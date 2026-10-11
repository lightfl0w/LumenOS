#ifndef LIB_SYNC_SPIN_H
#define LIB_SYNC_SPIN_H

#include <stdint.h>
#include "arch/x86_64/cpu.h"

struct LIB_SPINLOCK {
    volatile uint32_t locked;
};

static inline void lib_spin_init(struct LIB_SPINLOCK *s) {
    s->locked = 0;
}

static inline void lib_spin_acquire(struct LIB_SPINLOCK *s) {
    while (cpu_xchg32(&s->locked, 1) != 0) {
        cpu_pause();
    }
}

static inline void lib_spin_release(struct LIB_SPINLOCK *s) {
    s->locked = 0;
}

#endif
