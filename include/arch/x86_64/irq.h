#ifndef ARCH_X86_IRQ_H
#define ARCH_X86_IRQ_H

#include <stdint.h>

extern uint64_t asm_save_eflags(void);
extern void asm_restore_eflags(uint64_t eflags);

#define IRQ_TIMER    0u
#define IRQ_KEYBOARD 1u
#define IRQ_CASCADE  2u
#define IRQ_IDE      14u
#define IRQ_MOUSE    12u

#define IRQ_VECTOR_BASE 0x20u

struct IRQ_HANDLER {
    uint8_t irq;
    void (*handler)(uint8_t vector);
    const char *name;
};

extern const struct IRQ_HANDLER __irq_handlers_start[];
extern const struct IRQ_HANDLER __irq_handlers_end[];

#define IRQ_REGISTER(irq_line, fn, name_str)                                   \
    static const struct IRQ_HANDLER __irq_##fn                                 \
        __attribute__((used, section(".irq_handlers"))) = {                    \
            .irq = (uint8_t)(irq_line), .handler = (fn), .name = (name_str) }

const struct IRQ_HANDLER *irq_handler_lookup(uint8_t irq);

#endif
