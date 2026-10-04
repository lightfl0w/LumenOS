#include "user/libc/stdio.h"
#include "user/libc/stdlib.h"
#include "syscall.h"
#define STACK_SIZE 0x4000
static volatile int shared;
static volatile int done;
static void wait_until(volatile int *v, int want) {
    while (*v != want) {
        futex((uint32_t)(uintptr_t)v, FUTEX_WAIT, want == 1 ? 0 : 1, 0);
    }
}
static void notify(volatile int *v, int want) {
    *v = want;
    futex((uint32_t)(uintptr_t)v, FUTEX_WAKE, 1, 0);
}
static int thread_share(void *arg) {
    shared = 200;
    notify(&done, 1);
    exit(0);
    return 0;
}
static int thread_add(void *arg) {
    shared += (int)(intptr_t)arg;
    if ((int)(intptr_t)arg == 5) {
        notify(&done, 1);
    }
    exit(0);
    return 0;
}
static int thread_copy(void *arg) {
    (void)arg;
    shared = 77;
    exit(0);
    return 0;
}
int main(void) {
    char *stk = (char *)malloc(STACK_SIZE);
    char *stk2 = (char *)malloc(STACK_SIZE);
    char *stk3 = (char *)malloc(STACK_SIZE);
    int32_t st = 0;
    int fail = 0;
    shared = 0;
    done = 0;
    int32_t tid = clone(thread_share, stk + STACK_SIZE,
                        CLONE_VM | CLONE_FS | CLONE_FILES | CLONE_THREAD,
                        (void *)0);
    if (tid < 0) {
        printf("FAIL: clone(CLONE_VM) returned %d\n", (int)tid);
        fail = 1;
    } else {
        wait_until(&done, 1);
        if (shared == 200) {
            printf("PASS: CLONE_VM child-exit-first shared=%d\n", shared);
        } else {
            printf("FAIL: shared=%d (want 200)\n", shared);
            fail = 1;
        }
    }
    shared = 0;
    tid = clone(thread_copy, stk2 + STACK_SIZE, CLONE_FS | CLONE_FILES,
                (void *)0);
    if (tid < 0) {
        printf("FAIL: clone(fork-like) returned %d\n", (int)tid);
        fail = 1;
    } else {
        if (wait(&st) != tid) {
            printf("FAIL: wait did not reap child\n");
            fail = 1;
        }
        if (shared == 0) {
            printf("PASS: no-CLONE_VM copy isolated shared=%d\n", shared);
        } else {
            printf("FAIL: parent saw child write shared=%d\n", shared);
            fail = 1;
        }
    }
    shared = 0;
    done = 0;
    int32_t t1 = clone(thread_add, stk + STACK_SIZE, CLONE_VM | CLONE_THREAD,
                       (void *)10);
    int32_t t2 = clone(thread_add, stk3 + STACK_SIZE, CLONE_VM | CLONE_THREAD,
                       (void *)5);
    if (t1 < 0 || t2 < 0) {
        printf("FAIL: thread clones returned %d/%d\n", (int)t1, (int)t2);
        fail = 1;
    } else {
        wait_until(&done, 1);
        if (shared == 15) {
            printf("PASS: two CLONE_VM threads shared=%d\n", shared);
        } else {
            printf("FAIL: shared=%d (want 15)\n", shared);
            fail = 1;
        }
    }
    if (fail) {
        exit(1);
    }
    printf("clone_stress: ALL PASS\n");
    exit(0);
    return 0;
}
