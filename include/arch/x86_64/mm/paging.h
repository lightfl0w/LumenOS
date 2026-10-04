#ifndef ARCH_X86_64_MM_PAGING_H
#define ARCH_X86_64_MM_PAGING_H

#include <stdint.h>

#define X86_PML4_INDEX(v) (((uint64_t)(v) >> 39) & 0x1ffu)
#define X86_PDPT_INDEX(v) (((uint64_t)(v) >> 30) & 0x1ffu)
#define X86_PD_INDEX(v) (((uint64_t)(v) >> 21) & 0x1ffu)
#define X86_PT_INDEX(v) (((uint64_t)(v) >> 12) & 0x1ffu)

#define X86_PTE_PHYS(e) ((uint64_t)(e) & 0x000ffffffffff000ull)
#define X86_PTE_PS (1ull << 7)
#define X86_PTE_NX (1ull << 63)

uint64_t arch_current_pgd(void);
uint64_t arch_boot_pgd(void);
uint64_t *arch_pte_lookup(uint64_t pgd_phys, uint64_t vaddr);
uint64_t *arch_pte_create(uint64_t pgd_phys, uint64_t vaddr);
uint64_t *arch_pde_lookup(uint64_t pgd_phys, uint64_t vaddr);
void arch_walk_page_tables(uint64_t pgd_phys,
                           void (*mark)(uint64_t phys, uint64_t bytes, void *ctx), void *ctx);

#endif
