#include "arch/x86_64/irq/interrupt/interrupt.h"
#include "arch/x86_64/irq/apic.h"
#include "arch/x86_64/irq/pic.h"
#include "lib/printf/printf.h"
#include "lib/console/console.h"
#include "arch/x86_64/irq.h"
#include "arch/asm/stub.h"
#include "arch/asm_func.h"
#include "arch/percpu.h"
#include "kernel/sched/thread.h"
#include "arch/signal_if.h"
#include "arch/time/pit.h"
#include "arch/process_if.h"
#include "mm/access.h"
#include "mm/pool.h"
volatile uint32_t tick = 0;
volatile uint32_t lapic_calib_count = 0;
static const char *exc_names[32] = {"Divide Error",
                                    "Debug",
                                    "Non-Maskable Interrupt",
                                    "Breakpoint",
                                    "Overflow",
                                    "BOUND Range Exceeded",
                                    "Invalid Opcode",
                                    "Device Not Available",
                                    "Double Fault",
                                    "Coprocessor Seg Overrun",
                                    "Invalid TSS",
                                    "Segment Not Present",
                                    "Stack-Segment Fault",
                                    "General Protection",
                                    "Page Fault",
                                    "(Reserved)",
                                    "x87 FP Error",
                                    "Alignment Check",
                                    "Machine Check",
                                    "SIMD FP Exception",
                                    "Virtualization Exception",
                                    "Control Protection",
                                    "(Reserved)",
                                    "(Reserved)",
                                    "(Reserved)",
                                    "(Reserved)",
                                    "(Reserved)",
                                    "(Reserved)",
                                    "(Reserved)",
                                    "(Reserved)",
                                    "(Reserved)",
                                    "(Reserved)"};
static uint32_t tlb_retry_rip;
static uint32_t tlb_retry_pid;
static int handle_cow_fault(uint32_t fault_addr, uint32_t error_code) {
    if (fault_addr < USER_EXEC64_FLOOR || fault_addr >= USER_STACK_TOP) {
        return 0;
    }
    if (!(error_code & 0x2)) {
        return 0;
    }
    if (current == 0 || current->pml4_phys == 0) {
        return 0;
    }
    uint64_t *pte = pte_ptr(fault_addr);
    if (!(*pte & 1) || !(*pte & COW_FLAG)) {
        return 0;
    }
    return page_cow_resolve(fault_addr, *pte);
}
static int handle_stack_grow(uint32_t fa, uint32_t err_code, uint32_t rsp) {
    struct TASK *cur = current;
    if (cur == NULL || cur->pml4_phys == 0 || (err_code & 1))
        return 0;
    if (fa >= USER_STACK_TOP || fa < USER_STACK_TOP - 0x800000u)
        return 0;
    if (rsp >= USER_STACK_TOP || rsp < USER_STACK_TOP - 0x800000u)
        return 0;
    if (fa < rsp && rsp - fa > 0x10000u)
        return 0;
    if (page_is_mapped(fa))
        return 0;
    uint32_t page = fa & ~0xfffu;
    if (get_a_page(page) == 0)
        return 0;
    if (page < cur->stack_bottom || cur->stack_bottom == 0)
        cur->stack_bottom = page;
    return 1;
}

static uint64_t read_cr2(void) {
    uint64_t cr2;
    __asm__ volatile("mov %%cr2, %0" : "=r"(cr2));
    return cr2;
}

static int handle_recoverable_fault(struct ARCH_REGS *r) {
    if (r->int_no != 14) {
        return 0;
    }
    uint64_t cr2 = read_cr2();
    if (handle_cow_fault((uint32_t)cr2, r->err_code)) {
        return 1;
    }
    return (r->cs & 3) == 3 &&
           handle_stack_grow((uint32_t)cr2, (uint32_t)r->err_code, (uint32_t)r->user_rsp);
}

static int report_kernel_touched_user(struct ARCH_REGS *r) {
    if (r->int_no != 14 || current == NULL || current->pml4_phys == 0) {
        return 0;
    }
    uint32_t fa = (uint32_t)read_cr2();
    if (fa < USER_EXEC64_FLOOR || fa >= USER_STACK_TOP) {
        return 0;
    }
    set_text_color(14);
    kprintf("[pf] kernel touched unmapped user addr 0x%x, killing pid %d (%s) "
            "eip=0x%x err=%x mapped=%d\n",
            fa, current->pid, current->name, (uint32_t)r->eip, (uint32_t)r->err_code,
            page_is_mapped(fa));
    signal_terminate(current, SIGSEGV);
    return 1;
}

static int retry_after_remap(struct ARCH_REGS *r, uint64_t cr2) {
    if (current == NULL) {
        return 0;
    }
    if ((current->signal_pending | current->signal_mask) & (1u << SIGSEGV)) {
        signal_terminate(current, SIGSEGV);
        return 1;
    }
    int writable = !(r->err_code & 1);
    int present = !(r->err_code & 0x2);
    if (cr2 >= USER_STACK_TOP || !writable || !present) {
        return 0;
    }
    uint64_t *rpte = pte_ptr((uint32_t)cr2);
    if (!(*rpte & 1) || !(*rpte & 4)) {
        return 0;
    }
    if (tlb_retry_rip == (uint32_t)r->eip && tlb_retry_pid == current->pid) {
        return 0;
    }
    tlb_retry_rip = (uint32_t)r->eip;
    tlb_retry_pid = current->pid;
    if (user_remap_page(cr2 & ~0xfffu) == 0) {
        return 0;
    }
    uint64_t cr3v = current->pml4_phys;
    __asm__ volatile("mov %0, %%cr3" : : "r"(cr3v) : "memory");
    return 1;
}

static int deliver_user_signal(struct ARCH_REGS *r) {
    if ((r->cs & 3) != 3) {
        return 0;
    }
    int sig = exception_to_signal((int)r->int_no);
    if (sig <= 0) {
        return 0;
    }
    uint64_t cr2 = 0;
    if (sig == SIGSEGV) {
        cr2 = read_cr2();
        if (retry_after_remap(r, cr2)) {
            return 1;
        }
    }
    current->sig_fault_addr = (uint32_t)cr2;
    current->signal_pending |= (1u << sig);
    check_pending_signals(r);
    return 1;
}

static void report_unhandled_user_exception(struct ARCH_REGS *r) {
    set_text_color(12);
    kprintf("\n*** UNHANDLED USER EXCEPTION ***\n");
    if (r->int_no < 32) {
        kprintf("  %s (vector %d)  eip = 0x%x\n", exc_names[r->int_no], (int)r->int_no,
                r->eip);
    }
    signal_terminate(current, SIGSEGV);
}

static void dump_exception(struct ARCH_REGS *r, uint64_t cr2) {
    set_text_color(12);
    kprintf("\n*** EXCEPTION ***\n");
    if (r->int_no < 32) {
        kprintf("  %s (vector %d)\n", exc_names[r->int_no], (int)r->int_no);
        kprintf("  err_code = 0x%x\n", r->err_code);
    } else {
        kprintf("  Unregistered interrupt (vector %d)\n", (int)r->int_no);
    }
    kprintf("  eip = 0x%x  cs = 0x%x  eflags = 0x%x\n", r->eip, r->cs, r->eflags);
    kprintf("  eax = 0x%x  ebx = 0x%x  ecx = 0x%x  edx = 0x%x\n",
            r->eax, r->ebx, r->ecx, r->edx);
    kprintf("  esi = 0x%x  edi = 0x%x  ebp = 0x%x  rflags = 0x%x\n",
            r->esi, r->edi, r->ebp, r->rflags);
    kprintf("  cr2 (fault addr) = 0x%x\n", (uint32_t)cr2);
    kprintf("  efer = 0x%x (NXE=%d)  cr4 = 0x%x (PAE=%d SMEP=%d SMAP=%d)\n",
            (uint32_t)asm_rdmsr(0xC0000080u), (int)((asm_rdmsr(0xC0000080u) >> 11) & 1),
            (uint32_t)asm_read_cr4(), (int)((asm_read_cr4() >> 5) & 1),
            (int)((asm_read_cr4() >> 20) & 1), (int)((asm_read_cr4() >> 21) & 1));
    if (r->int_no == 14) {
        page_table_dump((uint32_t)cr2);
    }
    if (cr2 >= 0xC1000000 && cr2 < 0xC1400000) {
        kprintf("  (note: fault is inside kernel virtual pool "
                "0xC1000000..0xC1400000)\n");
    } else if (cr2 >= 0xE0000000) {
        kprintf("  (note: fault is inside VRAM region 0xE0000000+)\n");
    }
}

static void panic_halt(void) {
    static volatile int panicking;
    if (panicking) {
        asm_cli();
        for (;;)
            asm_hlt();
    }
    panicking = 1;
    struct TASK *cur = get_current();
    if (cur && cur->stack_magic != STACK_MAGIC) {
        kprintf("  task: pid=%d name=%s stack_magic=%#x CORRUPTED (want %#x)\n",
                cur->pid, cur->name, cur->stack_magic, (uint32_t)STACK_MAGIC);
    }
    unsigned long *rbp;
    int frame = 1;
    __asm__ volatile("mov %%rbp, %0" : "=r"(rbp));
    while (rbp) {
        kprintf("[#%d] 0x%x\n", frame++, *(rbp + 1));
        if ((unsigned long)rbp < 0x1000)
            break;
        rbp = (unsigned long *)*rbp;
    }
    asm_cli();
    for (;;)
        asm_hlt();
}

void isr_handler(struct ARCH_REGS *r) {
    if (handle_recoverable_fault(r))
        return;
    if (deliver_user_signal(r))
        return;
    if ((r->cs & 3) == 3)
        report_unhandled_user_exception(r);
    else if (report_kernel_touched_user(r))
        return;
    else
        dump_exception(r, read_cr2());
    panic_halt();
}

static void irq_eoi(uint32_t irq) {
    if (apic_active()) {
        lapic_eoi();
    } else {
        pic_send_eoi((uint8_t)irq);
    }
}
const struct IRQ_HANDLER *irq_handler_lookup(uint8_t irq) {
    for (const struct IRQ_HANDLER *h = __irq_handlers_start;
         h < __irq_handlers_end; h++) {
        if (h->irq == irq)
            return h;
    }
    return NULL;
}
static volatile uint32_t cpu_ipi_ticks[NR_CPU];
void irq_handler(struct ARCH_REGS *r) {
    if (r->int_no == IPI_VECTOR_RESCHED) {
        uint32_t c = cpu_id();
        if (c < NR_CPU)
            cpu_ipi_ticks[c]++;
        lapic_eoi();
        if (current != 0 && preempt_disabled() == 0)
            schedule();
        return;
    }
    if (r->int_no == LAPIC_CALIB_VECTOR) {
        lapic_calib_count++;
        lapic_eoi();
        return;
    }
    uint32_t irq = r->int_no - 32;
    if (irq == 0) {
        uint32_t cpu = cpu_id();
        int percpu_tick = lapic_timer_on();
        irq_eoi(irq);
        if (!percpu_tick || cpu == 0) {
            tick++;
            itimer_tick();
            thread_timer_wake();
            scheduler_tick();
            if (current != 0) {
                check_pending_signals(r);
                if (preempt_disabled() == 0)
                    schedule();
            }
            if (!percpu_tick && apic_active())
                lapic_send_ipi_all_but_self(IPI_VECTOR_RESCHED);
        } else {
            if (current != 0 && current->weight != 0)
                scheduler_tick();
            if (current != 0 && preempt_disabled() == 0)
                schedule();
        }
        return;
    }
    const struct IRQ_HANDLER *h = irq_handler_lookup((uint8_t)irq);
    if (h != NULL) {
        h->handler((uint8_t)r->int_no);
    }
    irq_eoi(irq);
}
