#include "uapi/fs.h"
#include "user/libc/stdio.h"
#include "user/libc/syscall.h"
#include "lib/string/str.h"

static int64_t lc_stat(const char *path, void *st) {
    int64_t ret;
    __asm__ volatile("syscall"
                     : "=a"(ret)
                     : "a"(4L), "D"(path), "S"(st)
                     : "rcx", "r11", "memory");
    return ret;
}

int main(void) {
    printf("dirtest: start\n");

    struct FS_STAT st;
    int rc = stat("/dev/dri", &st);
    printf("  native stat rc=%d type=%d\n", rc, st.st_filetype);

    char lst[128];
    int64_t lrc = lc_stat("/dev/dri", lst);
    printf("  linux stat rc=%d mode=%x\n", (int)lrc, *(uint32_t *)(lst + 24));

    int fd = open("/dev/dri", O_RDONLY | 0x10000);
    printf("  open rc=%d\n", fd);
    if (fd < 0) {
        printf("dirtest: done\n");
        return 0;
    }

    struct LINUX_DIRENT d;
    int32_t n = getdents(fd, &d, sizeof(d));
    printf("  getdents rc=%d\n", n);
    if (n > 0) {
        printf("  entry: %s\n", d.d_name);
    }
    close(fd);
    printf("dirtest: done\n");
    return 0;
}
