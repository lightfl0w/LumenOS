#include "fs/devfs.h"
#include "fs/chardev.h"
#include "fs/file.h"
#include "fs/fs.h"
#include "fs/proc.h"
#include "kernel/sched/thread.h"
#include "lib/string/str.h"
#include "uapi/linux_abi.h"
#include <stdint.h>

#define DEVFS_MAX_NODES 32
#define DEVFS_PATH_MAX 64

struct DEVFS_NODE {
    uint8_t used;
    uint8_t is_dir;
    char path[DEVFS_PATH_MAX];
    uint32_t major;
    uint32_t minor;
};

static struct DEVFS_NODE devfs_nodes[DEVFS_MAX_NODES];
static uint32_t devfs_node_count;

static int devfs_prefix(const char *path) {
    if (path == NULL) {
        return 0;
    }
    if (strcmp(path, "/dev/input") == 0 || strncmp(path, "/dev/input/", 11) == 0) {
        return 1;
    }
    if (strcmp(path, "/dev/dri") == 0 || strncmp(path, "/dev/dri/", 9) == 0) {
        return 1;
    }
    return 0;
}

int devfs_match(const char *path) {
    return devfs_prefix(path);
}

static int parent_of(const char *path, char *out, uint32_t cap) {
    const char *slash = NULL;
    for (const char *p = path; *p; p++) {
        if (*p == '/') {
            slash = p;
        }
    }
    if (slash == NULL || slash == path) {
        return -1;
    }
    uint32_t len = (uint32_t)(slash - path);
    if (len >= cap) {
        return -1;
    }
    memcpy(out, path, len);
    out[len] = '\0';
    return 0;
}

static int devfs_is_dir_path(const char *path) {
    if (strcmp(path, "/dev/input") == 0 || strcmp(path, "/dev/dri") == 0) {
        return 1;
    }
    for (uint32_t i = 0; i < devfs_node_count; i++) {
        const struct DEVFS_NODE *n = &devfs_nodes[i];
        if (n->is_dir && strcmp(n->path, path) == 0) {
            return 1;
        }
    }
    return 0;
}

static struct DEVFS_NODE *devfs_find_exact(const char *path) {
    for (uint32_t i = 0; i < devfs_node_count; i++) {
        struct DEVFS_NODE *n = &devfs_nodes[i];
        if (n->used && strcmp(n->path, path) == 0) {
            return n;
        }
    }
    return NULL;
}

static int devfs_add_node(const char *path, int is_dir, uint32_t major, uint32_t minor) {
    for (uint32_t i = 0; i < DEVFS_MAX_NODES; i++) {
        struct DEVFS_NODE *n = &devfs_nodes[i];
        if (n->used) {
            continue;
        }
        memset(n, 0, sizeof(*n));
        n->used = 1;
        n->is_dir = (uint8_t)is_dir;
        strcpy(n->path, path);
        n->major = major;
        n->minor = minor;
        if (i >= devfs_node_count) {
            devfs_node_count = i + 1;
        }
        return 0;
    }
    return -1;
}

int devfs_add_char(const char *path, uint32_t major, uint32_t minor) {
    if (path == NULL || !devfs_prefix(path) || strlen(path) >= DEVFS_PATH_MAX) {
        return -1;
    }
    if (devfs_find_exact(path) != NULL) {
        return 0;
    }
    if (devfs_add_node(path, 0, major, minor) < 0) {
        return -1;
    }
    char parent[DEVFS_PATH_MAX];
    if (parent_of(path, parent, sizeof(parent)) == 0 && devfs_find_exact(parent) == NULL) {
        devfs_add_node(parent, 1, 0, 0);
    }
    return 0;
}

int devfs_open(const char *path, uint8_t flags) {
    struct DEVFS_NODE *node = devfs_find_exact(path);
    int is_dir = (node != NULL && node->is_dir) || (node == NULL && devfs_is_dir_path(path));
    if (node == NULL && !is_dir) {
        current->errno = 2;
        return -1;
    }
    int gfd = file_table_alloc_slot();
    if (gfd == -1) {
        current->errno = 24;
        return -1;
    }
    struct FILE *file = file_get((uint32_t)gfd);
    memset(file, 0, sizeof(*file));
    file->fd_pos = 0;
    file->ref_cnt = 1;
    file->fd_inode = NULL;
    if (node != NULL && node->is_dir) {
        file->fd_flag = PROC_DIRF_FLAG;
        file->devfs_node = (uint32_t)(node - devfs_nodes) + 1;
    } else if (is_dir) {
        file->fd_flag = PROC_DIRF_FLAG;
        file->devfs_node = (uint32_t)-1;
    } else {
        file->fd_flag = flags;
        file->devfs_node = (uint32_t)(node - devfs_nodes) + 1;
        file->dev_major = (uint16_t)node->major;
        file->dev_minor = (uint16_t)node->minor;
        const struct CHARDEV_OPS *ops = chardev_lookup(node->major);
        if (ops != NULL && ops->open != NULL &&
            ops->open(file, (node->major << 8) | node->minor) < 0) {
            file_table_free_slot(gfd);
            current->errno = 16;
            return -1;
        }
    }
    int fd = fd_install(gfd);
    if (fd == -1) {
        file_table_free_slot(gfd);
        current->errno = 24;
        return -1;
    }
    return fd;
}

static struct DEVFS_NODE *devfs_node_of(const struct FILE *file) {
    if (file == NULL || file->devfs_node == 0 || file->devfs_node == (uint32_t)-1) {
        return NULL;
    }
    uint32_t idx = file->devfs_node - 1;
    if (idx >= DEVFS_MAX_NODES || !devfs_nodes[idx].used) {
        return NULL;
    }
    return &devfs_nodes[idx];
}

uint32_t devfs_read(struct FILE *file, void *buf, uint32_t count) {
    struct DEVFS_NODE *node = devfs_node_of(file);
    if (node == NULL) {
        return 0;
    }
    const struct CHARDEV_OPS *ops = chardev_lookup(node->major);
    if (ops == NULL || ops->read == NULL) {
        return 0;
    }
    int32_t r = ops->read(file, buf, (int32_t)count);
    return r < 0 ? 0 : (uint32_t)r;
}

uint32_t devfs_write(struct FILE *file, const void *buf, uint32_t count) {
    struct DEVFS_NODE *node = devfs_node_of(file);
    if (node == NULL) {
        return count;
    }
    const struct CHARDEV_OPS *ops = chardev_lookup(node->major);
    if (ops == NULL || ops->write == NULL) {
        return count;
    }
    int32_t r = ops->write(file, buf, (int32_t)count);
    return r < 0 ? 0 : (uint32_t)r;
}

int32_t devfs_lseek(struct FILE *file, int32_t offset, uint8_t whence) {
    (void)file;
    (void)offset;
    (void)whence;
    return -29;
}

static void devfs_fill_stat(struct FS_STAT *buf, int is_dir, uint32_t ino) {
    memset(buf, 0, sizeof(*buf));
    buf->st_ino = ino;
    buf->st_filetype = is_dir ? FT_DIRECTORY : FT_CHARDEVICE;
}

int devfs_stat(const char *path, struct FS_STAT *buf) {
    struct DEVFS_NODE *node = devfs_find_exact(path);
    if (node != NULL) {
        devfs_fill_stat(buf, node->is_dir, (uint32_t)(node - devfs_nodes) + 1);
        return 0;
    }
    if (devfs_is_dir_path(path)) {
        devfs_fill_stat(buf, 1, 1);
        return 0;
    }
    current->errno = 2;
    return -1;
}

int devfs_fstat(struct FILE *file, struct FS_STAT *buf) {
    if (devfs_node_of(file) == NULL) {
        devfs_fill_stat(buf, 1, 1);
        return 0;
    }
    devfs_fill_stat(buf, 0, file->devfs_node);
    return 0;
}

static const char *const devfs_roots[] = {"/dev/input", "/dev/dri"};

static uint16_t dirent64_len(uint32_t name_len) {
    return (uint16_t)((19u + name_len + 1u + 7u) & ~7u);
}

static void dirent64_fill(struct LINUX_DIRENT64 *d, uint32_t ino, uint32_t off, uint8_t type,
                          const char *name) {
    uint32_t nl = strlen(name);
    d->d_ino = ino;
    d->d_off = (int64_t)off;
    d->d_reclen = dirent64_len(nl);
    d->d_type = type;
    memcpy(d->d_name, name, nl + 1);
}

static int32_t devfs_getdents_root(struct FILE *file, void *dirp, uint32_t count) {
    uint32_t pos = file->fd_pos;
    if (pos >= 2) {
        return 0;
    }
    const char *path = devfs_roots[pos];
    uint16_t reclen = dirent64_len(strlen(path) - strlen("/dev/"));
    if (reclen > count) {
        return -22;
    }
    dirent64_fill((struct LINUX_DIRENT64 *)dirp, 1, pos + 1, LINUX_DT_DIR, path + strlen("/dev/"));
    file->fd_pos = pos + 1;
    return (int32_t)reclen;
}

static int32_t devfs_getdents_children(struct FILE *file, const char *dirpath, void *dirp,
                                       uint32_t count) {
    uint32_t written = 0;
    uint32_t skip = file->fd_pos;
    uint32_t dlen = strlen(dirpath);
    for (uint32_t i = 0; i < devfs_node_count; i++) {
        const struct DEVFS_NODE *n = &devfs_nodes[i];
        if (!n->used || n->is_dir) {
            continue;
        }
        if (strncmp(n->path, dirpath, dlen) != 0 || n->path[dlen] != '/') {
            continue;
        }
        const char *name = n->path + dlen + 1;
        if (strchr(name, '/') != NULL) {
            continue;
        }
        if (skip > 0) {
            skip--;
            continue;
        }
        uint16_t reclen = dirent64_len(strlen(name));
        if (written + reclen > count) {
            break;
        }
        dirent64_fill((struct LINUX_DIRENT64 *)((uint8_t *)dirp + written), i + 1,
                      file->fd_pos + 1, LINUX_DT_CHR, name);
        written += reclen;
        file->fd_pos++;
    }
    return (int32_t)written;
}

int32_t devfs_getdents64(struct FILE *file, void *dirp, uint32_t count) {
    struct DEVFS_NODE *self = devfs_node_of(file);
    if (self != NULL && self->is_dir) {
        return devfs_getdents_children(file, self->path, dirp, count);
    }
    if (file->devfs_node == (uint32_t)-1) {
        return devfs_getdents_root(file, dirp, count);
    }
    return -20;
}

void devfs_close(struct FILE *file) {
    struct DEVFS_NODE *node = devfs_node_of(file);
    if (node == NULL) {
        return;
    }
    const struct CHARDEV_OPS *ops = chardev_lookup(node->major);
    if (ops != NULL && ops->release != NULL) {
        ops->release(file);
    }
}

int devfs_lookup_mm(uint32_t major, uint32_t minor, uint32_t *node_id) {
    for (uint32_t i = 0; i < devfs_node_count; i++) {
        const struct DEVFS_NODE *n = &devfs_nodes[i];
        if (n->used && !n->is_dir && n->major == major && n->minor == minor) {
            *node_id = i + 1;
            return 0;
        }
    }
    return -1;
}

const char *devfs_node_path(uint32_t node_id) {
    if (node_id == 0 || node_id > devfs_node_count) {
        return NULL;
    }
    return devfs_nodes[node_id - 1].path;
}

uint32_t devfs_node_major(uint32_t node_id) {
    if (node_id == 0 || node_id > devfs_node_count) {
        return 0;
    }
    return devfs_nodes[node_id - 1].major;
}

uint32_t devfs_node_minor(uint32_t node_id) {
    if (node_id == 0 || node_id > devfs_node_count) {
        return 0;
    }
    return devfs_nodes[node_id - 1].minor;
}
