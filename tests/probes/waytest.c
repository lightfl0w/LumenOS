#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>

#include "wayland-client.h"
#include "wayland-client-protocol.h"
#include "wayland-server.h"
#include "wayland-server-protocol.h"

#define SOCK_NAME "lumen-waytest"
#define IMG_W 64
#define IMG_H 32
#define IMG_BYTES (IMG_W * IMG_H * 4)
#define PIX_FIRST 0x40E2C81Fu
#define PIX_LAST 0x7F112233u

static int g_fail;

static void expect(int cond, const char *msg) {
    if (cond) {
        printf("  [ok] %s\n", msg);
    } else {
        g_fail = 1;
        printf("  [FAIL] %s (errno=%d)\n", msg, errno);
    }
}

static volatile int srv_attach_ok;
static volatile int srv_commit_seen;

static void surface_destroy(struct wl_client *client, struct wl_resource *res) {
    (void)client;
    wl_resource_destroy(res);
}

static void surface_attach(struct wl_client *client, struct wl_resource *res,
                           struct wl_resource *buffer, int32_t x, int32_t y) {
    (void)client;
    (void)res;
    (void)x;
    (void)y;
    struct wl_shm_buffer *shm = wl_shm_buffer_get(buffer);
    if (shm == NULL) {
        printf("  [FAIL] attach: not a shm buffer\n");
        g_fail = 1;
        return;
    }
    uint32_t w = wl_shm_buffer_get_width(shm);
    uint32_t h = wl_shm_buffer_get_height(shm);
    uint32_t *data = wl_shm_buffer_get_data(shm);
    if (w != IMG_W || h != IMG_H) {
        printf("  [FAIL] attach: size %ux%u\n", w, h);
        g_fail = 1;
        return;
    }
    if (data[0] != PIX_FIRST || data[IMG_W * IMG_H - 1] != PIX_LAST) {
        printf("  [FAIL] attach: pixels %x %x\n", data[0], data[IMG_W * IMG_H - 1]);
        g_fail = 1;
        return;
    }
    srv_attach_ok = 1;
}

static void surface_damage(struct wl_client *c, struct wl_resource *r, int32_t x, int32_t y,
                           int32_t w, int32_t h) {
    (void)c;
    (void)r;
    (void)x;
    (void)y;
    (void)w;
    (void)h;
}

static void surface_frame(struct wl_client *client, struct wl_resource *res, uint32_t id) {
    struct wl_resource *cb = wl_resource_create(client, NULL, 1, id);
    if (cb != NULL) {
        wl_callback_send_done(cb, 0);
        wl_resource_destroy(cb);
    }
}

static void surface_commit(struct wl_client *client, struct wl_resource *res) {
    (void)client;
    (void)res;
    srv_commit_seen = 1;
}

static void surface_noop(struct wl_client *client, struct wl_resource *res) {
    (void)client;
    (void)res;
}

static void surface_noop_res(struct wl_client *client, struct wl_resource *res,
                             struct wl_resource *r2) {
    (void)client;
    (void)res;
    (void)r2;
}

static void surface_noop_i32(struct wl_client *client, struct wl_resource *res, int32_t v) {
    (void)client;
    (void)res;
    (void)v;
}

static void surface_noop_4i32(struct wl_client *client, struct wl_resource *res, int32_t a,
                             int32_t b, int32_t c, int32_t d) {
    (void)client;
    (void)res;
    (void)a;
    (void)b;
    (void)c;
    (void)d;
}

static const struct wl_surface_interface surface_impl = {
    surface_destroy,  surface_attach,   surface_damage,   surface_frame,
    surface_noop_res, surface_noop_res, surface_commit,   surface_noop_i32,
    surface_noop_i32, surface_noop_4i32,
};

static void compositor_create_surface(struct wl_client *client, struct wl_resource *res,
                                      uint32_t id) {
    (void)res;
    struct wl_resource *surf = wl_resource_create(client, &wl_surface_interface, 1, id);
    if (surf == NULL) {
        wl_client_post_no_memory(client);
        return;
    }
    wl_resource_set_implementation(surf, &surface_impl, NULL, NULL);
}

static void compositor_create_region(struct wl_client *client, struct wl_resource *res,
                                     uint32_t id) {
    (void)res;
    struct wl_resource *region = wl_resource_create(client, NULL, 1, id);
    if (region != NULL) {
        wl_resource_destroy(region);
    }
}

static const struct wl_compositor_interface compositor_impl = {
    compositor_create_surface,
    compositor_create_region,
};

static void bind_compositor(struct wl_client *client, void *data, uint32_t version, uint32_t id) {
    (void)data;
    struct wl_resource *res =
        wl_resource_create(client, &wl_compositor_interface, version, id);
    if (res == NULL) {
        wl_client_post_no_memory(client);
        return;
    }
    wl_resource_set_implementation(res, &compositor_impl, NULL, NULL);
}

static void registry_global(void *data, struct wl_registry *r, uint32_t name, const char *iface,
                            uint32_t version) {
    (void)data;
    if (strcmp(iface, "wl_compositor") == 0) {
        struct wl_compositor *c = wl_registry_bind(r, name, &wl_compositor_interface, 1);
        *(void **)((void **)data)[0] = c;
    } else if (strcmp(iface, "wl_shm") == 0) {
        struct wl_shm *sh = wl_registry_bind(r, name, &wl_shm_interface, 1);
        *(void **)((void **)data)[1] = sh;
    }
}

static void registry_global_remove(void *data, struct wl_registry *r, uint32_t name) {
    (void)data;
    (void)r;
    (void)name;
}

static const struct wl_registry_listener registry_listener = {registry_global,
                                                              registry_global_remove};

static int run_server(void) {
    struct wl_display *display = wl_display_create();
    if (display == NULL) {
        printf("server: no display\n");
        return 1;
    }

    if (wl_display_init_shm(display) < 0) {
        printf("server: init_shm failed\n");
        return 1;
    }

    if (wl_global_create(display, &wl_compositor_interface, 1, NULL, bind_compositor) == NULL) {
        printf("server: global_create failed\n");
        return 1;
    }

    if (wl_display_add_socket(display, SOCK_NAME) < 0) {
        printf("server: add_socket failed errno=%d\n", errno);
        return 1;
    }
    printf("  [ok] server socket /tmp/%s\n", SOCK_NAME);

    struct wl_event_loop *loop = wl_display_get_event_loop(display);
    for (int i = 0; i < 200 && !(srv_attach_ok && srv_commit_seen); i++) {
        int rc = wl_event_loop_dispatch(loop, 50);
        wl_display_flush_clients(display);
    }

    expect(srv_attach_ok, "server verified shm buffer pixels");
    expect(srv_commit_seen, "server saw surface commit");
    wl_display_destroy(display);
    return 0;
}

static int run_client(void) {
    struct wl_display *d = wl_display_connect(SOCK_NAME);
    expect(d != NULL, "client connect");
    if (d == NULL) {
        return 1;
    }

    struct wl_compositor *compositor = NULL;
    struct wl_shm *shm = NULL;
    void *globals[2] = {&compositor, &shm};
    struct wl_registry *registry = wl_display_get_registry(d);
    wl_registry_add_listener(registry, &registry_listener, globals);
    wl_display_roundtrip(d);
    expect(compositor != NULL, "client bound wl_compositor");
    expect(shm != NULL, "client bound wl_shm");

    int fd = syscall(319, "wl-pool", 0);
    expect(fd >= 0, "memfd_create");
    if (fd < 0) {
        return 1;
    }
    expect(ftruncate(fd, IMG_BYTES) == 0, "ftruncate pool");

    uint32_t *pixels = mmap(NULL, IMG_BYTES, PROT_WRITE, MAP_SHARED, fd, 0);
    expect(pixels != MAP_FAILED, "mmap pool");
    if (pixels == MAP_FAILED) {
        return 1;
    }
    pixels[0] = PIX_FIRST;
    pixels[IMG_W * IMG_H - 1] = PIX_LAST;

    struct wl_shm_pool *pool = wl_shm_create_pool(shm, fd, IMG_BYTES);
    expect(pool != NULL, "create_pool");
    struct wl_buffer *buffer =
        wl_shm_pool_create_buffer(pool, 0, IMG_W, IMG_H, IMG_W * 4, WL_SHM_FORMAT_ARGB8888);
    expect(buffer != NULL, "create_buffer");

    struct wl_surface *surface = wl_compositor_create_surface(compositor);
    expect(surface != NULL, "create_surface");
    wl_surface_attach(surface, buffer, 0, 0);
    wl_surface_commit(surface);

    expect(wl_display_roundtrip(d) >= 0, "roundtrip after commit");
    wl_display_disconnect(d);
    return 0;
}

int main(void) {
    printf("waytest: start\n");
    setenv("XDG_RUNTIME_DIR", "/tmp", 1);


    pid_t pid = fork();
    if (pid == 0) {
        usleep(300000);
        int rc = run_client();
        _exit(rc);
    }

    int src = run_server();
    int st = 0;
    wait(&st);
    int crc = WIFEXITED(st) ? WEXITSTATUS(st) : 1;

    printf("waytest: %s (server=%d client=%d)\n",
           (g_fail || src || crc) ? "FAIL" : "PASS", src, crc);
    return g_fail || src || crc;
}
