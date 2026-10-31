#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <libseat.h>
#include <xf86drm.h>
#include <xf86drmMode.h>

static int enabled_count;
static int disabled_count;

static void handle_enable_seat(struct libseat *seat, void *data) {
	(void)seat;
	(void)data;
	enabled_count++;
	printf("[seatprobe] seat enabled\n");
}

static void handle_disable_seat(struct libseat *seat, void *data) {
	(void)seat;
	(void)data;
	disabled_count++;
	printf("[seatprobe] seat disabled\n");
}

static void _socketpair_child(int fd) {
	char b[8] = {0};
	struct iovec iov = { .iov_base = b, .iov_len = sizeof(b) };
	struct msghdr mh = { .msg_iov = &iov, .msg_iovlen = 1 };
	for (int t = 0; t < 2000; t++) {
		ssize_t n = recvmsg(fd, &mh, MSG_DONTWAIT);
		if (n >= 0) {
			printf("[seatprobe] raw: child recvmsg %zd\n", n);
			fflush(stdout);
			_exit(0);
		}
		if (errno != EAGAIN && errno != EINTR) {
			printf("[seatprobe] raw: child recvmsg: %s\n", strerror(errno));
			fflush(stdout);
			_exit(2);
		}
		usleep(1000);
	}
	printf("[seatprobe] raw: child recvmsg timeout\n");
	fflush(stdout);
	_exit(3);
}

static void raw_parent_send(int fd) {
	struct iovec iov = { .iov_base = (void *)"ping", .iov_len = 4 };
	struct msghdr mh = { .msg_iov = &iov, .msg_iovlen = 1 };
	ssize_t n = sendmsg(fd, &mh, MSG_NOSIGNAL | MSG_DONTWAIT);
	printf("[seatprobe] raw: parent sendmsg %zd\n", n);
	fflush(stdout);
}

static void raw_socketpair_test(void) {
	int sv[2];
	if (socketpair(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0, sv) == -1) {
		printf("[seatprobe] raw: socketpair: %s\n", strerror(errno));
		return;
	}
	printf("[seatprobe] raw: socketpair ok sv=%d,%d\n", sv[0], sv[1]);
	fflush(stdout);
	pid_t pid = fork();
	if (pid == 0) {
		_socketpair_child(sv[0]);
	}
	raw_parent_send(sv[1]);
	int st = 0;
	waitpid(pid, &st, 0);
	printf("[seatprobe] raw: child exit=%d\n", WEXITSTATUS(st));
	fflush(stdout);
	close(sv[0]);
	close(sv[1]);
}

static void dbg_device_name(int fd) {
	struct stat dst;
	int fr = fstat(fd, &dst);
	printf("[seatprobe] devfstat r=%d chr=%d rdev=%llu\n", fr,
	       fr == 0 && S_ISCHR(dst.st_mode),
	       fr == 0 ? (unsigned long long)dst.st_rdev : 0ull);

	FILE *uf = fopen("/sys/dev/char/226:0/uevent", "r");
	printf("[seatprobe] direct uevent fopen=%s\n", uf ? "ok" : strerror(errno));
	if (uf != NULL) {
		char l1[64] = {0};
		fgets(l1, sizeof(l1), uf);
		printf("[seatprobe] uevent line=%s", l1);
		fclose(uf);
	}
	fflush(stdout);
	struct stat sst;
	int sr = stat("/sys/dev/char/226:0/device/drm", &sst);
	printf("[seatprobe] sysfs device/drm stat r=%d\n", sr);

	char *dn = drmGetDeviceNameFromFd2(fd);
	printf("[seatprobe] GetDeviceNameFromFd2=%s\n", dn != NULL ? dn : "<null>");
	free(dn);
	FILE *df = fopen("/etc/drmdbg.txt", "r");
	if (df != NULL) {
		char lbuf[160];
		while (fgets(lbuf, sizeof(lbuf), df) != NULL) {
			printf("[seatprobe] dbg: %s", lbuf);
		}
		fclose(df);
	}
	fflush(stdout);
	fflush(stdout);
}

int main(void) {
	int rc = 1;

	setenv("SEATD_VTBOUND", "0", 1);
	libseat_set_log_level(LIBSEAT_LOG_LEVEL_DEBUG);
	raw_socketpair_test();

	const struct libseat_seat_listener listener = {
		.enable_seat = handle_enable_seat,
		.disable_seat = handle_disable_seat,
	};

	printf("[seatprobe] libseat_open_seat (embedded seatd)...\n");
	struct libseat *seat = libseat_open_seat(&listener, NULL);
	if (seat == NULL) {
		printf("[seatprobe] FAIL: libseat_open_seat: %s\n", strerror(errno));
		return 1;
	}
	printf("[seatprobe] seat name: %s\n", libseat_seat_name(seat));

	for (int t = 0; t < 3000 && enabled_count == 0; t++) {
		if (libseat_dispatch(seat, 0) == -1) {
			break;
		}
		if (enabled_count == 0) {
			usleep(1000);
		}
	}
	if (enabled_count == 0) {
		printf("[seatprobe] FAIL: seat never enabled\n");
		goto out;
	}

	int fd = -1;
	int devid = libseat_open_device(seat, "/dev/dri/card0", &fd);
	if (devid == -1) {
		printf("[seatprobe] FAIL: open_device: %s\n", strerror(errno));
		goto out;
	}
	printf("[seatprobe] device_id=%d fd=%d, KMS=%s\n", devid, fd,
	       drmIsKMS(fd) ? "yes" : "no");
	dbg_device_name(fd);

	if (drmSetMaster(fd) == 0) {
		printf("[seatprobe] drmSetMaster OK\n");
	} else {
		printf("[seatprobe] FAIL: drmSetMaster: %s\n", strerror(errno));
		goto out;
	}

	if (libseat_dispatch(seat, 0) == -1) {
		printf("[seatprobe] FAIL: dispatch after open_device\n");
		goto out;
	}

	if (drmDropMaster(fd) == 0) {
		printf("[seatprobe] drmDropMaster OK\n");
	} else {
		printf("[seatprobe] note: drmDropMaster: %s\n", strerror(errno));
	}

	if (libseat_close_device(seat, devid) == -1) {
		printf("[seatprobe] FAIL: close_device: %s\n", strerror(errno));
		goto out;
	}
	printf("[seatprobe] close_device OK\n");

	rc = 0;

out:
	libseat_close_seat(seat);
	printf("[seatprobe] %s (enabled=%d disabled=%d)\n",
	       rc == 0 ? "PASS" : "FAIL", enabled_count, disabled_count);
	return rc;
}
