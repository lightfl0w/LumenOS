#include "kernel/ipc/unix_sock.h"

#include "arch/time/pit.h"
#include "fs/file.h"
#include "fs/fs.h"
#include "arch/x86_64/percpu.h"
#include "kernel/sched/thread.h"
#include "lib/printf/printf.h"
#include "lib/string/str.h"
#include "mm/pool.h"

#include <stdint.h>

#define UNIX_MAX_SOCK 64
#define UNIX_BUF_SIZE 16384
#define UNIX_MSG_RING 64
#define UNIX_MAX_FDS 28
#define UNIX_BACKLOG 8
#define UNIX_PATH_MAX 96

#define US_UNCONNECTED 0
#define US_LISTENING 1
#define US_CONNECTED 2

struct UNIX_MSG {
    uint32_t len;
    uint8_t nfds;
    int gfds[UNIX_MAX_FDS];
};

struct UNIX_SOCK {
    uint8_t used;
    uint8_t state;
    uint8_t nonblock;
    int8_t peer;
    int32_t refs;
    char path[UNIX_PATH_MAX];

    int pending[UNIX_BACKLOG];
    int pend_count;

    uint8_t buf[UNIX_BUF_SIZE];
    uint32_t head;
    uint32_t count;

    struct UNIX_MSG msgs[UNIX_MSG_RING];
    uint32_t mhead;
    uint32_t mcount;
    struct UNIX_MSG cur;
    uint8_t cur_valid;
};

static struct UNIX_SOCK us[UNIX_MAX_SOCK];

#define UNIX_MAX_OWNERS 16
struct UNIX_OWNER {
    int used;
    int32_t pid;
    uint32_t mask;
};
static struct UNIX_OWNER owners[UNIX_MAX_OWNERS];

static int32_t unix_cur_pid(void) {
    return (int32_t)current->pid;
}

static struct UNIX_OWNER *unix_owner_find(int32_t pid, int create) {
    struct UNIX_OWNER *free_slot = NULL;
    for (int i = 0; i < UNIX_MAX_OWNERS; i++) {
        if (owners[i].used && owners[i].pid == pid) {
            return &owners[i];
        }
        if (!owners[i].used && free_slot == NULL) {
            free_slot = &owners[i];
        }
    }
    if (!create) {
        return NULL;
    }
    if (free_slot != NULL) {
        free_slot->used = 1;
        free_slot->pid = pid;
        free_slot->mask = 0;
        return free_slot;
    }
    return NULL;
}

static void unix_owner_drop(int32_t pid) {
    for (int i = 0; i < UNIX_MAX_OWNERS; i++) {
        if (owners[i].used && owners[i].pid == pid) {
            owners[i].used = 0;
            return;
        }
    }
}

void unix_fd_acquire(int32_t pid, uint64_t fd) {
    int idx = unix_fd_slot(fd);
    if (idx < 0) {
        return;
    }
    struct UNIX_OWNER *o = unix_owner_find(pid, 1);
    if (o != NULL && (o->mask & (1u << idx)) == 0) {
        o->mask |= 1u << idx;
        us[idx].refs++;
    }
}

void unix_fd_release(int32_t pid, uint64_t fd) {
    int idx = unix_fd_slot(fd);
    if (idx < 0) {
        return;
    }
    struct UNIX_OWNER *o = unix_owner_find(pid, 0);
    if (o == NULL || (o->mask & (1u << idx)) == 0) {
        unix_close_slot(idx);
        return;
    }
    o->mask &= ~(1u << idx);
    if (us[idx].refs > 0) {
        us[idx].refs--;
    }
    if (us[idx].refs == 0) {
        unix_close_slot(idx);
    }
}

void unix_fds_dup(int32_t parent_pid, int32_t child_pid) {
    struct UNIX_OWNER *p = unix_owner_find(parent_pid, 0);
    if (p == NULL) {
        return;
    }
    for (int idx = 0; idx < UNIX_MAX_SOCK; idx++) {
        if ((p->mask & (1u << idx)) == 0) {
            continue;
        }
        struct UNIX_OWNER *c = unix_owner_find(child_pid, 1);
        if (c == NULL) {
            break;
        }
        c->mask |= 1u << idx;
        us[idx].refs++;
    }
}

void unix_fds_exit(int32_t pid) {
    struct UNIX_OWNER *o = unix_owner_find(pid, 0);
    if (o == NULL) {
        return;
    }
    for (int idx = 0; idx < UNIX_MAX_SOCK; idx++) {
        if ((o->mask & (1u << idx)) == 0) {
            continue;
        }
        if (us[idx].refs > 0) {
            us[idx].refs--;
        }
        if (us[idx].refs == 0) {
            unix_close_slot(idx);
        }
    }
    unix_owner_drop(pid);
}

static struct UNIX_SOCK *sock(int idx) {
    if (idx < 0 || idx >= UNIX_MAX_SOCK || !us[idx].used) {
        return NULL;
    }
    return &us[idx];
}

int unix_fd_slot(uint64_t fd) {
    uint64_t idx = fd - UNIX_FD_BASE;
    if (fd < UNIX_FD_BASE || idx >= UNIX_MAX_SOCK || !us[idx].used) {
        return -1;
    }
    return (int)idx;
}

static int alloc_slot(void) {
    for (int i = 0; i < UNIX_MAX_SOCK; i++) {
        if (!us[i].used) {
            memset(&us[i], 0, sizeof(us[i]));
            us[i].used = 1;
            us[i].peer = -1;
            return i;
        }
    }
    return -1;
}

int unix_socket_create(int type) {
    (void)type;
    int i = alloc_slot();
    return i;
}

int unix_socketpair(int type, int out[2]) {
    (void)type;
    int a = alloc_slot();
    int b = a >= 0 ? alloc_slot() : -1;
    if (a < 0 || b < 0) {
        return -1;
    }
    us[a].state = US_CONNECTED;
    us[b].state = US_CONNECTED;
    us[a].peer = (int8_t)b;
    us[b].peer = (int8_t)a;
    out[0] = UNIX_FD_BASE + a;
    out[1] = UNIX_FD_BASE + b;
    return 0;
}

int unix_path_bound(const char *path) {
    for (int i = 0; i < UNIX_MAX_SOCK; i++) {
        if (us[i].used && us[i].path[0] != 0 && strcmp(us[i].path, path) == 0) {
            return i;
        }
    }
    return -1;
}

int unix_bind(int idx, const char *path) {
    struct UNIX_SOCK *s = sock(idx);
    if (s == NULL || path == NULL || path[0] != '/') {
        return -1;
    }
    if (strlen(path) >= UNIX_PATH_MAX) {
        return -1;
    }
    if (unix_path_bound(path) >= 0) {
        return -1;
    }
    strcpy(s->path, path);
    return 0;
}

int unix_listen(int idx) {
    struct UNIX_SOCK *s = sock(idx);
    if (s == NULL || s->path[0] == 0 || s->state != US_UNCONNECTED) {
        return -1;
    }
    s->state = US_LISTENING;
    return 0;
}

int unix_connect(int idx, const char *path) {
    struct UNIX_SOCK *c = sock(idx);
    if (c == NULL || c->state != US_UNCONNECTED) {
        return -1;
    }
    int l = unix_path_bound(path);
    struct UNIX_SOCK *ls = l >= 0 ? sock(l) : NULL;
    if (ls == NULL || ls->state != US_LISTENING) {
        return -1;
    }
    int sidx = alloc_slot();
    if (sidx < 0) {
        return -1;
    }
    struct UNIX_SOCK *srv = &us[sidx];
    srv->state = US_CONNECTED;
    srv->peer = (int8_t)idx;
    srv->path[0] = 0;
    c->state = US_CONNECTED;
    c->peer = (int8_t)sidx;

    if (ls->pend_count >= UNIX_BACKLOG) {
        return -1;
    }
    ls->pending[ls->pend_count++] = sidx;
    return 0;
}

int unix_accept(int idx) {
    struct UNIX_SOCK *s = sock(idx);
    if (s == NULL || s->state != US_LISTENING) {
        return -1;
    }
    for (;;) {
        if (s->pend_count > 0) {
            break;
        }
        if (s->nonblock) {
            return -1;
        }
        mtime_sleep(2);
        if (sock(idx) == NULL) {
            return -1;
        }
    }
    int nidx = s->pending[0];
    for (int i = 1; i < s->pend_count; i++) {
        s->pending[i - 1] = s->pending[i];
    }
    s->pend_count--;
    return nidx;
}

int unix_peer_alive(int idx) {
    struct UNIX_SOCK *s = sock(idx);
    if (s == NULL) {
        return 0;
    }
    if (s->state != US_CONNECTED) {
        return 0;
    }
    struct UNIX_SOCK *p = sock(s->peer);
    return p != NULL;
}

void unix_set_nonblock(int idx, int on) {
    struct UNIX_SOCK *s = sock(idx);
    if (s != NULL) {
        s->nonblock = (uint8_t)(on ? 1 : 0);
    }
}

static uint32_t byte_space(const struct UNIX_SOCK *s) {
    return UNIX_BUF_SIZE - s->count;
}

static void bytes_push(struct UNIX_SOCK *s, const uint8_t *src, uint32_t len) {
    for (uint32_t i = 0; i < len; i++) {
        s->buf[(s->head + s->count + i) % UNIX_BUF_SIZE] = src[i];
    }
    s->count += len;
}

static void bytes_pop(struct UNIX_SOCK *s, uint8_t *dst, uint32_t len) {
    for (uint32_t i = 0; i < len; i++) {
        dst[i] = s->buf[(s->head + i) % UNIX_BUF_SIZE];
    }
    s->head = (s->head + len) % UNIX_BUF_SIZE;
    s->count -= len;
}

static void drop_gfd(int gfd) {
    if (file_table_unref((uint32_t)gfd) == 0) {
        struct FILE *f = file_get((uint32_t)gfd);
        if (f != NULL && f->fd_inode != NULL) {
            inode_close(f->fd_inode);
        }
        file_table_free_slot(gfd);
    }
}

static int32_t queue_message(struct UNIX_SOCK *p, const void *buf, uint32_t len, const int *gfds,
                             int nfds) {
    if (len > UNIX_BUF_SIZE || nfds > UNIX_MAX_FDS || p->mcount >= UNIX_MSG_RING) {
        return -1;
    }
    if (byte_space(p) < len) {
        return -1;
    }
    struct UNIX_MSG *m = &p->msgs[(p->mhead + p->mcount) % UNIX_MSG_RING];
    m->len = len;
    m->nfds = (uint8_t)nfds;
    for (int i = 0; i < nfds; i++) {
        m->gfds[i] = gfds[i];
    }
    bytes_push(p, buf, len);
    p->mcount++;
    return (int32_t)len;
}

int32_t unix_send_data(int idx, const void *buf, uint32_t len, const int *gfds, int nfds) {
    struct UNIX_SOCK *s = sock(idx);
    if (s == NULL || s->state != US_CONNECTED || s->peer < 0) {
        return -32;
    }
    struct UNIX_SOCK *p = sock(s->peer);
    if (p == NULL) {
        return -32;
    }
    for (;;) {
        int32_t rc = queue_message(p, buf, len, gfds, nfds);
        if (rc >= 0) {
            return rc;
        }
        if (s->nonblock || !sock(idx) || !sock(s->peer)) {
            return -11;
        }
        mtime_sleep(2);
    }
}

int32_t unix_recv_data(int idx, void *buf, uint32_t len, int *gfds, int *nfds) {
    struct UNIX_SOCK *s = sock(idx);
    if (s == NULL) {
        return -9;
    }
    *nfds = 0;
    for (;;) {
        if (s->cur_valid || s->mcount > 0) {
            break;
        }
        if (s->nonblock) {
            return -11;
        }
        if (!unix_peer_alive(idx)) {
            return 0;
        }
        mtime_sleep(2);
        s = sock(idx);
        if (s == NULL) {
            return -9;
        }
    }

    if (!s->cur_valid) {
        s->cur = s->msgs[s->mhead];
        s->mhead = (s->mhead + 1) % UNIX_MSG_RING;
        s->mcount--;
        s->cur_valid = 1;
        for (int i = 0; i < s->cur.nfds; i++) {
            gfds[i] = s->cur.gfds[i];
        }
        *nfds = s->cur.nfds;
    }

    uint32_t n = len < s->cur.len ? len : s->cur.len;
    bytes_pop(s, buf, n);
    s->cur.len -= n;
    if (s->cur.len == 0) {
        s->cur_valid = 0;
    }
    return (int32_t)n;
}

int32_t unix_send(int idx, const void *buf, uint32_t len) {
    return unix_send_data(idx, buf, len, NULL, 0);
}

int32_t unix_recv(int idx, void *buf, uint32_t len) {
    int gfds[UNIX_MAX_FDS];
    int nfds = 0;
    int32_t n = unix_recv_data(idx, buf, len, gfds, &nfds);
    if (n > 0 && nfds > 0) {
        struct UNIX_SOCK *s = sock(idx);
        if (s != NULL) {
            for (int i = 0; i < nfds; i++) {
                drop_gfd(gfds[i]);
            }
        }
    }
    return n;
}

uint32_t unix_events(int idx) {
    struct UNIX_SOCK *s = sock(idx);
    if (s == NULL) {
        return 0;
    }
    uint32_t ev = 0;
    if (s->state == US_LISTENING) {
        if (s->pend_count > 0) {
            ev |= 1u;
        }
        return ev;
    }
    if (s->cur_valid || s->mcount > 0) {
        ev |= 1u;
    }
    if (s->state == US_CONNECTED && !unix_peer_alive(idx)) {
        ev |= 2u;
    }
    return ev;
}

void unix_close_slot(int idx) {
    struct UNIX_SOCK *s = sock(idx);
    if (s == NULL) {
        return;
    }
    for (uint32_t i = 0; i < s->mcount; i++) {
        struct UNIX_MSG *m = &s->msgs[(s->mhead + i) % UNIX_MSG_RING];
        for (int k = 0; k < m->nfds; k++) {
            drop_gfd(m->gfds[k]);
        }
    }
    if (s->cur_valid) {
        for (int k = 0; k < s->cur.nfds; k++) {
            drop_gfd(s->cur.gfds[k]);
        }
    }
    s->used = 0;
}
