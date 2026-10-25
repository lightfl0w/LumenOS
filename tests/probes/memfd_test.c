#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <sys/syscall.h>

#define MAP_SIZE 8192

static int g_fail;

static void expect(int cond, const char *msg) {
    if (cond) {
        printf("  [ok] %s\n", msg);
    } else {
        g_fail = 1;
        printf("  [FAIL] %s (errno=%d)\n", msg, errno);
    }
}

int main(void) {
    printf("memfd_test: start\n");

    int fd = syscall(SYS_memfd_create, "wl-shm", 0);
    expect(fd >= 0, "memfd_create");

    expect(ftruncate(fd, MAP_SIZE) == 0, "ftruncate");

    struct stat st;
    expect(fstat(fd, &st) == 0 && st.st_size == MAP_SIZE, "fstat size");

    char *map = mmap(NULL, MAP_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    expect(map != MAP_FAILED, "mmap MAP_SHARED");

    strcpy(map, "PING");
    strcpy(map + 4096, "PAGE2");

    int sv[2];
    expect(socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0, "socketpair");

    pid_t pid = fork();
    if (pid == 0) {
        char *cmap = mmap(NULL, MAP_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        if (cmap == MAP_FAILED)
            _exit(1);
        if (strcmp(cmap, "PING") != 0)
            _exit(2);
        if (strcmp(cmap + 4096, "PAGE2") != 0)
            _exit(3);
        strcpy(cmap + 100, "PONG");

        char cmsgbuf[CMSG_SPACE(sizeof(int))];
        memset(cmsgbuf, 0, sizeof(cmsgbuf));
        struct msghdr mh;
        memset(&mh, 0, sizeof(mh));
        struct iovec iov;
        iov.iov_base = (void *)"FD";
        iov.iov_len = 2;
        mh.msg_iov = &iov;
        mh.msg_iovlen = 1;
        mh.msg_control = cmsgbuf;
        mh.msg_controllen = sizeof(cmsgbuf);
        struct cmsghdr *cm = CMSG_FIRSTHDR(&mh);
        cm->cmsg_len = CMSG_LEN(sizeof(int));
        cm->cmsg_level = SOL_SOCKET;
        cm->cmsg_type = SCM_RIGHTS;
        memcpy(CMSG_DATA(cm), &fd, sizeof(int));

        ssize_t sn = sendmsg(sv[1], &mh, 0);
        printf("  [child] sendmsg=%d errno=%d fd=%d\n", (int)sn, errno, fd);
        if (sn < 0)
            _exit(4);
        _exit(0);
    }

    char cmsgbuf[CMSG_SPACE(sizeof(int))];
    struct msghdr rm;
    memset(&rm, 0, sizeof(rm));
    char data[8] = {0};
    struct iovec riov;
    riov.iov_base = data;
    riov.iov_len = sizeof(data);
    rm.msg_iov = &riov;
    rm.msg_iovlen = 1;
    rm.msg_control = cmsgbuf;
    rm.msg_controllen = sizeof(cmsgbuf);

    ssize_t n = recvmsg(sv[0], &rm, 0);
    expect(n == 2 && memcmp(data, "FD", 2) == 0, "recvmsg from child");

    int got_fd = -1;
    struct cmsghdr *rcm = CMSG_FIRSTHDR(&rm);
    expect(rcm != NULL && rcm->cmsg_type == SCM_RIGHTS, "cmsg present");
    if (rcm != NULL && rcm->cmsg_type == SCM_RIGHTS) {
        memcpy(&got_fd, CMSG_DATA(rcm), sizeof(int));
    }
    expect(got_fd >= 0, "memfd fd received");

    expect(strcmp(map + 100, "PONG") == 0, "child write visible in parent map");

    char *pmap = got_fd >= 0 ? mmap(NULL, MAP_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, got_fd, 0)
                             : MAP_FAILED;
    expect(pmap != MAP_FAILED, "mmap received memfd");
    if (pmap != MAP_FAILED) {
        expect(strcmp(pmap, "PING") == 0 && strcmp(pmap + 100, "PONG") == 0,
               "same pages via passed fd");
    }

    int st2 = 0;
    wait(&st2);
    printf("  [dbg] child exit=%d\n", st2);
    expect(st2 == 0, "child exit clean");

    printf("memfd_test: %s\n", g_fail ? "FAIL" : "PASS");
    return g_fail;
}
