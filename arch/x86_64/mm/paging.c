#include "arch/x86_64/mm/paging.h"
#include "arch/mmu.h"
#include "arch/percpu.h"
#include "kernel/sched/thread.h"
#include "lib/string/str.h"
#include "mm/pool.h"

uint64_t arch_boot_pgd(void) {
    return asm_read_cr3();
}

uint64_t arch_current_pgd(void) {
    if (current && current->pml4_phys)
        return (uint64_t)current->pml4_phys;
    return asm_read_cr3();
}

static uint64_t *table_at(uint64_t phys) {
    return (uint64_t *)(uintptr_t)VIRT_OF(phys);
}

uint64_t *arch_pte_lookup(uint64_t pgd_phys, uint64_t vaddr) {
    uint64_t e = table_at(pgd_phys)[X86_PML4_INDEX(vaddr)];
    if (!(e & PTE_P))
        return 0;
    e = table_at(X86_PTE_PHYS(e))[X86_PDPT_INDEX(vaddr)];
    if (!(e & PTE_P))
        return 0;
    e = table_at(X86_PTE_PHYS(e))[X86_PD_INDEX(vaddr)];
    if (!(e & PTE_P))
        return 0;
    if (e & X86_PTE_PS)
        return &table_at(X86_PTE_PHYS(e))[X86_PT_INDEX(vaddr)];
    return &table_at(X86_PTE_PHYS(e))[X86_PT_INDEX(vaddr)];
}

uint64_t *arch_pde_lookup(uint64_t pgd_phys, uint64_t vaddr) {
    uint64_t e = table_at(pgd_phys)[X86_PML4_INDEX(vaddr)];
    if (!(e & PTE_P))
        return 0;
    e = table_at(X86_PTE_PHYS(e))[X86_PDPT_INDEX(vaddr)];
    if (!(e & PTE_P))
        return 0;
    return &table_at(X86_PTE_PHYS(e))[X86_PD_INDEX(vaddr)];
}

static int alloc_table(uint64_t *slot, uint64_t *out_phys) {
    uint32_t pa = palloc_phys();
    if (pa == 0)
        return -1;
    *slot = (uint64_t)pa | PTE_P | PTE_W | PTE_U;
    memset(table_at(pa), 0, PAGE_SIZE);
    *out_phys = (uint64_t)pa;
    return 0;
}

uint64_t *arch_pte_create(uint64_t pgd_phys, uint64_t vaddr) {
    uint64_t *pml4 = table_at(pgd_phys);
    uint64_t phys;
    uint32_t idx = X86_PML4_INDEX(vaddr);
    if (!(pml4[idx] & PTE_P)) {
        if (alloc_table(&pml4[idx], &phys) != 0)
            return 0;
    }
    uint64_t *pdp = table_at(X86_PTE_PHYS(pml4[idx]));
    idx = X86_PDPT_INDEX(vaddr);
    if (!(pdp[idx] & PTE_P)) {
        if (alloc_table(&pdp[idx], &phys) != 0)
            return 0;
    }
    uint64_t *pd = table_at(X86_PTE_PHYS(pdp[idx]));
    idx = X86_PD_INDEX(vaddr);
    if (!(pd[idx] & PTE_P)) {
        if (alloc_table(&pd[idx], &phys) != 0)
            return 0;
    }
    return &table_at(X86_PTE_PHYS(pd[idx]))[X86_PT_INDEX(vaddr)];
}

static void walk_pd(uint64_t pd_phys, void (*mark)(uint64_t, uint64_t, void *), void *ctx) {
    uint64_t *pd = table_at(pd_phys);
    for (int k = 0; k < 512; k++) {
        uint64_t e = pd[k];
        if (!(e & PTE_P) || (e & X86_PTE_PS)) {
            continue;
        }
        mark(X86_PTE_PHYS(e), PAGE_SIZE, ctx);
    }
}

static void walk_pdp(uint64_t *pdp, void (*mark)(uint64_t, uint64_t, void *), void *ctx) {
    for (int j = 0; j < 512; j++) {
        uint64_t e = pdp[j];
        if (!(e & PTE_P) || (e & X86_PTE_PS)) {
            continue;
        }
        uint64_t pd_phys = X86_PTE_PHYS(e);
        mark(pd_phys, PAGE_SIZE, ctx);
        walk_pd(pd_phys, mark, ctx);
    }
}

void arch_walk_page_tables(uint64_t pgd_phys,
                           void (*mark)(uint64_t phys, uint64_t bytes, void *ctx), void *ctx) {
    uint64_t *pml4 = table_at(pgd_phys);
    for (int i = 0; i < 512; i++) {
        uint64_t e = pml4[i];
        if (!(e & PTE_P)) {
            continue;
        }
        uint64_t pdp_phys = X86_PTE_PHYS(e);
        mark(pdp_phys, PAGE_SIZE, ctx);
        walk_pdp(table_at(pdp_phys), mark, ctx);
    }
}
