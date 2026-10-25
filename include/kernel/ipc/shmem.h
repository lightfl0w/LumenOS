#ifndef KERNEL_IPC_SHMEM_H
#define KERNEL_IPC_SHMEM_H

#include <stdint.h>

struct FILE;
struct FS_STAT;

#define MEMFD_FLAG 0xFFFCu

int memfd_create_fd(void);
int32_t shmem_truncate_fd(int32_t fd, uint32_t length);
void shmem_close(struct FILE *file);
int shmem_fstat(struct FILE *file, struct FS_STAT *st);
int shmem_fd_mmap(int32_t fd, uint32_t base, uint32_t pages, uint32_t prot, uint64_t offset);

#endif
