#include "kernel/ipc/shmem.h"

#include "arch/x86_64/mmu.h"
#include "arch/x86_64/mm/paging.h"
#include "fs/file.h"
#include "fs/fs.h"
#include "kernel/sched/thread.h"
#include "kernel/syscall/mmap.h"
#include "lib/malloc/kmalloc.h"
#include "lib/printf/printf.h"
#include "lib/string/str.h"
#include "lib/sync/spin.h"
#include "mm/access.h"
#include "mm/pool.h"

#include <stdint.h>

#define SHMEM_MAX_OBJ 32

#define SHMEM_CHUNK_SHIFT 10
#define SHMEM_CHUNK_PAGES (1u << SHMEM_CHUNK_SHIFT)
#define SHMEM_CHUNK_MASK (SHMEM_CHUNK_PAGES - 1)

struct SHMEM_OBJ {
    uint8_t used;
    uint8_t file_alive;
    uint16_t pad;
    uint32_t size;
    uint32_t page_count;
    uint32_t chunk_count;
    uint32_t map_refs;
    uint32_t **chunks;
};

static struct SHMEM_OBJ shmem_objs[SHMEM_MAX_OBJ];
static struct LIB_SPINLOCK shmem_lock;

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

static void obj_release_locked(struct SHMEM_OBJ *obj) {
    for (uint32_t ci = 0; ci < obj->chunk_count; ci++) {
        uint32_t *chunk = obj->chunks != NULL ? obj->chunks[ci] : NULL;
        if (chunk == NULL) {
            continue;
        }
        for (uint32_t pi = 0; pi < SHMEM_CHUNK_PAGES; pi++) {
            if (chunk[pi] != 0) {
                pfree_phys(chunk[pi]);
            }
        }
        kfree(chunk);
    }
    if (obj->chunks != NULL) {
        kfree(obj->chunks);
    }
    memset(obj, 0, sizeof(*obj));
}

static uint32_t obj_page_get(struct SHMEM_OBJ *obj, uint32_t idx) {
    uint32_t ci = idx >> SHMEM_CHUNK_SHIFT;
    if (idx >= obj->page_count || ci >= obj->chunk_count || obj->chunks == NULL ||
        obj->chunks[ci] == NULL) {
        return 0;
    }
    return obj->chunks[ci][idx & SHMEM_CHUNK_MASK];
}

static int obj_chunk_ensure(struct SHMEM_OBJ *obj, uint32_t ci) {
    if (obj->chunks == NULL || ci >= obj->chunk_count) {
        return -1;
    }
    if (obj->chunks[ci] == NULL) {
        uint32_t *chunk = kmalloc(SHMEM_CHUNK_PAGES * sizeof(uint32_t));
        if (chunk == NULL) {
            return -1;
        }
        memset(chunk, 0, SHMEM_CHUNK_PAGES * sizeof(uint32_t));
        obj->chunks[ci] = chunk;
    }
    return 0;
}

static uint32_t obj_page_ensure(struct SHMEM_OBJ *obj, uint32_t idx) {
    uint32_t pa = obj_page_get(obj, idx);
    if (pa != 0) {
        return pa;
    }
    if (idx >= obj->page_count) {
        return 0;
    }
    if (obj_chunk_ensure(obj, idx >> SHMEM_CHUNK_SHIFT) != 0) {
        return 0;
    }
    pa = palloc_phys();
    if (pa == 0) {
        return 0;
    }
    memset((void *)(uintptr_t)VIRT_OF(pa), 0, PAGE_SIZE);
    obj->chunks[idx >> SHMEM_CHUNK_SHIFT][idx & SHMEM_CHUNK_MASK] = pa;
    return pa;
}

static int32_t obj_resize(struct SHMEM_OBJ *obj, uint32_t size) {
    uint32_t pages = DIV_ROUND_UP(size, PAGE_SIZE);
    uint32_t new_chunks = DIV_ROUND_UP(pages, SHMEM_CHUNK_PAGES);
    uint32_t old_pages = obj->page_count;
    uint32_t old_chunks = obj->chunk_count;

    for (uint32_t idx = pages; idx < old_pages; idx++) {
        uint32_t ci = idx >> SHMEM_CHUNK_SHIFT;
        if (ci >= old_chunks || obj->chunks == NULL || obj->chunks[ci] == NULL) {
            continue;
        }
        uint32_t pa = obj->chunks[ci][idx & SHMEM_CHUNK_MASK];
        if (pa != 0) {
            obj->chunks[ci][idx & SHMEM_CHUNK_MASK] = 0;
            pfree_phys(pa);
        }
    }

    if (new_chunks != old_chunks) {
        uint32_t **nchunks = NULL;
        if (new_chunks > 0) {
            nchunks = kmalloc(new_chunks * sizeof(uint32_t *));
            if (nchunks == NULL) {
                return -1;
            }
            for (uint32_t ci = 0; ci < new_chunks; ci++) {
                nchunks[ci] = ci < old_chunks && obj->chunks != NULL
                                  ? obj->chunks[ci]
                                  : NULL;
            }
        }
        if (obj->chunks != NULL) {
            for (uint32_t ci = new_chunks; ci < old_chunks; ci++) {
                if (obj->chunks[ci] != NULL) {
                    kfree(obj->chunks[ci]);
                }
            }
            kfree(obj->chunks);
        }
        obj->chunks = nchunks;
        obj->chunk_count = new_chunks;
    }

    obj->page_count = pages;
    obj->size = size;
    return 0;
}

int memfd_create_fd(void) {
    lib_spin_acquire(&shmem_lock);
    struct SHMEM_OBJ *obj = obj_alloc();
    if (obj != NULL) {
        obj->file_alive = 1;
    }
    lib_spin_release(&shmem_lock);
    if (obj == NULL) {
        return -1;
    }

    int gfd = file_table_alloc_slot();
    if (gfd == -1) {
        lib_spin_acquire(&shmem_lock);
        memset(obj, 0, sizeof(*obj));
        lib_spin_release(&shmem_lock);
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
        lib_spin_acquire(&shmem_lock);
        memset(obj, 0, sizeof(*obj));
        lib_spin_release(&shmem_lock);
        return -1;
    }
    return fd;
}

static struct SHMEM_OBJ *obj_from_fd(int32_t fd) {
    if (fd < 0 || fd >= (int32_t)MAX_FILES_OPEN_PER_PROC) {
        return NULL;
    }
    uint32_t gfd = fd_local2global((uint32_t)fd);
    if (gfd == (uint32_t)-1) {
        return NULL;
    }
    return obj_of(file_get(gfd));
}

int shmem_fd_is_shmem(int32_t fd) {
    return obj_from_fd(fd) != NULL;
}

int32_t shmem_truncate_fd(int32_t fd, uint32_t length) {
    struct SHMEM_OBJ *obj = obj_from_fd(fd);
    if (obj == NULL) {
        return -1;
    }
    lib_spin_acquire(&shmem_lock);
    int32_t rc = obj_resize(obj, length);
    lib_spin_release(&shmem_lock);
    return rc;
}

void shmem_close(struct FILE *file) {
    struct SHMEM_OBJ *obj = obj_of(file);
    if (obj == NULL) {
        return;
    }
    lib_spin_acquire(&shmem_lock);
    obj->file_alive = 0;
    if (obj->map_refs == 0) {
        obj_release_locked(obj);
    }
    lib_spin_release(&shmem_lock);
    file->dev_priv = NULL;
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

#define SHMEM_SPACES_MAX 32
struct SHMEM_SPACE {
    uint8_t used;
    uint8_t count;
    uint16_t pad;
    uint32_t pml4;
    struct SHMEM_MAP maps[SHMEM_MAPS_MAX];
};
static struct SHMEM_SPACE shmem_spaces[SHMEM_SPACES_MAX];

static void obj_map_ref_locked(uint32_t idx) {
    if (idx < SHMEM_MAX_OBJ && shmem_objs[idx].used) {
        shmem_objs[idx].map_refs++;
    }
}

static void obj_map_unref_locked(uint32_t idx) {
    if (idx >= SHMEM_MAX_OBJ) {
        return;
    }
    struct SHMEM_OBJ *obj = &shmem_objs[idx];
    if (!obj->used) {
        return;
    }
    if (obj->map_refs > 0) {
        obj->map_refs--;
    }
    if (!obj->file_alive && obj->map_refs == 0) {
        obj_release_locked(obj);
    }
}

static struct SHMEM_SPACE *space_find_locked(uint32_t pml4) {
    if (pml4 == 0) {
        return NULL;
    }
    for (int i = 0; i < SHMEM_SPACES_MAX; i++) {
        if (shmem_spaces[i].used && shmem_spaces[i].pml4 == pml4) {
            return &shmem_spaces[i];
        }
    }
    return NULL;
}

static struct SHMEM_SPACE *space_get_locked(uint32_t pml4) {
    struct SHMEM_SPACE *space = space_find_locked(pml4);
    if (space != NULL) {
        return space;
    }
    for (int i = 0; i < SHMEM_SPACES_MAX; i++) {
        if (!shmem_spaces[i].used) {
            memset(&shmem_spaces[i], 0, sizeof(shmem_spaces[i]));
            shmem_spaces[i].used = 1;
            shmem_spaces[i].pml4 = pml4;
            return &shmem_spaces[i];
        }
    }
    return NULL;
}

int shmem_fd_mmap(int32_t fd, uint32_t base, uint32_t pages, uint32_t prot,
                  uint64_t offset) {
    struct SHMEM_OBJ *obj = obj_from_fd(fd);
    if (obj == NULL || current == NULL || current->pml4_phys == 0) {
        return -1;
    }
    if ((offset & (PAGE_SIZE - 1)) != 0) {
        return -1;
    }
    uint64_t end = offset + (uint64_t)pages * PAGE_SIZE;
    if (end > (uint64_t)obj->page_count * PAGE_SIZE) {
        return -1;
    }

    lib_spin_acquire(&shmem_lock);
    struct SHMEM_SPACE *space = space_get_locked((uint32_t)current->pml4_phys);
    if (space == NULL || space->count >= SHMEM_MAPS_MAX) {
        lib_spin_release(&shmem_lock);
        return -1;
    }
    struct SHMEM_MAP *map = &space->maps[space->count];
    map->start = base;
    map->pages = pages;
    map->obj_idx = (uint32_t)(obj - shmem_objs);
    map->obj_off = (uint32_t)offset;
    map->prot = prot;
    space->count++;
    obj_map_ref_locked(map->obj_idx);
    lib_spin_release(&shmem_lock);

    return 0;
}

static struct SHMEM_MAP *map_find_locked(struct SHMEM_SPACE *space, uint32_t vaddr) {
    if (space == NULL) {
        return NULL;
    }
    for (uint32_t i = 0; i < space->count; i++) {
        struct SHMEM_MAP *map = &space->maps[i];
        uint64_t span = (uint64_t)map->pages * PAGE_SIZE;
        if (vaddr >= map->start && (uint64_t)(vaddr - map->start) < span) {
            return map;
        }
    }
    return NULL;
}

int shmem_fault(uint32_t vaddr, int write, uint32_t *phys, uint32_t *prot) {
    if (current == NULL || current->pml4_phys == 0) {
        return 0;
    }
    lib_spin_acquire(&shmem_lock);
    struct SHMEM_SPACE *space = space_find_locked((uint32_t)current->pml4_phys);
    struct SHMEM_MAP *map = map_find_locked(space, vaddr);
    if (map == NULL) {
        lib_spin_release(&shmem_lock);
        return 0;
    }
    if (write && !(map->prot & PROT_WRITE)) {
        lib_spin_release(&shmem_lock);
        return 0;
    }
    uint32_t idx = (map->obj_off >> 12) + ((vaddr - map->start) >> 12);
    struct SHMEM_OBJ *obj = &shmem_objs[map->obj_idx];
    uint32_t pa = 0;
    if (obj->used && idx < obj->page_count) {
        pa = obj_page_ensure(obj, idx);
    }
    uint32_t fault_prot = map->prot;
    lib_spin_release(&shmem_lock);
    if (pa == 0) {
        return 0;
    }
    *phys = pa;
    *prot = fault_prot;
    return 1;
}

int shmem_ensure_page(uint32_t vaddr, int write) {
    uint32_t pa;
    uint32_t prot;
    if (!shmem_fault(vaddr & ~0xfffu, write, &pa, &prot)) {
        return -1;
    }
    if (page_is_mapped(vaddr & ~0xfffu)) {
        return 0;
    }
    return user_map_shmem_page(vaddr & ~0xfffu, pa, prot);
}

void shmem_unmap_range(uint32_t pml4, uint32_t start, uint32_t pages) {
    if (pml4 == 0 || pages == 0) {
        return;
    }
    uint64_t end = (uint64_t)start + (uint64_t)pages * PAGE_SIZE;
    lib_spin_acquire(&shmem_lock);
    struct SHMEM_SPACE *space = space_find_locked(pml4);
    if (space == NULL) {
        lib_spin_release(&shmem_lock);
        return;
    }
    uint32_t i = 0;
    while (i < space->count) {
        struct SHMEM_MAP *map = &space->maps[i];
        uint64_t mstart = map->start;
        uint64_t mend = mstart + (uint64_t)map->pages * PAGE_SIZE;
        if (end <= mstart || start >= mend) {
            i++;
            continue;
        }
        uint64_t ov_start = start > mstart ? start : mstart;
        uint64_t ov_end = end < mend ? end : mend;
        uint32_t head = (uint32_t)((ov_start - mstart) / PAGE_SIZE);
        uint32_t tail = (uint32_t)((mend - ov_end) / PAGE_SIZE);
        if (head == 0 && tail == 0) {
            obj_map_unref_locked(map->obj_idx);
            space->count--;
            if (i < space->count) {
                space->maps[i] = space->maps[space->count];
            }
            continue;
        }
        if (head == 0) {
            uint32_t dropped = (uint32_t)((ov_end - ov_start) / PAGE_SIZE);
            map->start += dropped * PAGE_SIZE;
            map->obj_off += dropped * PAGE_SIZE;
            map->pages -= dropped;
        } else if (tail == 0) {
            map->pages = head;
        } else if (space->count < SHMEM_MAPS_MAX) {
            struct SHMEM_MAP tail_map = *map;
            tail_map.start = (uint32_t)ov_end;
            tail_map.obj_off += (uint32_t)(ov_end - mstart);
            tail_map.pages = tail;
            map->pages = head;
            space->maps[space->count++] = tail_map;
            obj_map_ref_locked(tail_map.obj_idx);
        } else {
            map->pages = head;
        }
        i++;
    }
    lib_spin_release(&shmem_lock);
}

void shmem_release_space(uint32_t pml4) {
    if (pml4 == 0) {
        return;
    }
    lib_spin_acquire(&shmem_lock);
    struct SHMEM_SPACE *space = space_find_locked(pml4);
    if (space != NULL) {
        for (uint32_t i = 0; i < space->count; i++) {
            obj_map_unref_locked(space->maps[i].obj_idx);
        }
        memset(space, 0, sizeof(*space));
    }
    lib_spin_release(&shmem_lock);
}

void shmem_fork_space(uint32_t parent_pml4, uint32_t child_pml4) {
    if (parent_pml4 == 0 || child_pml4 == 0) {
        return;
    }
    lib_spin_acquire(&shmem_lock);
    struct SHMEM_SPACE *parent = space_find_locked(parent_pml4);
    if (parent == NULL || parent->count == 0) {
        lib_spin_release(&shmem_lock);
        return;
    }
    struct SHMEM_SPACE *old_child = space_find_locked(child_pml4);
    if (old_child != NULL) {
        for (uint32_t i = 0; i < old_child->count; i++) {
            obj_map_unref_locked(old_child->maps[i].obj_idx);
        }
        memset(old_child, 0, sizeof(*old_child));
    }
    struct SHMEM_SPACE *child = space_get_locked(child_pml4);
    if (child == NULL) {
        lib_spin_release(&shmem_lock);
        return;
    }
    for (uint32_t i = 0; i < parent->count && child->count < SHMEM_MAPS_MAX; i++) {
        child->maps[child->count] = parent->maps[i];
        obj_map_ref_locked(parent->maps[i].obj_idx);
        child->count++;
    }
    lib_spin_release(&shmem_lock);
}
