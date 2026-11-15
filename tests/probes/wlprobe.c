#define _GNU_SOURCE
#include <errno.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

#include "wayland-client.h"
#include "xdg-shell-client-protocol.h"

#define WIDTH 320
#define HEIGHT 200
#define STRIDE (WIDTH * 4)
#define SIZE ((size_t)STRIDE * HEIGHT)

struct probe {
    struct wl_display *display;
    struct wl_registry *registry;
    struct wl_compositor *compositor;
    struct wl_shm *shm;
    struct wl_seat *seat;
    struct wl_keyboard *keyboard;
    struct xdg_wm_base *wm_base;
    struct wl_surface *surface;
    struct xdg_surface *xdg_surface;
    struct xdg_toplevel *toplevel;
    struct wl_buffer *buffer;
    int configured;
    int mapped;
    int key_seen;
};

static void ping(void *data, struct xdg_wm_base *wm_base, uint32_t serial) {
    (void)data;
    xdg_wm_base_pong(wm_base, serial);
}

static const struct xdg_wm_base_listener wm_base_listener = {ping};

static void keyboard_keymap(void *data, struct wl_keyboard *keyboard,
                            uint32_t format, int32_t fd, uint32_t size) {
    (void)data; (void)keyboard; (void)format; (void)size;
    close(fd);
}

static void keyboard_enter(void *data, struct wl_keyboard *keyboard,
                           uint32_t serial, struct wl_surface *surface,
                           struct wl_array *keys) {
    (void)data; (void)keyboard; (void)serial; (void)keys;
    printf("WLPROBE keyboard enter surface=%p\n", (void *)surface);
}

static void keyboard_leave(void *data, struct wl_keyboard *keyboard,
                           uint32_t serial, struct wl_surface *surface) {
    (void)data; (void)keyboard; (void)serial;
    printf("WLPROBE keyboard leave surface=%p\n", (void *)surface);
}

static void keyboard_key(void *data, struct wl_keyboard *keyboard,
                         uint32_t serial, uint32_t time, uint32_t key,
                         uint32_t state) {
    struct probe *probe = data;
    (void)keyboard; (void)serial; (void)time;
    printf("WLPROBE key code=%u state=%u\n", key, state);
    fflush(stdout);
    probe->key_seen = 1;
}

static void keyboard_modifiers(void *data, struct wl_keyboard *keyboard,
                               uint32_t serial, uint32_t depressed,
                               uint32_t latched, uint32_t locked,
                               uint32_t group) {
    (void)data; (void)keyboard; (void)serial; (void)depressed;
    (void)latched; (void)locked; (void)group;
}

static void keyboard_repeat_info(void *data, struct wl_keyboard *keyboard,
                                 int32_t rate, int32_t delay) {
    (void)data; (void)keyboard; (void)rate; (void)delay;
}

static const struct wl_keyboard_listener keyboard_listener = {
    .keymap = keyboard_keymap,
    .enter = keyboard_enter,
    .leave = keyboard_leave,
    .key = keyboard_key,
    .modifiers = keyboard_modifiers,
    .repeat_info = keyboard_repeat_info,
};

static void seat_capabilities(void *data, struct wl_seat *seat, uint32_t caps) {
    struct probe *probe = data;
    if ((caps & WL_SEAT_CAPABILITY_KEYBOARD) && probe->keyboard == NULL) {
        probe->keyboard = wl_seat_get_keyboard(seat);
        wl_keyboard_add_listener(probe->keyboard, &keyboard_listener, probe);
    }
}

static void seat_name(void *data, struct wl_seat *seat, const char *name) {
    (void)data; (void)seat;
    printf("WLPROBE seat=%s\n", name);
}

static const struct wl_seat_listener seat_listener = {
    .capabilities = seat_capabilities,
    .name = seat_name,
};

static void xdg_surface_configure(void *data, struct xdg_surface *surface,
                                  uint32_t serial) {
    struct probe *probe = data;
    xdg_surface_ack_configure(surface, serial);
    probe->configured = 1;
    printf("WLPROBE configured\n");
    fflush(stdout);
}

static const struct xdg_surface_listener xdg_surface_listener = {
    .configure = xdg_surface_configure,
};

static void toplevel_configure(void *data, struct xdg_toplevel *toplevel,
                               int32_t width, int32_t height,
                               struct wl_array *states) {
    (void)data; (void)toplevel; (void)width; (void)height; (void)states;
}

static void toplevel_close(void *data, struct xdg_toplevel *toplevel) {
    (void)data; (void)toplevel;
}

static void toplevel_bounds(void *data, struct xdg_toplevel *toplevel,
                            int32_t width, int32_t height) {
    (void)data; (void)toplevel; (void)width; (void)height;
}

static void toplevel_capabilities(void *data, struct xdg_toplevel *toplevel,
                                  struct wl_array *capabilities) {
    (void)data; (void)toplevel; (void)capabilities;
}

static const struct xdg_toplevel_listener toplevel_listener = {
    .configure = toplevel_configure,
    .close = toplevel_close,
    .configure_bounds = toplevel_bounds,
    .wm_capabilities = toplevel_capabilities,
};

static void registry_global(void *data, struct wl_registry *registry,
                            uint32_t name, const char *interface,
                            uint32_t version) {
    struct probe *probe = data;
    if (strcmp(interface, wl_compositor_interface.name) == 0) {
        probe->compositor = wl_registry_bind(registry, name,
                                             &wl_compositor_interface, 4);
    } else if (strcmp(interface, wl_shm_interface.name) == 0) {
        probe->shm = wl_registry_bind(registry, name, &wl_shm_interface, 1);
    } else if (strcmp(interface, wl_seat_interface.name) == 0) {
        probe->seat = wl_registry_bind(registry, name, &wl_seat_interface, 7);
        wl_seat_add_listener(probe->seat, &seat_listener, probe);
    } else if (strcmp(interface, xdg_wm_base_interface.name) == 0) {
        probe->wm_base = wl_registry_bind(registry, name,
                                          &xdg_wm_base_interface, 1);
        xdg_wm_base_add_listener(probe->wm_base, &wm_base_listener, probe);
    }
    (void)version;
}

static void registry_global_remove(void *data, struct wl_registry *registry,
                                   uint32_t name) {
    (void)data; (void)registry; (void)name;
}

static const struct wl_registry_listener registry_listener = {
    .global = registry_global,
    .global_remove = registry_global_remove,
};

static struct wl_buffer *create_buffer(struct probe *probe) {
    int fd = memfd_create("wlprobe", MFD_CLOEXEC);
    if (fd < 0) {
        printf("WLPROBE memfd failed: %d\n", errno);
        return NULL;
    }
    if (ftruncate(fd, (off_t)SIZE) < 0) {
        close(fd);
        return NULL;
    }
    uint32_t *pixels = mmap(NULL, SIZE, PROT_READ | PROT_WRITE,
                            MAP_SHARED, fd, 0);
    if (pixels == MAP_FAILED) {
        close(fd);
        return NULL;
    }
    for (uint32_t y = 0; y < HEIGHT; y++) {
        for (uint32_t x = 0; x < WIDTH; x++) {
            pixels[y * WIDTH + x] = ((x / 32 + y / 32) & 1)
                ? 0xff2a6f97u : 0xff1d3557u;
        }
    }
    struct wl_shm_pool *pool = wl_shm_create_pool(probe->shm, fd, (int32_t)SIZE);
    struct wl_buffer *buffer = wl_shm_pool_create_buffer(
        pool, 0, WIDTH, HEIGHT, STRIDE, WL_SHM_FORMAT_XRGB8888);
    wl_shm_pool_destroy(pool);
    munmap(pixels, SIZE);
    close(fd);
    return buffer;
}

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);
    if (getenv("XDG_RUNTIME_DIR") == NULL) {
        setenv("XDG_RUNTIME_DIR", "/tmp", 1);
    }
    if (getenv("WAYLAND_DISPLAY") == NULL) {
        setenv("WAYLAND_DISPLAY", "wayland-1", 1);
    }
    printf("WLPROBE display=%s runtime=%s\n",
           getenv("WAYLAND_DISPLAY"), getenv("XDG_RUNTIME_DIR"));

    struct probe probe = {0};
    probe.display = wl_display_connect(NULL);
    if (probe.display == NULL) {
        printf("WLPROBE connect failed: %d\n", errno);
        return 1;
    }
    probe.registry = wl_display_get_registry(probe.display);
    wl_registry_add_listener(probe.registry, &registry_listener, &probe);
    if (wl_display_roundtrip(probe.display) < 0) {
        printf("WLPROBE registry roundtrip failed: %d\n",
               wl_display_get_error(probe.display));
        return 1;
    }
    if (!probe.compositor || !probe.shm || !probe.wm_base) {
        printf("WLPROBE missing globals compositor=%p shm=%p wm_base=%p\n",
               (void *)probe.compositor, (void *)probe.shm,
               (void *)probe.wm_base);
        return 1;
    }

    probe.surface = wl_compositor_create_surface(probe.compositor);
    probe.xdg_surface = xdg_wm_base_get_xdg_surface(probe.wm_base,
                                                    probe.surface);
    xdg_surface_add_listener(probe.xdg_surface, &xdg_surface_listener, &probe);
    probe.toplevel = xdg_surface_get_toplevel(probe.xdg_surface);
    xdg_toplevel_add_listener(probe.toplevel, &toplevel_listener, &probe);
    xdg_toplevel_set_title(probe.toplevel, "wlprobe");
    wl_surface_commit(probe.surface);
    wl_display_roundtrip(probe.display);

    probe.buffer = create_buffer(&probe);
    if (probe.buffer == NULL) {
        printf("WLPROBE buffer failed\n");
        return 1;
    }
    wl_surface_attach(probe.surface, probe.buffer, 0, 0);
    wl_surface_damage_buffer(probe.surface, 0, 0, WIDTH, HEIGHT);
    wl_surface_commit(probe.surface);
    wl_display_flush(probe.display);
    probe.mapped = 1;
    printf("WLPROBE mapped %dx%d\n", WIDTH, HEIGHT);
    fflush(stdout);

    struct pollfd pfd = {.fd = wl_display_get_fd(probe.display),
                         .events = POLLIN};
    int idle_ms = 0;
    while (idle_ms < 60000 && !probe.key_seen) {
        wl_display_flush(probe.display);
        int rc = poll(&pfd, 1, 100);
        if (rc > 0) {
            wl_display_dispatch(probe.display);
        } else {
            idle_ms += 100;
        }
    }
    printf("WLPROBE done key_seen=%d\n", probe.key_seen);
    return probe.key_seen ? 0 : 2;
}
