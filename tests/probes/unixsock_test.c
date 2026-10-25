#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>

#define SOCK_PATH "/tmp/ux_test_sock"
#define DATA_PATH "/tmp/ux_test_data"

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
    printf("unixsock_test: start\n");

    unlink(SOCK_PATH);
    unlink(DATA_PATH);

    int lfd = socket(AF_UNIX, SOCK_STREAM, 0);
    expect(lfd >= 0, "socket");

    struct sockaddr_un sa;
    memset(&sa, 0, sizeof(sa));
    sa.sun_family = AF_UNIX;
    strcpy(sa.sun_path, SOCK_PATH);
    expect(bind(lfd, (struct sockaddr *)&sa, sizeof(sa)) == 0, "bind");

    expect(listen(lfd, 4) == 0, "listen");

    pid_t pid = fork();
    if (pid == 0) {
        int cfd = socket(AF_UNIX, SOCK_STREAM, 0);
        if (cfd < 0)
            _exit(1);
        struct sockaddr_un ca;
        memset(&ca, 0, sizeof(ca));
        ca.sun_family = AF_UNIX;
        strcpy(ca.sun_path, SOCK_PATH);
        if (connect(cfd, (struct sockaddr *)&ca, sizeof(ca)) != 0)
            _exit(2);

        int pfd[2];
        if (pipe(pfd) != 0)
            _exit(3);
        if (write(pfd[1], "MAGIC", 5) != 5)
            _exit(4);
        close(pfd[1]);
        int dfd = pfd[0];

        char cmsgbuf[CMSG_SPACE(sizeof(int))];
        memset(cmsgbuf, 0, sizeof(cmsgbuf));
        struct msghdr mh;
        memset(&mh, 0, sizeof(mh));
        struct iovec iov;
        iov.iov_base = (void *)"HELLO";
        iov.iov_len = 5;
        mh.msg_iov = &iov;
        mh.msg_iovlen = 1;
        mh.msg_control = cmsgbuf;
        mh.msg_controllen = sizeof(cmsgbuf);
        struct cmsghdr *cm = CMSG_FIRSTHDR(&mh);
        cm->cmsg_len = CMSG_LEN(sizeof(int));
        cm->cmsg_level = SOL_SOCKET;
        cm->cmsg_type = SCM_RIGHTS;
        memcpy(CMSG_DATA(cm), &dfd, sizeof(int));

        if (sendmsg(cfd, &mh, 0) < 0)
            _exit(6);
        close(dfd);

        char ack[8] = {0};
        ssize_t n = recv(cfd, ack, 3, 0);
        if (n != 3 || memcmp(ack, "ACK", 3) != 0)
            _exit(7);
        close(cfd);
        _exit(0);
    }

    int afd = accept(lfd, NULL, NULL);
    expect(afd >= 0, "accept");

    char rmsgbuf[CMSG_SPACE(sizeof(int))];
    struct msghdr rm;
    memset(&rm, 0, sizeof(rm));
    char data[16] = {0};
    struct iovec riov;
    riov.iov_base = data;
    riov.iov_len = sizeof(data);
    rm.msg_iov = &riov;
    rm.msg_iovlen = 1;
    rm.msg_control = rmsgbuf;
    rm.msg_controllen = sizeof(rmsgbuf);

    ssize_t n = recvmsg(afd, &rm, 0);
    expect(n == 5 && memcmp(data, "HELLO", 5) == 0, "recvmsg data");

    int got_fd = -1;
    struct cmsghdr *rcm = CMSG_FIRSTHDR(&rm);
    expect(rcm != NULL && rcm->cmsg_type == SCM_RIGHTS, "cmsg present");
    if (rcm != NULL && rcm->cmsg_type == SCM_RIGHTS) {
        memcpy(&got_fd, CMSG_DATA(rcm), sizeof(int));
    }
    expect(got_fd >= 0, "fd received");

    char fdata[16] = {0};
    ssize_t fn = got_fd >= 0 ? read(got_fd, fdata, 8) : -1;
    expect(fn == 5 && memcmp(fdata, "MAGIC", 5) == 0, "read passed fd");

    expect(send(afd, "ACK", 3, 0) == 3, "send ack");

    int st = 0;
    wait(&st);
    expect(st == 0, "client exit clean");

    close(afd);
    close(lfd);
    unlink(SOCK_PATH);
    unlink(DATA_PATH);

    printf("unixsock_test: %s\n", g_fail ? "FAIL" : "PASS");
    return g_fail;
}
