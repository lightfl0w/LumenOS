#include "arch/x86_64/irq/interrupt/idt.h"

#include "arch/x86_64/cpu/gdt.h"
#include "arch/x86_64/irq/apic.h"
#include "arch/x86_64/syscall/init.h"
#include "arch/asm/stub.h"
#include "arch/asm_func.h"

struct IDT_ENTRY idt[256];

struct IDTR {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed)) idtr0;

static void idt_set(uint8_t vec, void (*handler)(void), uint8_t type) {
    uint64_t addr = (uint64_t)handler;

    idt[vec].offset_low = (uint16_t)(addr & 0xFFFF);
    idt[vec].selector = SELECTOR_KERNEL_CODE;
    idt[vec].ist = 0;
    idt[vec].type_attr = type;
    idt[vec].offset_mid = (uint16_t)((addr >> 16) & 0xFFFF);
    idt[vec].offset_high = (uint32_t)((addr >> 32) & 0xFFFFFFFF);
    idt[vec].reserved = 0;
}

void idt_load_idtr(void) {
    idtr0.limit = (uint16_t)(sizeof(idt) - 1);
    idtr0.base = (uint64_t)idt;
    __asm__ volatile("lidt %0" : : "m"(idtr0) : "memory");
}

void idt_init(void) {
    for (int i = 0; i < 256; ++i) {
        idt_set((uint8_t)i, default_handler, IDT_TYPE_INT_GATE64);
    }

    idt_set(0, isr0, IDT_TYPE_INT_GATE64);
    idt_set(1, isr1, IDT_TYPE_INT_GATE64);
    idt_set(2, isr2, IDT_TYPE_INT_GATE64);
    idt_set(3, isr3, IDT_TYPE_INT_GATE64);
    idt_set(4, isr4, IDT_TYPE_INT_GATE64);
    idt_set(5, isr5, IDT_TYPE_INT_GATE64);
    idt_set(6, isr6, IDT_TYPE_INT_GATE64);
    idt_set(7, isr7, IDT_TYPE_INT_GATE64);
    idt_set(8, isr8, IDT_TYPE_INT_GATE64);
    idt_set(9, isr9, IDT_TYPE_INT_GATE64);
    idt_set(10, isr10, IDT_TYPE_INT_GATE64);
    idt_set(11, isr11, IDT_TYPE_INT_GATE64);
    idt_set(12, isr12, IDT_TYPE_INT_GATE64);
    idt_set(13, isr13, IDT_TYPE_INT_GATE64);
    idt_set(14, isr14, IDT_TYPE_INT_GATE64);
    idt_set(15, isr15, IDT_TYPE_INT_GATE64);
    idt_set(16, isr16, IDT_TYPE_INT_GATE64);
    idt_set(17, isr17, IDT_TYPE_INT_GATE64);
    idt_set(18, isr18, IDT_TYPE_INT_GATE64);
    idt_set(19, isr19, IDT_TYPE_INT_GATE64);
    idt_set(20, isr20, IDT_TYPE_INT_GATE64);
    idt_set(21, isr21, IDT_TYPE_INT_GATE64);
    idt_set(22, isr22, IDT_TYPE_INT_GATE64);
    idt_set(23, isr23, IDT_TYPE_INT_GATE64);
    idt_set(24, isr24, IDT_TYPE_INT_GATE64);
    idt_set(25, isr25, IDT_TYPE_INT_GATE64);
    idt_set(26, isr26, IDT_TYPE_INT_GATE64);
    idt_set(27, isr27, IDT_TYPE_INT_GATE64);
    idt_set(28, isr28, IDT_TYPE_INT_GATE64);
    idt_set(29, isr29, IDT_TYPE_INT_GATE64);
    idt_set(30, isr30, IDT_TYPE_INT_GATE64);
    idt_set(31, isr31, IDT_TYPE_INT_GATE64);

    idt_set(32, irq0, IDT_TYPE_INT_GATE64);
    idt_set(33, irq1, IDT_TYPE_INT_GATE64);
    idt_set(34, irq2, IDT_TYPE_INT_GATE64);
    idt_set(35, irq3, IDT_TYPE_INT_GATE64);
    idt_set(36, irq4, IDT_TYPE_INT_GATE64);
    idt_set(37, irq5, IDT_TYPE_INT_GATE64);
    idt_set(38, irq6, IDT_TYPE_INT_GATE64);
    idt_set(39, irq7, IDT_TYPE_INT_GATE64);
    idt_set(40, irq8, IDT_TYPE_INT_GATE64);
    idt_set(41, irq9, IDT_TYPE_INT_GATE64);
    idt_set(42, irq10, IDT_TYPE_INT_GATE64);
    idt_set(43, irq11, IDT_TYPE_INT_GATE64);
    idt_set(44, irq12, IDT_TYPE_INT_GATE64);
    idt_set(45, irq13, IDT_TYPE_INT_GATE64);
    idt_set(46, irq14, IDT_TYPE_INT_GATE64);
    idt_set(47, irq15, IDT_TYPE_INT_GATE64);

    idt_set(0x80, syscall_0x80, IDT_TYPE_TRAP_GATE3);
    idt_set(IPI_VECTOR_RESCHED, ipi_resched, IDT_TYPE_INT_GATE64);
    idt_set(LAPIC_CALIB_VECTOR, ipi_calib, IDT_TYPE_INT_GATE64);

    idt_load_idtr();

    idt_syscall_init();
}

void idt_syscall_init(void) {
    arch_syscall_init();
}
