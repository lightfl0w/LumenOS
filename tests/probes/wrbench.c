#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>
#include <errno.h>

static uint64_t now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000u + (uint32_t)ts.tv_nsec / 1000000u;
}

static unsigned long checksum;

static void upd(const char *p, int n) {
    for (int i = 0; i < n; i++) {
        checksum = checksum * 31u + (unsigned char)p[i];
    }
}

static int big_write(const char *path, int mb, int chunk) {
    char *buf = malloc(chunk);
    if (buf == NULL) {
        return -1;
    }
    for (int i = 0; i < chunk; i++) {
        buf[i] = (char)(i * 7 + 3);
    }
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        printf("  open %s failed errno=%d\n", path, errno);
        free(buf);
        return -1;
    }
    uint64_t t0 = now_ms();
    int blocks = mb * 1024 * 1024 / chunk;
    for (int i = 0; i < blocks; i++) {
        ssize_t n = write(fd, buf, chunk);
        if (n != chunk) {
            printf("  short write at %d (%d)\n", i, (int)n);
            close(fd);
            free(buf);
            return -1;
        }
        upd(buf, chunk);
    }
    uint64_t t1 = now_ms();
    close(fd);

    unsigned long expect = checksum;
    checksum = 0;
    fd = open(path, O_RDONLY);
    if (fd < 0) {
        printf("  reopen failed\n");
        free(buf);
        return -1;
    }
    int got_bytes = 0;
    for (;;) {
        ssize_t n = read(fd, buf, chunk);
        if (n <= 0) {
            break;
        }
        upd(buf, (int)n);
        got_bytes += (int)n;
    }
    uint64_t t2 = now_ms();
    close(fd);
    free(buf);
    printf("  seq-write %dMB chunk=%d: %d ms (%d KB/s), verify=%s, read %d ms\n",
           mb, chunk, (int)(t1 - t0),
           (t1 > t0) ? (int)((uint64_t)mb * 1024 * 1000 / (t1 - t0)) : -1,
           checksum == expect && got_bytes == mb * 1024 * 1024 ? "OK" : "MISMATCH",
           (int)(t2 - t1));
    checksum = 0;
    unlink(path);
    return 0;
}

static int small_files(const char *dir, int count) {
    char path[128];
    char buf[256];
    mkdir(dir, 0755);
    uint64_t t0 = now_ms();
    for (int i = 0; i < count; i++) {
        snprintf(path, sizeof(path), "%s/f%04d", dir, i);
        int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd < 0) {
            printf("  create %s failed errno=%d\n", path, errno);
            return -1;
        }
        int len = snprintf(buf, sizeof(buf), "content of file %d with some padding text\n", i);
        if (write(fd, buf, len) != len) {
            printf("  write %s failed\n", path);
            close(fd);
            return -1;
        }
        close(fd);
    }
    uint64_t t1 = now_ms();

    for (int i = 0; i < count; i++) {
        snprintf(path, sizeof(path), "%s/f%04d", dir, i);
        int fd = open(path, O_RDONLY);
        if (fd < 0) {
            printf("  reopen %s failed\n", path);
            return -1;
        }
        char rbuf[256];
        int len = snprintf(buf, sizeof(buf), "content of file %d with some padding text\n", i);
        ssize_t n = read(fd, rbuf, sizeof(rbuf));
        close(fd);
        if (n != len || memcmp(rbuf, buf, len) != 0) {
            printf("  verify %s MISMATCH\n", path);
            return -1;
        }
    }
    uint64_t t2 = now_ms();
    for (int i = 0; i < count; i++) {
        snprintf(path, sizeof(path), "%s/f%04d", dir, i);
        unlink(path);
    }
    rmdir(dir);
    uint64_t t3 = now_ms();
    printf("  small-files %d: create %d ms, read+verify %d ms, delete %d ms\n",
           count, (int)(t1 - t0), (int)(t2 - t1), (int)(t3 - t2));
    return 0;
}

int main(void) {
    printf("wrbench: start\n");
    big_write("/wrbig.bin", 4, 4096);
    big_write("/wrbig.bin", 4, 512);
    small_files("/wrsmall", 64);
    printf("wrbench: done\n");
    return 0;
}
