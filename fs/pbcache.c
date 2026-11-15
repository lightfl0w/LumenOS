#include "fs/pbcache.h"

#include "kernel/sched/thread.h"
#include "arch/time/pit.h"

static void pbc_flush_thread(void *arg);
static uint8_t *pbc_bounce_get(uint32_t blocks);
static int pbc_load_slot(uint32_t dev, uint32_t blk, const uint8_t *src);
static inline uint32_t pbc_bucket(uint32_t dev, uint32_t blk);
void pbc_flush_hook(void) __attribute__((weak));
#include "drivers/char/serial/console/io.h"
#include "arch/asm_func.h"
#include "kernel/sync/sync.h"
#include "lib/string/str.h"
#include "mm/pool.h"
#include "ops/block_ops.h"

#define PBC_DEV_SLOTS_MAX 4096u
#define PBC_HASH_BUCKETS 1024u

static struct PBC_SLOT s_slots[PBC_DEV_SLOTS_MAX];
static uint32_t s_nr_slots;
static uint8_t s_pool_ready;
static struct PBC_DEV s_devs[PBC_DEV_MAX];
static uint32_t s_nr_devs;
static uint32_t s_seq;
static struct SCHED_SPINLOCK pbc_lock;
static struct PBC_SLOT *s_hash[PBC_HASH_BUCKETS];
static struct PBC_SLOT *s_lru_head;
static struct PBC_SLOT *s_lru_tail;
static uint32_t s_free_cursor;

static struct PBC_DEV *dev_of(uint32_t id) {
    for (uint32_t i = 0; i < s_nr_devs; i++) {
        if (s_devs[i].id == id) {
            return &s_devs[i];
        }
    }
    return NULL;
}

static int pbc_fill_from_disk(struct PBC_DEV *d, uint32_t blk, uint8_t *dst) {
    if (d == NULL || d->disk == NULL) {
        return -1;
    }
    BLOCK.read_sectors(d->disk, d->start + blk * d->spb, dst, d->spb);
    return 0;
}

static void pbc_write_to_disk(struct PBC_DEV *d, uint32_t blk, const uint8_t *src) {
    if (d == NULL || d->disk == NULL) {
        return;
    }
    BLOCK.write_sectors(d->disk, d->start + blk * d->spb, (void *)src, d->spb);
}

static inline uint32_t pbc_lk(void) {
    uint32_t f = asm_save_eflags();
    asm_cli();
    while (pbc_lock.locked) {
        asm_pause();
    }
    pbc_lock.locked = 1;
    return f;
}

static inline void pbc_unlk(uint32_t f) {
    pbc_lock.locked = 0;
    asm_restore_eflags(f);
}

static void pbc_pool_init(void) {
    uint32_t want = PBC_DEV_SLOTS_MAX;
    while (want >= 64u) {
        uint8_t *base = (uint8_t *)get_kernel_pages(want * PBC_SLOT_SIZE / PAGE_SIZE);
        if (base == NULL) {
            want /= 2u;
            continue;
        }
        for (uint32_t i = 0; i < want; i++) {
            s_slots[i].data = base + i * PBC_SLOT_SIZE;
        }
        s_nr_slots = want;
        break;
    }
    if (want < 64u) {
        kprintf("pbc: disabled (no memory)\n");
        s_nr_slots = 0;
    }
    pbc_lock.locked = 0;
    s_pool_ready = 1;
    kernel_thread("pbcflush", 10, pbc_flush_thread, NULL, 0xF);
}

int pbc_dev_register(uint32_t dev_id, struct DISK *d, uint32_t start_lba, uint32_t sect_per_block,
                     uint32_t block_size) {
    if (dev_of(dev_id) != NULL) {
        return 0;
    }
    if (s_nr_devs >= PBC_DEV_MAX) {
        return -1;
    }
    if (s_pool_ready == 0) {
        pbc_pool_init();
    }
    struct PBC_DEV *dv = &s_devs[s_nr_devs++];
    dv->id = dev_id;
    dv->disk = d;
    dv->start = start_lba;
    dv->spb = sect_per_block;
    dv->bs = block_size;
    dv->last_blk = 0;
    dv->have_last = 0;
    return 0;
}

static int pbc_present(uint32_t dev, uint32_t blk) {
    struct PBC_SLOT *s = s_hash[pbc_bucket(dev, blk)];
    while (s != NULL) {
        if (s->dev == dev && s->blk == blk && s->valid) {
            return 1;
        }
        s = s->hnext;
    }
    return 0;
}

static int pbc_busy_present(uint32_t dev, uint32_t blk) {
    struct PBC_SLOT *s = s_hash[pbc_bucket(dev, blk)];
    while (s != NULL) {
        if (s->dev == dev && s->blk == blk && s->valid && s->busy) {
            return 1;
        }
        s = s->hnext;
    }
    return 0;
}

static void pbc_wait_free(uint32_t dev, uint32_t blk) {
    for (;;) {
        uint32_t f = pbc_lk();
        struct PBC_SLOT *s = s_hash[pbc_bucket(dev, blk)];
        int busy = 0;
        while (s != NULL) {
            if (s->dev == dev && s->blk == blk && s->valid && s->busy) {
                busy = 1;
                break;
            }
            s = s->hnext;
        }
        pbc_unlk(f);
        if (!busy) {
            return;
        }
        mtime_sleep(1);
    }
}

#define PBC_RA_MAX 8u

static void pbc_readahead(struct PBC_DEV *d, uint32_t dev, uint32_t blk) {
    uint32_t f = pbc_lk();
    uint32_t run = 0;
    while (run < PBC_RA_MAX && pbc_present(dev, blk + run) == 0) {
        run++;
    }
    pbc_unlk(f);
    if (run < 2) {
        return;
    }
    uint8_t *bounce = pbc_bounce_get(run);
    if (bounce == NULL) {
        return;
    }
    BLOCK.read_sectors(d->disk, d->start + blk * d->spb, bounce, run * d->spb);
    for (uint32_t k = 0; k < run; k++) {
        pbc_load_slot(dev, blk + k, bounce + k * d->bs);
    }
}

static inline uint32_t pbc_bucket(uint32_t dev, uint32_t blk) {
    uint32_t h = blk * 2654435761u + dev * 0x9E3779B9u;
    return (h ^ (h >> 16)) & (PBC_HASH_BUCKETS - 1);
}

static void pbc_hash_insert(struct PBC_SLOT *s) {
    uint32_t b = pbc_bucket(s->dev, s->blk);
    s->hnext = s_hash[b];
    s_hash[b] = s;
}

static void pbc_hash_remove(struct PBC_SLOT *s) {
    uint32_t b = pbc_bucket(s->dev, s->blk);
    struct PBC_SLOT **pp = &s_hash[b];
    while (*pp != NULL && *pp != s) {
        pp = &(*pp)->hnext;
    }
    if (*pp == s) {
        *pp = s->hnext;
    }
    s->hnext = NULL;
}

static void pbc_lru_push(struct PBC_SLOT *s) {
    s->lprev = NULL;
    s->lnext = s_lru_head;
    if (s_lru_head != NULL) {
        s_lru_head->lprev = s;
    }
    s_lru_head = s;
    if (s_lru_tail == NULL) {
        s_lru_tail = s;
    }
}

static void pbc_lru_remove(struct PBC_SLOT *s) {
    if (s->lprev != NULL) {
        s->lprev->lnext = s->lnext;
    } else {
        s_lru_head = s->lnext;
    }
    if (s->lnext != NULL) {
        s->lnext->lprev = s->lprev;
    } else {
        s_lru_tail = s->lprev;
    }
    s->lprev = s->lnext = NULL;
}

static void pbc_touch(struct PBC_SLOT *s) {
    pbc_lru_remove(s);
    pbc_lru_push(s);
    s->last_used = ++s_seq;
}

static void pbc_publish(struct PBC_SLOT *s) {
    pbc_hash_insert(s);
    pbc_lru_push(s);
    s->last_used = ++s_seq;
}

static struct PBC_SLOT *pbc_find(uint32_t dev, uint32_t blk) {
    struct PBC_SLOT *s = s_hash[pbc_bucket(dev, blk)];
    while (s != NULL) {
        if (s->dev == dev && s->blk == blk && s->valid && s->busy == 0) {
            return s;
        }
        s = s->hnext;
    }
    return NULL;
}

static struct PBC_SLOT *pbc_alloc(uint32_t dev, uint32_t blk, uint32_t *wb_dev, uint32_t *wb_blk,
                                  uint8_t *wb_dirty) {
    struct PBC_SLOT *victim = NULL;
        for (uint32_t n = 0; n < s_nr_slots; n++) {
        uint32_t i = (s_free_cursor + n) % s_nr_slots;
        if (s_slots[i].valid == 0 && s_slots[i].busy == 0) {
            victim = &s_slots[i];
            s_free_cursor = (i + 1) % s_nr_slots;
            break;
        }
    }
    if (victim == NULL) {
        struct PBC_SLOT *s = s_lru_tail;
        while (s != NULL && s->busy) {
            s = s->lprev;
        }
        if (s == NULL) {
            return NULL;
        }
        victim = s;
        *wb_dev = victim->dev;
        *wb_blk = victim->blk;
        *wb_dirty = victim->dirty;
        pbc_hash_remove(victim);
        pbc_lru_remove(victim);
    }
    victim->busy = 1;
    victim->dev = dev;
    victim->blk = blk;
    victim->valid = 0;
    return victim;
}

int pbc_read(uint32_t dev, uint32_t blk, void *buf) {
    if (s_nr_slots == 0) {
        struct PBC_DEV *d = dev_of(dev);
        return pbc_fill_from_disk(d, blk, (uint8_t *)buf);
    }
    struct PBC_DEV *rd = dev_of(dev);
    uint8_t sequential = (rd != NULL && rd->have_last && blk == rd->last_blk + 1);
    if (rd != NULL) {
        rd->last_blk = blk;
        rd->have_last = 1;
    }
    uint32_t f;
    struct PBC_SLOT *s;
    for (;;) {
        f = pbc_lk();
        s = pbc_find(dev, blk);
        if (s != NULL) {
            break;
        }
        if (pbc_busy_present(dev, blk)) {
            pbc_unlk(f);
            mtime_sleep(1);
            continue;
        }
        break;
    }
    if (s != NULL) {
        s->busy = 1;
        pbc_touch(s);
        uint32_t bs = dev_of(dev)->bs;
        pbc_unlk(f);
        memcpy(buf, s->data, bs);
        f = pbc_lk();
        s->busy = 0;
        pbc_unlk(f);
        if (sequential) {
            pbc_readahead(rd, dev, blk + 1);
        }
        return 0;
    }
    uint32_t wb_dev = 0;
    uint32_t wb_blk = 0;
    uint8_t wb_dirty = 0;
    s = pbc_alloc(dev, blk, &wb_dev, &wb_blk, &wb_dirty);
    if (s == NULL) {
        pbc_unlk(f);
        return -1;
    }
    struct PBC_DEV *d = dev_of(dev);
    uint32_t bs = d->bs;
    uint32_t old_dev = wb_dev;
    uint32_t old_blk = wb_blk;
    struct PBC_DEV *od = wb_dirty ? dev_of(old_dev) : NULL;
    pbc_unlk(f);
    if (wb_dirty) {
        pbc_write_to_disk(od, old_blk, s->data);
    }
    int rc = pbc_fill_from_disk(d, blk, s->data);
    if (rc != 0) {
        f = pbc_lk();
        s->busy = 0;
        pbc_unlk(f);
        return rc;
    }
    memcpy(buf, s->data, bs);
    f = pbc_lk();
    s->valid = 1;
    s->dirty = 0;
    pbc_publish(s);
    s->busy = 0;
    pbc_unlk(f);
    if (sequential) {
        pbc_readahead(rd, dev, blk + 1);
    }
    return 0;
}

#define PBC_RUN_MAX 32u

static uint8_t *s_bounce;
static uint32_t s_bounce_blocks;

static uint8_t *pbc_bounce_get(uint32_t blocks) {
    if (s_bounce == NULL || s_bounce_blocks < blocks) {
        uint8_t *nb =
            (uint8_t *)get_kernel_pages((blocks * PBC_SLOT_SIZE + PAGE_SIZE - 1) / PAGE_SIZE);
        if (nb == NULL) {
            return NULL;
        }
        s_bounce = nb;
        s_bounce_blocks = blocks;
    }
    return s_bounce;
}

static int pbc_load_slot(uint32_t dev, uint32_t blk, const uint8_t *src) {
    uint32_t f;
    for (;;) {
        f = pbc_lk();
        if (pbc_find(dev, blk) != NULL) {
            pbc_unlk(f);
            return 1;
        }
        if (pbc_busy_present(dev, blk)) {
            pbc_unlk(f);
            mtime_sleep(1);
            continue;
        }
        break;
    }
    uint32_t wb_dev = 0;
    uint32_t wb_blk = 0;
    uint8_t wb_dirty = 0;
    struct PBC_SLOT *s = pbc_alloc(dev, blk, &wb_dev, &wb_blk, &wb_dirty);
    if (s == NULL) {
        pbc_unlk(f);
        return -1;
    }
    struct PBC_DEV *od = wb_dirty ? dev_of(wb_dev) : NULL;
    uint32_t bs = dev_of(dev)->bs;
    pbc_unlk(f);
    if (wb_dirty) {
        pbc_write_to_disk(od, wb_blk, s->data);
    }
    memcpy(s->data, src, bs);
    f = pbc_lk();
    s->valid = 1;
    s->dirty = 0;
    pbc_publish(s);
    s->busy = 0;
    pbc_unlk(f);
    return 0;
}

int pbc_read_run(uint32_t dev, uint32_t blk, uint32_t cnt, void *buf) {
    struct PBC_DEV *d = dev_of(dev);
    if (cnt == 0) {
        return 0;
    }
    if (d == NULL || d->disk == NULL) {
        return -1;
    }
    uint32_t bs = d->bs;
    uint8_t *dst = (uint8_t *)buf;
    if (s_nr_slots == 0) {
        BLOCK.read_sectors(d->disk, d->start + blk * d->spb, dst, cnt * d->spb);
        return 0;
    }
    uint32_t i = 0;
    while (i < cnt) {
        uint32_t f = pbc_lk();
        struct PBC_SLOT *s = pbc_find(dev, blk + i);
        if (s != NULL) {
            s->busy = 1;
            pbc_touch(s);
            pbc_unlk(f);
            memcpy(dst, s->data, bs);
            f = pbc_lk();
            s->busy = 0;
            pbc_unlk(f);
            i++;
            dst += bs;
            continue;
        }
        uint32_t run = 1;
        while (run < PBC_RUN_MAX && i + run < cnt) {
            if (pbc_find(dev, blk + i + run) != NULL) {
                break;
            }
            run++;
        }
        uint8_t *bounce = pbc_bounce_get(run);
        if (bounce == NULL) {
            pbc_unlk(f);
            for (uint32_t k = 0; k < run; k++) {
                if (pbc_read(dev, blk + i + k, dst + k * bs) != 0) {
                    return -1;
                }
            }
            i += run;
            dst += run * bs;
            continue;
        }
        pbc_unlk(f);
        BLOCK.read_sectors(d->disk, d->start + (blk + i) * d->spb, bounce, run * d->spb);
        for (uint32_t k = 0; k < run; k++) {
            pbc_load_slot(dev, blk + i + k, bounce + k * bs);
            memcpy(dst + k * bs, bounce + k * bs, bs);
        }
        i += run;
        dst += run * bs;
    }
    return 0;
}

int pbc_write(uint32_t dev, uint32_t blk, const void *buf) {
    if (s_nr_slots == 0) {
        struct PBC_DEV *d = dev_of(dev);
        if (d == NULL || d->disk == NULL) {
            return -1;
        }
        BLOCK.write_sectors(d->disk, d->start + blk * d->spb, (void *)buf, d->spb);
        return 0;
    }
    uint32_t bs = dev_of(dev)->bs;
    struct PBC_SLOT *s;
    uint32_t f;
    for (;;) {
        f = pbc_lk();
        s = pbc_find(dev, blk);
        if (s == NULL && pbc_busy_present(dev, blk)) {
            pbc_unlk(f);
            mtime_sleep(1);
            continue;
        }
        break;
    }
    if (s == NULL) {
        uint32_t wb_dev = 0;
        uint32_t wb_blk = 0;
        uint8_t wb_dirty = 0;
        s = pbc_alloc(dev, blk, &wb_dev, &wb_blk, &wb_dirty);
        if (s == NULL) {
            pbc_unlk(f);
            return -1;
        }
        struct PBC_DEV *od = wb_dirty ? dev_of(wb_dev) : NULL;
        pbc_unlk(f);
        if (wb_dirty) {
            pbc_write_to_disk(od, wb_blk, s->data);
        }
        memcpy(s->data, buf, bs);
        f = pbc_lk();
        s->valid = 1;
        s->dirty = 1;
        pbc_publish(s);
        s->busy = 0;
        pbc_unlk(f);
        return 0;
    }
    s->busy = 1;
    pbc_touch(s);
    pbc_unlk(f);
    memcpy(s->data, buf, bs);
    f = pbc_lk();
    s->valid = 1;
    s->dirty = 1;
    s->busy = 0;
    pbc_unlk(f);
    return 0;
}

int pbc_write_sync(uint32_t dev, uint32_t blk, const void *buf) {
    if (pbc_write(dev, blk, buf) != 0) {
        return -1;
    }
    if (s_nr_slots == 0) {
        return 0;
    }
    uint32_t f = pbc_lk();
    struct PBC_SLOT *s = pbc_find(dev, blk);
    if (s == NULL || s->busy) {
        pbc_unlk(f);
        return 0;
    }
    s->busy = 1;
    pbc_unlk(f);
    pbc_write_to_disk(dev_of(s->dev), s->blk, s->data);
    f = pbc_lk();
    s->dirty = 0;
    s->busy = 0;
    pbc_unlk(f);
    return 0;
}

static void pbc_flush_slot(struct PBC_SLOT *s) {
    uint32_t f = pbc_lk();
    if (s->valid == 0 || s->dirty == 0 || s->busy) {
        pbc_unlk(f);
        return;
    }
    struct PBC_DEV *d = dev_of(s->dev);
    if (d == NULL) {
        s->dirty = 0;
        pbc_unlk(f);
        return;
    }
    s->busy = 1;
    pbc_unlk(f);
    pbc_write_to_disk(d, s->blk, s->data);
    f = pbc_lk();
    s->dirty = 0;
    s->busy = 0;
    pbc_unlk(f);
}

void pbc_flush_all(void) {
    if (s_nr_slots == 0) {
        return;
    }
    for (uint32_t i = 0; i < s_nr_slots; i++) {
        pbc_flush_slot(&s_slots[i]);
    }
}

static void pbc_flush_thread(void *arg) {
    for (;;) {
        mtime_sleep(1000);
        pbc_flush_all();
        pbc_flush_hook();
    }
}

void pbc_flush_tick(void) {
    extern volatile uint32_t tick;
    static uint32_t last_flush;
    uint32_t now = tick;
    if ((uint32_t)(now - last_flush) < 500u) {
        return;
    }
    last_flush = now;
    pbc_flush_all();
}

uint32_t pbc_dirty_count(void) {
    uint32_t n = 0;
    for (uint32_t i = 0; i < s_nr_slots; i++) {
        if (s_slots[i].valid && s_slots[i].dirty) {
            n++;
        }
    }
    return n;
}
