#ifndef UAPI_FS_H
#define UAPI_FS_H

#include <stddef.h>
#include <stdint.h>

#define MAX_FILE_NAME_LEN 256
#define MAX_PATH_LEN 512

enum FS_FILE_TYPE { FT_UNKNOWN, FT_REGULAR, FT_DIRECTORY, FT_CHARDEVICE, FT_SYMLINK };

enum FS_OFLAGS { O_RDONLY, O_WRONLY, O_RDWR, O_CREAT = 4 };

enum FS_WHENCE { SEEK_SET = 1, SEEK_CUR, SEEK_END };

struct FS_STAT {
    uint32_t st_ino;
    uint32_t st_size;
    enum FS_FILE_TYPE st_filetype;
};

struct FS_DIRENT {
    char filename[MAX_FILE_NAME_LEN];
    uint32_t i_no;
    enum FS_FILE_TYPE f_type;
};

struct FS_DIR;

#endif
