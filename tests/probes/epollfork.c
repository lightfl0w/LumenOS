#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/timerfd.h>
#include <sys/un.h>
#include <unistd.h>

#define PATH "/tmp/epollfork.sock"

int main(int argc, char **argv) {
    if (argc > 1 && strcmp(argv[1], "client") == 0) {
        usleep(300 * 1000);
        int cfd = socket(AF_UNIX, SOCK_STREAM, 0);
        struct sockaddr_un caddr = {0};
        caddr.sun_family = AF_UNIX;
        strcpy(caddr.sun_path, PATH);
        int rc = connect(cfd, (struct sockaddr *)&caddr, sizeof(caddr));
        printf("EPOLLFORK child connect=%d errno=%d\n", rc, errno);
        sleep(3);
        return 0;
    }
    int lfd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (lfd < 0) {
        printf("EPOLLFORK socket failed errno=%d\n", errno);
        return 1;
    }
    unlink(PATH);
    struct sockaddr_un addr = {0};
    addr.sun_family = AF_UNIX;
    strcpy(addr.sun_path, PATH);
    if (bind(lfd, (struct sockaddr *)&addr, sizeof(addr)) < 0 ||
        listen(lfd, 4) < 0) {
        printf("EPOLLFORK bind/listen failed errno=%d\n", errno);
        return 1;
    }
    int epfd = epoll_create1(0);
    if (epfd < 0) {
        printf("EPOLLFORK epoll_create1 failed errno=%d\n", errno);
        return 1;
    }
    struct epoll_event ev = {.events = EPOLLIN, .data.fd = lfd};
    if (epoll_ctl(epfd, EPOLL_CTL_ADD, lfd, &ev) < 0) {
        printf("EPOLLFORK epoll_ctl failed errno=%d\n", errno);
        return 1;
    }

    int tfd = timerfd_create(CLOCK_MONOTONIC, 0);
    struct itimerspec its = {.it_value = {.tv_nsec = 200 * 1000 * 1000}};
    timerfd_settime(tfd, 0, &its, NULL);
    struct epoll_event tev = {.events = EPOLLIN, .data.fd = tfd};
    epoll_ctl(epfd, EPOLL_CTL_ADD, tfd, &tev);
    int forked = 0;

    for (int i = 0; i < 50; i++) {
        struct epoll_event out[32];
        int n = epoll_wait(epfd, out, 32, 100);
        for (int k = 0; k < n; k++) {
            if (out[k].data.fd == tfd && !forked) {
                forked = 1;
                uint64_t ticks = 0;
                read(tfd, &ticks, sizeof(ticks));
                pid_t pid = fork();
                if (pid == 0) {
                    execl("/bin/epollfork.elf", "epollfork.elf", "client",
                          (char *)NULL);
                    _exit(0);
                }
                continue;
            }
            int cfd = accept(lfd, NULL, NULL);
            printf("EPOLLFORK PASS after fork: epoll n=%d fd=%d accept=%d\n",
                   n, out[k].data.fd, cfd);
            return 0;
        }
    }
    printf("EPOLLFORK FAIL: listener never readable after fork\n");
    return 2;
}
