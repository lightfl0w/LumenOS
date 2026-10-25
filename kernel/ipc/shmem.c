#include "kernel/ipc/shmem.h"

#include "arch/x86_64/mmu.h"
#include "arch/x86_64/mm/paging.h"
#include "fs/file.h"
#include "fs/fs.h"
#include "kernel/sched/thread.h"
#include "kernel/syscall/mmap.h"
#include "lib/malloc/kmalloc.h"
#include "lib/string/str.h"
#include "mm/pool.h"

#include <stdint.h>

#define SHMEM_MAX_OBJ 32

struct SHMEM_OBJ {
    uint8_t used;
    uint32_t size;
    uint32_t page_count;
    uint32_t *phys;
};

static struct SHMEM_OBJ shmem_objs[SHMEM_MAX_OBJ];

static struct SHMEM_OBJ *obj_of(struct FILE *file) {
    if (file == NULL || file->fd_flag != MEMFD_FLAG || file->dev_priv == NULL) {
        return NULL;
    }
    return (struct SHMEM_OBJ *)file->dev_priv;
}

static struct SHMEM_OBJ *obj_alloc(void) {
    for (int i = 0; i < SHMEM_MAX_OBJ; i++) {
        if (!shmem_objs[i].used) {
            memset(&shmem_objs[i], 0, sizeof(shmem_objs[i]));
            shmem_objs[i].used = 1;
            return &shmem_objs[i];
        }
    }
    return NULL;
}

static void obj_release(struct SHMEM_OBJ *obj) {
    for (uint32_t i = 0; i < obj->page_count; i++) {
        pfree_phys(obj->phys[i]);
    }
    if (obj->phys != NULL) {
        kfree(obj->phys);
    }
    memset(obj, 0, sizeof(*obj));
}

static int32_t obj_resize(struct SHMEM_OBJ *obj, uint32_t size) {
    uint32_t pages = (size + PAGE_SIZE - 1) / PAGE_SIZE;

    if (pages == obj->page_count) {
        obj->size = size;
        return 0;
    }

    uint32_t *nphys = NULL;
    if (pages > 0) {
        nphys = kmalloc(pages * sizeof(uint32_t));
        if (nphys == NULL) {
            return -1;
        }
    }

    uint32_t keep = pages < obj->page_count ? pages : obj->page_count;
    for (uint32_t i = 0; i < keep; i++) {
        nphys[i] = obj->phys[i];
    }

    for (uint32_t i = pages; i < obj->page_count; i++) {
        pfree_phys(obj->phys[i]);
    }

    for (uint32_t i = obj->page_count; i < pages; i++) {
        uint32_t pa = palloc_phys();
        if (pa == 0) {
            for (uint32_t k = obj->page_count; k < i; k++) {
                pfree_phys(nphys[k]);
            }
            kfree(nphys);
            return -1;
        }
        memset((void *)(uintptr_t)VIRT_OF(pa), 0, PAGE_SIZE);
        nphys[i] = pa;
    }

    if (obj->phys != NULL) {
        kfree(obj->phys);
    }
    obj->phys = nphys;
    obj->page_count = pages;
    obj->size = size;
    return 0;
}

int memfd_create_fd(void) {
    struct SHMEM_OBJ *obj = obj_alloc();
    if (obj == NULL) {
        return -1;
    }

    int gfd = file_table_alloc_slot();
    if (gfd == -1) {
        obj_release(obj);
        return -1;
    }

    struct FILE *file = file_get((uint32_t)gfd);
    memset(file, 0, sizeof(*file));
    file->fd_flag = MEMFD_FLAG;
    file->fd_inode = NULL;
    file->dev_priv = obj;
    file->ref_cnt = 1;

    int fd = fd_install(gfd);
    if (fd == -1) {
        file_table_free_slot(gfd);
        obj_release(obj);
        return -1;
    }
    return fd;
}

int32_t shmem_truncate_fd(int32_t fd, uint32_t length) {
    if (fd < 0 || fd >= (int32_t)MAX_FILES_OPEN_PER_PROC) {
        return -1;
    }
    uint32_t gfd = fd_local2global((uint32_t)fd);
    if (gfd == (uint32_t)-1) {
        return -1;
    }
    struct SHMEM_OBJ *obj = obj_of(file_get(gfd));
    if (obj == NULL) {
        return -1;
    }
    return obj_resize(obj, length);
}

void shmem_close(struct FILE *file) {
    struct SHMEM_OBJ *obj = obj_of(file);
    if (obj != NULL) {
        obj_release(obj);
        file->dev_priv = NULL;
    }
}

int shmem_fstat(struct FILE *file, struct FS_STAT *st) {
    struct SHMEM_OBJ *obj = obj_of(file);
    if (obj == NULL) {
        return -1;
    }
    memset(st, 0, sizeof(*st));
    st->st_ino = 1;
    st->st_filetype = FT_REGULAR;
    st->st_size = obj->size;
    return 0;
}

int shmem_fd_mmap(int32_t fd, uint32_t base, uint32_t pages, uint32_t prot, uint64_t offset) {
    if (fd < 0 || fd >= (int32_t)MAX_FILES_OPEN_PER_PROC) {
        return -1;
    }
    if ((offset & (PAGE_SIZE - 1)) != 0) {
        return -1;
    }
    uint32_t gfd = fd_local2global((uint32_t)fd);
    if (gfd == (uint32_t)-1) {
        return -1;
    }
    struct SHMEM_OBJ *obj = obj_of(file_get(gfd));
    if (obj == NULL) {
        return -1;
    }

    uint64_t end = offset + (uint64_t)pages * PAGE_SIZE;
    if (end > (uint64_t)obj->page_count * PAGE_SIZE) {
        return -1;
    }

    uint32_t first = (uint32_t)(offset / PAGE_SIZE);
    for (uint32_t i = 0; i < pages; i++) {
        uint64_t *pte = arch_pte_create(arch_current_pgd(), base + i * PAGE_SIZE);
        if (pte == NULL) {
            return -1;
        }
        *pte = pte_wx((uint64_t)obj->phys[first + i] | PTE_P | PTE_U,
                      !!(prot & PROT_WRITE), !!(prot & PROT_EXEC));
        arch_tlb_flush(base + i * PAGE_SIZE);
    }
    return 0;
}
