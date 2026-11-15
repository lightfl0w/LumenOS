#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int main(void) {
    int p[2];
    if (pipe2(p, O_CLOEXEC) < 0) {
        perror("pipe2");
        return 1;
    }
    pid_t pid = fork();
    if (pid == 0) {
        execl("/bin/prog_no_arg.elf", "prog_no_arg.elf", (char *)NULL);
        _exit(1);
    }
    close(p[1]);
    char c;
    ssize_t n = read(p[0], &c, 1);
    printf("CLOEXEC read=%zd %s\n", n, n == 0 ? "PASS" : "FAIL");
    return n == 0 ? 0 : 2;
}
