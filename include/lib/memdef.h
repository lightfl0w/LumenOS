#ifndef LIB_MEMDEF_H
#define LIB_MEMDEF_H

#include <stddef.h>
#include <stdint.h>

#define PAGE_SIZE   0x1000
#define MEMORY_BASE 0x100000
#define MAX_PHYS_MEM 0x20000000

#define COW_FLAG (1u << 9)

#define DIV_ROUND_UP(x, step) (((x) + (step) - 1) / (step))

void *get_kernel_pages(uint32_t pg_cnt);
void free_kernel_page(uint32_t vaddr);

#endif
