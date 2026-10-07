#include "kernel/userprog/process.h"
#include "arch/tls.h"
#include "arch/tss.h"
#include "drivers/char/serial/console/io.h"
#include "fs/file.h"
#include "kernel/asm/stub.h"
#include "kernel/asm_func.h"
#include "kernel/assert.h"
#include "kernel/userprog/exec.h"
#include "lib/string/str.h"
#include "mm/bitmap.h"
#include "mm/pool.h"
void start_process(void *arg) {
    char *path = (char *)arg;
    if (current->pml4_phys != 0) {
        process_activate(current);
    }
    const char *argv[] = {path, (const char *)0};
    if (sys_execv(path, argv, NULL) == -1) {
        kprintf("[exec] process_execute: load '%s' failed\n", path);
    }
    thread_exit_current();
}
void page_dir_activate(struct TASK *task) {
    if (task->pml4_phys == 0)
        return;
    asm_write_cr3((uint64_t)task->pml4_phys);
}
void process_activate(struct TASK *task) {
    if (task->pml4_phys != 0) {
        page_dir_activate(task);
        if (task->tls_msr) {
            arch_set_fs_base((uint64_t)task->tls_base);
        } else if (task->tls_selector != 0) {
            arch_tls_desc_set_base(task->tls_base);
        }
        arch_set_kernel_gs_base(task->gs_base_user);
    } else {
        asm_write_cr3(kernel_pml4);
    }
    tss_update_rsp0(task);
}
uint32_t *create_page_dir(void) {
    uint64_t pml4_phys = palloc_pages(&kernel_pool, 1);
    if (pml4_phys == 0) {
        return 0;
    }
    uint64_t *pml4 = phys_to_virt(pml4_phys);
    memset(pml4, 0, PAGE_SIZE);
    uint64_t pdp_phys = palloc_pages(&kernel_pool, 1);
    if (pdp_phys == 0) {
        pfree(&kernel_pool, (uint32_t)pml4_phys);
        return 0;
    }
    uint64_t *pdp = phys_to_virt(pdp_phys);
    memset(pdp, 0, PAGE_SIZE);
    pml4[0] = pdp_phys | 7;
    {
        uint64_t pd0_phys = palloc_pages(&kernel_pool, 1);
        if (pd0_phys == 0) {
            pfree(&kernel_pool, (uint32_t)pdp_phys);
            pfree(&kernel_pool, (uint32_t)pml4_phys);
            return 0;
        }
        uint64_t *pd0 = phys_to_virt(pd0_phys);
        memset(pd0, 0, PAGE_SIZE);
        uint64_t *loader_pd_low = phys_to_virt(0x92000);
        pd0[0] = loader_pd_low[0] & ~(uint64_t)PTE_U;
        pdp[0] = pd0_phys | 7;
    }
    {
        uint64_t *kpml4 = phys_to_virt(kernel_pml4);
        uint64_t kpdp0_phys = kpml4[0] & ~0xFFFull;
        uint64_t *kpdp0 = phys_to_virt(kpdp0_phys);
        pdp[1] = kpdp0[1];
    }
    uint64_t pd2_phys = palloc_pages(&kernel_pool, 1);
    if (pd2_phys == 0) {
        pfree(&kernel_pool, (uint32_t)PTE_PHYS(pdp[0]));
        pfree(&kernel_pool, (uint32_t)pdp_phys);
        pfree(&kernel_pool, (uint32_t)pml4_phys);
        return 0;
    }
    uint64_t *pd2 = phys_to_virt(pd2_phys);
    memset(pd2, 0, PAGE_SIZE);
    {
        uint64_t *loader_pd_lfb = phys_to_virt(0x94000);
        for (uint32_t i = 0; i < 512; i++) {
            if (loader_pd_lfb[i] & PTE_P)
                pd2[i] = loader_pd_lfb[i];
        }
    }
    pdp[2] = pd2_phys | 7;
    {
        uint64_t pd3_phys = palloc_pages(&kernel_pool, 1);
        if (pd3_phys == 0) {
            pfree(&kernel_pool, (uint32_t)PTE_PHYS(pdp[2]));
            pfree(&kernel_pool, (uint32_t)PTE_PHYS(pdp[0]));
            pfree(&kernel_pool, (uint32_t)pdp_phys);
            pfree(&kernel_pool, (uint32_t)pml4_phys);
            return 0;
        }
        uint64_t *pd3 = phys_to_virt(pd3_phys);
        memset(pd3, 0, PAGE_SIZE);
        memcpy(pd3, phys_to_virt(0x98000), PAGE_SIZE);
        pdp[3] = pd3_phys | 7;
    }
    return (uint32_t *)(uintptr_t)pml4_phys;
}
struct MM_SPACE_REF {
    uint32_t pml4;
    uint32_t refs;
};
static struct MM_SPACE_REF space_ref_table[MAX_TASKS];
static struct MM_SPACE_REF *space_ref_find(uint32_t pml4) {
    for (uint32_t i = 0; i < MAX_TASKS; i++) {
        if (space_ref_table[i].pml4 == pml4 && space_ref_table[i].refs != 0) {
            return &space_ref_table[i];
        }
    }
    return NULL;
}
void space_ref(uint32_t pml4) {
    if (pml4 == 0) {
        return;
    }
    uint32_t old = asm_save_eflags();
    asm_cli();
    struct MM_SPACE_REF *e = space_ref_find(pml4);
    if (e != NULL) {
        e->refs++;
    } else {
        for (uint32_t i = 0; i < MAX_TASKS; i++) {
            if (space_ref_table[i].refs == 0) {
                space_ref_table[i].pml4 = pml4;
                space_ref_table[i].refs = 1;
                break;
            }
        }
    }
    asm_restore_eflags(old);
}
static uint32_t space_unref(uint32_t pml4) {
    if (pml4 == 0) {
        return 0;
    }
    uint32_t old = asm_save_eflags();
    asm_cli();
    struct MM_SPACE_REF *e = space_ref_find(pml4);
    uint32_t left = 0;
    if (e != NULL) {
        e->refs--;
        left = e->refs;
        if (left == 0) {
            e->pml4 = 0;
        }
    }
    asm_restore_eflags(old);
    return left;
}
static void space_release_ex(uint32_t pml4_phys, struct TASK *owner, int release_bitmap) {
    if (pml4_phys != 0) {
        uint64_t *pml4 = phys_to_virt(pml4_phys);
        uint64_t pml4e = pml4[0];
        if (pml4e & 1) {
            uint64_t *pdp = phys_to_virt(PTE_PHYS(pml4e));
            for (uint32_t pdp_idx = 0; pdp_idx < 3; pdp_idx++) {
                uint64_t pdp_e = pdp[pdp_idx];
                if (!(pdp_e & 1) || (pdp_e & 0x80)) {
                    continue;
                }
                uint64_t *pd = phys_to_virt(PTE_PHYS(pdp_e));
                uint32_t pd_remaining = 0;
                for (uint32_t pd_idx = 0; pd_idx < 512; pd_idx++) {
                    uint64_t pd_e = pd[pd_idx];
                    if (!(pd_e & 1)) {
                        continue;
                    }
                    if (pd_e & 0x80) {
                        pd_remaining++;
                        continue;
                    }
                    uint64_t *pt = phys_to_virt(PTE_PHYS(pd_e));
                    uint32_t pt_remaining = 0;
                    for (uint32_t pte_idx = 0; pte_idx < 512; pte_idx++) {
                        if (!(pt[pte_idx] & 1)) {
                            continue;
                        }
                        uint64_t vaddr = ((uint64_t)pdp_idx << 30) + ((uint64_t)pd_idx << 21) +
                                         ((uint64_t)pte_idx << 12);
                        uint32_t bit = (uint32_t)((vaddr - USER_EXEC64_FLOOR) / PAGE_SIZE);
                        if (owner == NULL || vaddr < USER_EXEC64_FLOOR || vaddr >= 0xc0000000u ||
                            bit >= owner->userprog_v_addr.vaddr_bitmap.btmp_bytes_len * 8 ||
                            bitmap_scan_test(&owner->userprog_v_addr.vaddr_bitmap, bit) != 1) {
                            pt_remaining++;
                            continue;
                        }
                        page_free_or_decref_va((uint32_t)PTE_PHYS(pt[pte_idx]), (uint32_t)vaddr);
                        pt[pte_idx] = 0;
                    }
                    if (pt_remaining == 0) {
                        page_free_or_decref((uint32_t)PTE_PHYS(pd_e));
                        pd[pd_idx] = 0;
                    } else {
                        pd_remaining++;
                    }
                }
                if (pd_remaining == 0) {
                    page_free_or_decref((uint32_t)PTE_PHYS(pdp_e));
                    pdp[pdp_idx] = 0;
                }
            }
        }
        pfree(&kernel_pool, pml4_phys);
    }
    if (release_bitmap && owner != NULL && owner->userprog_v_addr.vaddr_bitmap.bits != NULL) {
        uint32_t bytes = owner->userprog_v_addr.vaddr_bitmap.btmp_bytes_len;
        uint32_t pg_cnt = (bytes + PAGE_SIZE - 1) / PAGE_SIZE;
        for (uint32_t i = 0; i < pg_cnt; i++) {
            free_kernel_page((uint32_t)(uintptr_t)owner->userprog_v_addr.vaddr_bitmap.bits +
                             i * PAGE_SIZE);
        }
        owner->userprog_v_addr.vaddr_bitmap.bits = NULL;
        owner->userprog_v_addr.vaddr_bitmap.btmp_bytes_len = 0;
    }
}
void space_detach_others(struct TASK *owner) {
    uint32_t pml4 = owner->pml4_phys;
    if (pml4 == 0) {
        return;
    }
    for (uint32_t i = 0; i < MAX_TASKS; i++) {
        struct TASK *t = &task_table[i];
        if (t == owner || !t->slot_used || t->status == TASK_DIED) {
            continue;
        }
        if (t->pml4_phys != pml4) {
            continue;
        }
        t->pml4_phys = 0;
        t->userprog_v_addr.vaddr_bitmap.bits = NULL;
        t->userprog_v_addr.vaddr_bitmap.btmp_bytes_len = 0;
        space_unref(pml4);
        list_unlink(&t->wait_tag);
        for (uint32_t fd_idx = 3; fd_idx < MAX_FILES_OPEN_PER_PROC; fd_idx++) {
            uint32_t g = t->fd_table[fd_idx];
            if (t->fd_owner_pid == (int32_t)t->pid && g != (uint32_t)-1 && g < MAX_FILE_OPEN) {
                if (file_table[g].ref_cnt > 0) {
                    file_table_unref(g);
                }
            }
            t->fd_table[fd_idx] = (uint32_t)-1;
        }
        thread_exit(t, 0);
    }
}
void task_release_space(struct TASK *t) {
    if (t == NULL || t->pml4_phys == 0) {
        return;
    }
    uint32_t pml4 = t->pml4_phys;
    t->pml4_phys = 0;
    t->userprog_v_addr.vaddr_bitmap.bits = NULL;
    t->userprog_v_addr.vaddr_bitmap.btmp_bytes_len = 0;
    if (space_unref(pml4) > 0) {
        return;
    }
    space_release_ex(pml4, t, 1);
}
void free_user_space(struct TASK *t, uint32_t pml4_phys) {
    if (t == NULL) {
        return;
    }
    int own = (t->pml4_phys == pml4_phys);
    if (own) {
        t->pml4_phys = 0;
    }
    space_release_ex(pml4_phys, t, own);
}
void create_user_vaddr_bitmap(struct TASK *user_prog) {
    user_prog->userprog_v_addr.vaddr_start = USER_EXEC64_FLOOR;
    uint32_t bitmap_pg_cnt =
        DIV_ROUND_UP((0xc0000000 - USER_EXEC64_FLOOR) / PAGE_SIZE / 8, PAGE_SIZE);
    user_prog->userprog_v_addr.vaddr_bitmap.bits = (uint8_t *)get_kernel_pages(bitmap_pg_cnt);
    user_prog->userprog_v_addr.vaddr_bitmap.btmp_bytes_len =
        (0xc0000000 - USER_EXEC64_FLOOR) / PAGE_SIZE / 8;
    bitmap_init(&user_prog->userprog_v_addr.vaddr_bitmap);
}
void process_execute(char *path, char *name) {
    struct TASK *thread = thread_alloc_slot(name, DEFAULT_PRIO);
    struct TASK_STACK *ts =
        (struct TASK_STACK *)(thread->kernel_stack_top - sizeof(struct TASK_STACK));
    ts->rflags = RFLAGS_INIT;
    ts->r15 = (uint64_t)start_process;
    ts->r14 = (uint64_t)path;
    ts->r13 = 0;
    ts->r12 = 0;
    ts->rbx = 0;
    ts->rbp = 0;
    ts->rip = kernel_thread_entry;
    create_user_vaddr_bitmap(thread);
    thread->pml4_phys = (uint32_t)(uintptr_t)create_page_dir();
    if (thread->pml4_phys == 0) {
        kprintf("[procexec] '%s' create_page_dir failed\n", path);
        thread_exit(thread, 0);
        return;
    }
    space_ref(thread->pml4_phys);
    thread->user_brk = 0;
    kprintf("[procexec] '%s' pid=%d pml4_phys=0x%x\n", path, thread->pid, thread->pml4_phys);
    thread_ready(thread);
}
