#ifndef FS_VFS_VFS_H
#define FS_VFS_VFS_H

#include <stdint.h>

struct FS_INODE;
struct FS_DIRENT;
struct DISK_PARTITION;

#define VFS_MAX_MOUNTS 8
#define VFS_MNT_PATH_MAX 32

struct VFS_OPS {
    int (*init)(void);
    struct DISK_PARTITION *(*partition)(void);
    int (*lookup)(const char *path, uint32_t *ino, int *is_dir);
    int (*lookup_ftype)(const char *path, uint32_t *ino, int *ftype, int follow);
    int (*abs_path)(const char *path, char *out, uint32_t cap);
    int (*read_link_target)(uint32_t ino, char *buf, uint32_t cap);
    int (*read_inode)(uint32_t ino, struct FS_INODE *out);
    int (*read_from_inode)(const struct FS_INODE *ino, uint32_t off, void *buf, uint32_t count);
    int (*dir_next)(const struct FS_INODE *dino, uint32_t *pos, struct FS_DIRENT *out);
    int (*new_inode)(uint32_t mode, struct FS_INODE *out);
    void (*free_inode)(uint32_t ino);
    int (*write_inode)(uint32_t ino, const struct FS_INODE *in);
    int (*write_to_inode)(struct FS_INODE *ino, uint32_t off, const void *buf, uint32_t count);
    void (*truncate_inode)(struct FS_INODE *ino);
    int (*add_entry)(struct FS_INODE *dino, uint32_t ino, const char *name, int is_dir);
    int (*add_entry_dt)(struct FS_INODE *dino, uint32_t ino, const char *name, uint8_t dtype);
    int (*remove_entry)(struct FS_INODE *dino, const char *name);
    void (*statfs_info)(uint32_t *bsize, uint32_t *blocks, uint32_t *bfree, uint32_t *files,
                        uint32_t *ffree);
};

struct VFS_MOUNT {
    int active;
    char path[VFS_MNT_PATH_MAX];
    const struct VFS_OPS *ops;
};

struct FS_REGISTRATION {
    const char *name;
    const struct VFS_OPS *ops;

    int (*probe)(const uint8_t *sb);
};

extern const struct FS_REGISTRATION __vfs_fs_start[];
extern const struct FS_REGISTRATION __vfs_fs_end[];

#define VFS_REGISTER(name_str, ops_ptr, probe_fn)                                 \
    static const struct FS_REGISTRATION __vfs_##probe_fn                          \
        __attribute__((used, section(".vfs_fs"))) = {                             \
            .name = (name_str), .ops = (ops_ptr), .probe = (probe_fn) }

int vfs_init(void);
int vfs_mount(const char *path, const struct VFS_OPS *ops);
int vfs_unmount(const char *path);
const struct VFS_OPS *vfs_ops_for(const char *path);
int vfs_match_fs(const char *path, const struct VFS_OPS *ops);
const struct VFS_OPS *vfs_root_ops(void);
const struct VFS_MOUNT *vfs_mount_at(int idx);
const char *vfs_ops_name(const struct VFS_OPS *ops);

#include "fs/dir.h"
#include "fs/ext2.h"
#include "fs/ext4.h"
#include "fs/fs.h"
#include "fs/inode.h"
#include <stdint.h>

#define FS_DT_DIR 2u
#define FS_DT_CHR 3u
#define FS_DT_LNK 7u

int fs_init(void);
struct DISK_PARTITION *fs_partition(void);
int fs_lookup(const char *path, uint32_t *ino, int *is_dir);
int fs_lookup_ftype(const char *path, uint32_t *ino, int *ftype, int follow);
int fs_abs_path(const char *path, char *out, uint32_t cap);
int fs_read_link_target(uint32_t ino, char *buf, uint32_t cap);
int fs_read_inode(uint32_t ino, struct FS_INODE *out);
int fs_read_from_inode(const struct FS_INODE *ino, uint32_t off, void *buf, uint32_t count);
int fs_dir_next(const struct FS_INODE *dino, uint32_t *pos, struct FS_DIRENT *out);

int fs_new_inode(uint32_t mode, struct FS_INODE *out);
void fs_free_inode(uint32_t ino);
int fs_write_inode(uint32_t ino, const struct FS_INODE *in);
int fs_write_to_inode(struct FS_INODE *ino, uint32_t off, const void *buf, uint32_t count);
void fs_truncate_inode(struct FS_INODE *ino);
int fs_add_entry(struct FS_INODE *dino, uint32_t ino, const char *name, int is_dir);
int fs_add_entry_dt(struct FS_INODE *dino, uint32_t ino, const char *name, uint8_t dtype);
int fs_remove_entry(struct FS_INODE *dino, const char *name);

#ifndef __ASSEMBLER__
void fs_statfs_info(uint32_t *bsize, uint32_t *blocks, uint32_t *bfree, uint32_t *files,
                    uint32_t *ffree);
#endif

#endif
