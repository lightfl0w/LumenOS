#ifndef ARCH_X86_CPU_H
#define ARCH_X86_CPU_H
#include <stdint.h>
static inline void cpu_cli(void) {
    __asm__ volatile("cli" ::: "memory");
}
static inline void cpu_sti(void) {
    __asm__ volatile("sti" ::: "memory");
}
static inline void cpu_hlt(void) {
    __asm__ volatile("hlt");
}
static inline void cpu_pause(void) {
    __asm__ volatile("pause");
}
static inline uint64_t cpu_rdtsc(void) {
    uint32_t lo, hi;
    __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | lo;
}
static inline uint32_t cpu_eflags(void) {
    uint64_t v;
    __asm__ volatile("pushfq\n\tpopq %0" : "=r"(v));
    return (uint32_t)v;
}
static inline void cpu_set_eflags(uint32_t v) {
    __asm__ volatile("pushq %0\n\tpopfq" ::"r"((uint64_t)v) : "memory");
}
static inline uint32_t cpu_xchg32(volatile uint32_t *addr, uint32_t v) {
    uint32_t out;
    __asm__ volatile("xchg %0, %1" : "=r"(out), "+m"(*addr) : "0"(v) : "memory");
    return out;
}
static inline uint32_t cpu_cmpxchg32(volatile uint32_t *addr, uint32_t expect, uint32_t newv) {
    __asm__ volatile("lock cmpxchgl %2, %1" : "+a"(expect), "+m"(*addr) : "r"(newv) : "memory");
    return expect;
}
static inline uint32_t cpu_xadd32(volatile uint32_t *addr, uint32_t inc) {
    __asm__ volatile("lock xaddl %0, %1" : "+r"(inc), "+m"(*addr) : : "memory");
    return inc;
}
static inline uint32_t cpu_atomic_load32(const volatile uint32_t *addr) {
    uint32_t v;
    __asm__ volatile("movl %1, %0" : "=r"(v) : "m"(*addr) : "memory");
    return v;
}
static inline void cpu_outb(uint16_t port, uint8_t val) {
    __asm__ volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}
static inline uint8_t cpu_inb(uint16_t port) {
    uint8_t v;
    __asm__ volatile("inb %1, %0" : "=a"(v) : "Nd"(port));
    return v;
}
static inline void cpu_outw(uint16_t port, uint16_t val) {
    __asm__ volatile("outw %0, %1" : : "a"(val), "Nd"(port));
}
static inline uint16_t cpu_inw(uint16_t port) {
    uint16_t v;
    __asm__ volatile("inw %1, %0" : "=a"(v) : "Nd"(port));
    return v;
}
static inline void cpu_outl(uint16_t port, uint32_t val) {
    __asm__ volatile("outl %0, %1" : : "a"(val), "Nd"(port));
}
static inline uint32_t cpu_inl(uint16_t port) {
    uint32_t v;
    __asm__ volatile("inl %1, %0" : "=a"(v) : "Nd"(port));
    return v;
}
static inline void cpu_ins(uint16_t port, void *buf, int count, int width) {
    if (width == 4)
        __asm__ volatile("rep insl" : "+D"(buf), "+c"(count) : "d"(port) : "memory");
    else if (width == 2)
        __asm__ volatile("rep insw" : "+D"(buf), "+c"(count) : "d"(port) : "memory");
    else
        __asm__ volatile("rep insb" : "+D"(buf), "+c"(count) : "d"(port) : "memory");
}
static inline void cpu_outs(uint16_t port, const void *buf, int count, int width) {
    if (width == 4)
        __asm__ volatile("rep outsl" : "+S"(buf), "+c"(count) : "d"(port) : "memory");
    else if (width == 2)
        __asm__ volatile("rep outsw" : "+S"(buf), "+c"(count) : "d"(port) : "memory");
    else
        __asm__ volatile("rep outsb" : "+S"(buf), "+c"(count) : "d"(port) : "memory");
}
static inline uint32_t arch_cpu_id(void) {
    uint32_t a = 1, d;
    __asm__ volatile("cpuid" : "+a"(a), "=d"(d) : : "ebx", "ecx");
    return d >> 24;
}
static inline int arch_cpu_has_nx(void) {
    uint32_t a = 0x80000001, d;
    __asm__ volatile("cpuid" : "+a"(a), "=d"(d) : : "ebx", "ecx");
    return (d & (1u << 20)) != 0;
}
static inline void arch_cpuid(uint32_t leaf, uint32_t sub, uint32_t *a, uint32_t *b, uint32_t *c,
                              uint32_t *d) {
    __asm__ volatile("cpuid" : "=a"(*a), "=b"(*b), "=c"(*c), "=d"(*d) : "a"(leaf), "c"(sub));
}
static inline int arch_hw_rand(uint64_t *out) {
    uint64_t v;
    uint8_t ok;
    __asm__ volatile("rdrand %0\n\tsetc %1" : "=r"(v), "=qm"(ok) : : "cc");
    if (!ok)
        return 0;
    *out = v;
    return 1;
}
static inline int arch_hw_seed(uint64_t *out) {
    uint64_t v;
    uint8_t ok;
    __asm__ volatile("rdseed %0\n\tsetc %1" : "=r"(v), "=qm"(ok) : : "cc");
    if (!ok)
        return 0;
    *out = v;
    return 1;
}
static inline void arch_fpu_init(void *template, uint32_t mxcsr) {
    __asm__ volatile("fninit\n\tldmxcsr (%0)\n\tfxsave (%1)" ::"r"(&mxcsr), "r"(template)
                     : "memory");
}
static inline void arch_fpu_save(void *buf) {
    __asm__ volatile("fxsave (%0)" ::"r"(buf) : "memory");
}
static inline void arch_fpu_restore(const void *buf) {
    __asm__ volatile("fxrstor (%0)" ::"r"(buf) : "memory");
}
#define ARCH_MSR_GS_BASE 0xC0000101ull
#define ARCH_MSR_KERNEL_GS_BASE 0xC0000102ull

static inline void arch_set_gs_base(uint64_t base) {
    uint32_t lo = (uint32_t)base, hi = (uint32_t)(base >> 32);
    __asm__ volatile("wrmsr" ::"a"(lo), "d"(hi), "c"(0xC0000101u) : "memory");
}
static inline void arch_set_kernel_gs_base(uint64_t base) {
    uint32_t lo = (uint32_t)base, hi = (uint32_t)(base >> 32);
    __asm__ volatile("wrmsr" ::"a"(lo), "d"(hi), "c"(0xC0000102u) : "memory");
}
static inline uint64_t arch_get_fs_base(void) {
    uint32_t lo, hi;
    __asm__ volatile("rdmsr" : "=a"(lo), "=d"(hi) : "c"(0xC0000100u));
    return ((uint64_t)hi << 32) | lo;
}
static inline uint64_t arch_get_kernel_gs_base(void) {
    uint32_t lo, hi;
    __asm__ volatile("rdmsr" : "=a"(lo), "=d"(hi) : "c"(0xC0000102u));
    return ((uint64_t)hi << 32) | lo;
}
static inline void arch_set_fs_base(uint64_t base) {
    uint32_t lo = (uint32_t)base, hi = (uint32_t)(base >> 32);
    __asm__ volatile("wrmsr" ::"a"(lo), "d"(hi), "c"(0xC0000100u) : "memory");
}
static inline void arch_load_gs(uint16_t sel) {
    __asm__ volatile("movw %0, %%gs" ::"r"(sel) : "memory");
}
#endif
