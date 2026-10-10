#include "kernel/userprog/wait_exit.h"
#include "fs/file.h"
#include "lib/assert.h"
#include "kernel/sched/thread.h"
#include "kernel/signal.h"
#include "kernel/syscall/futex.h"
#include "kernel/userprog/process.h"
#include "lib/list/list.h"
#include "mm/access.h"
#include "mm/bitmap.h"
#include "mm/pool.h"
#include "uapi/linux_abi.h"

static void clear_child_tid_of(struct TASK *t) {
    uint32_t addr = t->clear_child_tid;
    if (addr == 0) {
        return;
    }
    t->clear_child_tid = 0;
    if (!access_ok((const void *)(uintptr_t)addr, 4, 1)) {
        return;
    }
    *(volatile int32_t *)(uintptr_t)addr = 0;
    if (t == current) {
        sys_futex(addr, FUTEX_WAKE, 0x7FFFFFFF, 0, 0, 0);
    }
}

static void close_owned_fds(struct TASK *t) {
    if (t->fd_owner_pid != (int32_t)t->pid) {
        return;
    }
    for (uint32_t fd = 3; fd < MAX_FILES_OPEN_PER_PROC; fd++) {
        if (t->fd_table[fd] != (uint32_t)-1) {
            close_file((int)fd);
        }
    }
}

static void release_prog_resource(struct TASK *release_thread) {
    clear_child_tid_of(release_thread);
    task_release_space(release_thread);
    close_owned_fds(release_thread);
}

void kill_orphan_children(int32_t parent_pid) {
    struct TASK *victims[MAX_TASKS];
    uint32_t n = 0;
    uint32_t f = thread_all_lock();
    struct LIST_ELEM *e = thread_all_list.head.next;
    while (e != &thread_all_list.tail && n < MAX_TASKS) {
        struct TASK *t = list_entry(e, struct TASK, all_list_tag);
        if (t->parent_pid == parent_pid && t->status != TASK_DIED)
            victims[n++] = t;
        e = e->next;
    }
    thread_all_unlock(f);

    for (uint32_t i = 0; i < n; i++) {
        struct TASK *t = victims[i];
        if (t->status != TASK_DIED && t->status != TASK_HANGING)
            release_prog_resource(t);
        thread_exit(t, 0);
    }
}

static void find_child(struct TASK *parent, int32_t want_pid, struct TASK **out_hanging,
                       int *out_any) {
    struct TASK *hanging = NULL;
    int any = 0;
    uint32_t f = thread_all_lock();
    struct LIST_ELEM *e = thread_all_list.head.next;
    while (e != &thread_all_list.tail) {
        struct TASK *t = list_entry(e, struct TASK, all_list_tag);
        e = e->next;
        if (t->parent_pid != (int32_t)parent->pid) {
            continue;
        }
        if (want_pid > 0 && (int32_t)t->pid != want_pid) {
            continue;
        }
        any = 1;
        if (t->status == TASK_HANGING) {
            hanging = t;
            break;
        }
    }
    thread_all_unlock(f);
    *out_hanging = hanging;
    *out_any = any;
}

static pid_t reap_child(struct TASK *hanging, int32_t *status) {
    if (hanging == NULL) {
        return -1;
    }
    *status = hanging->exit_status;
    uint32_t child_pid = hanging->pid;
    thread_exit(hanging, 0);
    return (pid_t)child_pid;
}

pid_t sys_wait(int32_t *status) {
    struct TASK *parent = current;
    int32_t ignored_status;
    if (status == NULL) {
        status = &ignored_status;
    }
    for (;;) {
        struct TASK *hanging = NULL;
        int have_child = 0;
        find_child(parent, -1, &hanging, &have_child);
        pid_t reaped = reap_child(hanging, status);
        if (reaped >= 0) {
            return reaped;
        }
        if (!have_child) {
            return -1;
        }
        thread_block_with_status(TASK_WAITING);
    }
}

pid_t sys_wait4(int32_t pid, int32_t *status, uint32_t options) {
    struct TASK *parent = current;
    int32_t ignored_status;
    if (status == NULL) {
        status = &ignored_status;
    }
    for (;;) {
        struct TASK *hanging = NULL;
        int have_child = 0;
        find_child(parent, pid, &hanging, &have_child);
        pid_t reaped = reap_child(hanging, status);
        if (reaped >= 0) {
            return reaped;
        }
        if (!have_child) {
            return -1;
        }
        if (options & WAIT_WNOHANG) {
            return 0;
        }
        thread_block_with_status(TASK_WAITING);
    }
}

void proc_exit(struct TASK *cur, int status) {
    cur->exit_status = status;

    kill_orphan_children((int32_t)cur->pid);
    release_prog_resource(cur);
    struct TASK *parent = pid2thread(cur->parent_pid);
    if (parent && parent->status == TASK_WAITING) {
        thread_unblock(parent);
    }
    signal_notify_child_exit(parent);
    if (cur->parent_pid < 0) {
        thread_exit_current();
        return;
    }
    thread_block_with_status(TASK_HANGING);
}

void sys_exit(int32_t status) {
    proc_exit(current, status);
}

static int waitid_match(struct TASK *t, int idtype, int32_t id) {
    if (idtype == LINUX_P_PID) {
        return t->pid == (uint32_t)id;
    }
    if (idtype == LINUX_P_PGID) {
        return (t->pgid ? t->pgid : t->pid) == (uint32_t)id;
    }
    return 1;
}

int sys_waitid(int idtype, int32_t id, struct LINUX_SIGINFO *info, uint32_t options) {
    struct TASK *parent = current;
    for (;;) {
        int any_child = 0;
        struct TASK *target = NULL;
        uint32_t f = thread_all_lock();
        struct LIST_ELEM *e = thread_all_list.head.next;
        while (e != &thread_all_list.tail) {
            struct TASK *t = list_entry(e, struct TASK, all_list_tag);
            if (t->parent_pid != (int32_t)parent->pid || t->status == TASK_DIED) {
                e = e->next;
                continue;
            }
            any_child = 1;
            if (waitid_match(t, idtype, id) && t->status == TASK_HANGING) {
                target = t;
                break;
            }
            e = e->next;
        }
        thread_all_unlock(f);
        if (target != NULL) {
            if (info != NULL) {
                int sig = target->exit_status >= 128 ? target->exit_status - 128 : 0;
                info->si_signo = SIGCHLD;
                info->si_errno = 0;
                info->si_code = sig ? LINUX_CLD_KILLED : LINUX_CLD_EXITED;
                info->si_pid = (int32_t)target->pid;
                info->si_uid = 0;
                info->si_status = sig ? sig : target->exit_status;
                info->si_utime = target->elapsed_ticks;
                info->si_stime = 0;
            }
            if (!(options & LINUX_WNOWAIT)) {
                thread_exit(target, 0);
            }
            return 0;
        }
        if (!any_child) {
            return -1;
        }
        if (options & LINUX_WNOHANG) {
            if (info != NULL) {
                info->si_signo = 0;
            }
            return 0;
        }
        thread_block_with_status(TASK_WAITING);
    }
}
