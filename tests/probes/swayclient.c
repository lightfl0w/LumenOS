#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int main(void) {
    setenv("XDG_RUNTIME_DIR", "/tmp", 1);
    pid_t pid = fork();
    if (pid == 0) {
        execl("/bin/sway.elf", "/bin/sway.elf", "-c", "/etc/sway/config",
              (char *)NULL);
        perror("execl sway");
        _exit(1);
    }
    if (pid < 0) {
        perror("fork");
        return 1;
    }
    sleep(8);
    pid_t client = fork();
    if (client == 0) {
        execl("/bin/wlprobe.elf", "/bin/wlprobe.elf", (char *)NULL);
        _exit(1);
    }
    if (client < 0) {
        perror("fork client");
        return 1;
    }
    sleep(10);
    return 0;
}
