#ifndef FS_EXT_COMMON_H
#define FS_EXT_COMMON_H

#include "fs/fs.h"
#include "uapi/fs.h"
#include "fs/pbcache.h"
#include "lib/printf/printf.h"
#include "lib/string/str.h"
#include <stdint.h>

typedef int (*ext_read_inode_fn)(uint32_t ino, struct FS_INODE *out);
typedef int (*ext_read_from_inode_fn)(const struct FS_INODE *ino, uint32_t off, void *buf,
                                      uint32_t count);

static inline int ext_read_link_target_with(ext_read_inode_fn read_inode,
                                            ext_read_from_inode_fn read_from_inode,
                                            uint32_t ino, char *buf, uint32_t cap) {
    if (cap == 0) {
        return -1;
    }
    struct FS_INODE node;
    if (read_inode(ino, &node) || (node.i_mode & 0xF000u) != 0xA000u) {
        return -1;
    }
    uint32_t len = node.i_size < cap - 1 ? node.i_size : cap - 1;
    if (node.i_size < 60u) {
        memcpy(buf, &node.i_block[0], len);
    } else if (read_from_inode(&node, 0, buf, len) != (int)len) {
        return -1;
    }
    buf[len] = 0;
    return (int)len;
}

typedef int (*ext_dir_next_fn)(const struct FS_INODE *dino, uint32_t *pos,
                               struct FS_DIRENT *out);

static inline int ext_find_in_dir_with(ext_dir_next_fn dir_next,
                                       const struct FS_INODE *dino, const char *name,
                                       uint32_t *child, int *ftype) {
    uint32_t pos = 0;
    struct FS_DIRENT de;
    while (dir_next(dino, &pos, &de) == 0) {
        if (strcmp(de.filename, name) == 0) {
            *child = de.i_no;
            *ftype = (int)de.f_type;
            return 0;
        }
    }
    return -1;
}

typedef int (*ext_read_link_target_fn)(uint32_t ino, char *buf, uint32_t cap);

struct EXT_PATH_OPS {
    ext_read_inode_fn read_inode;
    ext_dir_next_fn dir_next;
    ext_read_link_target_fn read_link_target;
    int mounted;
};

static inline int ext_lookup_depth_with(const struct EXT_PATH_OPS *ops, const char *path,
                                        uint32_t *ino, int *ftype, int follow, int depth) {
    if (!ops->mounted || path == NULL || path[0] != '/') {
        return -1;
    }
    char cur[MAX_PATH_LEN];
    uint32_t clen = (uint32_t)strlen(path);
    if (clen >= MAX_PATH_LEN) {
        return -1;
    }
    memcpy(cur, path, clen + 1);
    for (;;) {
        if (--depth < 0) {
            return -1;
        }
        uint32_t cino = 2;
        int cdir = 1;
        *ino = 2;
        *ftype = FT_DIRECTORY;
        const char *p = cur;
        while (*p == '/') {
            p++;
        }
        if (*p == 0) {
            return 0;
        }
        struct FS_INODE node;
        if (ops->read_inode(2, &node)) {
            return -1;
        }
        for (;;) {
            const char *cstart = p;
            char comp[MAX_FILE_NAME_LEN];
            uint32_t ci = 0;
            while (*p && *p != '/' && ci < MAX_FILE_NAME_LEN - 1) {
                comp[ci++] = *p++;
            }
            comp[ci] = 0;
            if (ci == 0) {
                break;
            }
            uint32_t child = 0;
            int ft = 0;
            if (ext_find_in_dir_with(ops->dir_next, &node, comp, &child, &ft)) {
                return -1;
            }
            if (ft == FT_SYMLINK) {
                char tgt[MAX_PATH_LEN];
                if (!follow || ops->read_link_target(child, tgt, MAX_PATH_LEN) < 0) {
                    *ino = child;
                    *ftype = ft;
                    cdir = 0;
                    return 0;
                }
                char np[MAX_PATH_LEN];
                uint32_t plen = (uint32_t)(cstart - cur);
                uint32_t tlen = (uint32_t)strlen(tgt);
                if (tgt[0] == '/') {
                    plen = 0;
                } else {
                    while (plen > 1 && cur[plen - 1] == '/') {
                        plen--;
                    }
                }
                if (plen + tlen + 2 > MAX_PATH_LEN) {
                    return -1;
                }
                memcpy(np, cur, plen);
                if (plen > 1 || (plen == 1 && cur[0] != '/')) {
                    np[plen++] = '/';
                } else if (plen == 0) {
                    np[plen++] = '/';
                }
                memcpy(np + plen, tgt, tlen + 1);
                memcpy(cur, np, plen + tlen + 1);
                break;
            }
            cino = child;
            cdir = (ft == FT_DIRECTORY);
            while (*p == '/') {
                p++;
            }
            if (*p == 0) {
                *ino = cino;
                *ftype = ft;
                return 0;
            }
            if (!cdir || ops->read_inode(child, &node)) {
                return -1;
            }
        }
        *ino = cino;
        *ftype = cdir ? FT_DIRECTORY : FT_REGULAR;
    }
}

static inline int ext_read_blocks_with(const char *tag, int mounted, uint32_t total_blocks,
                                       uint32_t start, uint32_t blk, uint32_t cnt, void *buf) {
    if (!mounted) {
        return -1;
    }
    if (blk >= total_blocks && total_blocks != 0) {
        kprintf("[%s] read out-of-range block %d (total %d)\n", tag, blk, total_blocks);
        return -1;
    }
    if (cnt == 0) {
        return 0;
    }
    if (blk + cnt > total_blocks && total_blocks != 0) {
        cnt = total_blocks - blk;
    }
    return pbc_read_run(start, blk, cnt, buf);
}

#endif
