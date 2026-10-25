#ifndef KERNEL_IPC_UNIX_SOCK_H
#define KERNEL_IPC_UNIX_SOCK_H

#include <stdint.h>

#define UNIX_FD_BASE 0x400u

int unix_fd_slot(uint64_t fd);
int unix_socket_create(int type);
int unix_socketpair(int type, int out[2]);
int unix_bind(int idx, const char *path);
int unix_listen(int idx);
int unix_connect(int idx, const char *path);
int unix_accept(int idx);
int unix_path_bound(const char *path);

int32_t unix_send(int idx, const void *buf, uint32_t len);
int32_t unix_recv(int idx, void *buf, uint32_t len);
int32_t unix_send_data(int idx, const void *buf, uint32_t len, const int *gfds, int nfds);
int32_t unix_recv_data(int idx, void *buf, uint32_t len, int *gfds, int *nfds);

void unix_set_nonblock(int idx, int on);
uint32_t unix_events(int idx);
int unix_peer_alive(int idx);
void unix_close_slot(int idx);

#endif
