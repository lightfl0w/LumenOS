#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

int main(void) {
    setenv("XDG_RUNTIME_DIR", "/tmp", 1);
    const char *fifo = "/tmp/sway-spawn";
    int rfd = open(fifo, O_RDWR | O_CREAT | O_TRUNC, 0666);
    if (rfd < 0) {
        perror("open request file");
        return 1;
    }
    printf("SUPER ready\n");
    fflush(stdout);
    pid_t pid = fork();
    if (pid == 0) {
        if (rfd != 3) {
            dup2(rfd, 3);
            close(rfd);
        }
        execl("/bin/sway.elf", "/bin/sway.elf", "-c", "/etc/sway/config",
              (char *)NULL);
        perror("execl sway");
        _exit(1);
    }
    if (pid < 0) {
        perror("fork");
        return 1;
    }
    char target[256];
    for (;;) {
        lseek(rfd, 0, SEEK_SET);
        ssize_t n = read(rfd, target, sizeof(target) - 1);
        if (n <= 0) {
            usleep(100 * 1000);
            continue;
        }
        ftruncate(rfd, 0);
        target[n] = 0;
        printf("SUPER spawn %s\n", target);
        fflush(stdout);
        pid_t child = fork();
        if (child == 0) {
            execl(target, target, (char *)NULL);
            _exit(1);
        }
        if (child > 0) {
            waitpid(child, NULL, 0);
        }
    }
    return 0;
}
