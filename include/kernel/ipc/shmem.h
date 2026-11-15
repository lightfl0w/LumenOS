#ifndef KERNEL_IPC_SHMEM_H
#define KERNEL_IPC_SHMEM_H

#include <stdint.h>

struct FILE;
struct FS_STAT;
struct TASK;

#define MEMFD_FLAG 0xFFFCu

#define SHMEM_MAPS_MAX 16
struct SHMEM_MAP {
    uint32_t start;
    uint32_t pages;
    uint32_t obj_idx;
    uint32_t obj_off;
    uint32_t prot;
};

int memfd_create_fd(void);
int32_t shmem_truncate_fd(int32_t fd, uint32_t length);
void shmem_close(struct FILE *file);
int shmem_fstat(struct FILE *file, struct FS_STAT *st);
int shmem_fd_mmap(int32_t fd, uint32_t base, uint32_t pages, uint32_t prot, uint64_t offset);
int shmem_fd_is_shmem(int32_t fd);
int shmem_fault(uint32_t vaddr, int write, uint32_t *phys, uint32_t *prot);
int shmem_ensure_page(uint32_t vaddr, int write);
void shmem_unmap_range(uint32_t pml4, uint32_t start, uint32_t pages);
void shmem_release_space(uint32_t pml4);
void shmem_fork_space(uint32_t parent_pml4, uint32_t child_pml4);

#endif
