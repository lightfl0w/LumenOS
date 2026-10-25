#include <errno.h>
#include <stdio.h>
#include <sys/eventfd.h>
#include "wayland-client.h"
#include "wayland-server.h"

int main(void) {
    printf("wl version: %s\n", WAYLAND_VERSION);
    struct wl_event_loop *lp = wl_event_loop_create();
    printf("loop=%p errno=%d\n", lp, errno);
    if (lp) {
        int ef = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
        printf("efd=%d errno=%d\n", ef, errno);
        struct wl_event_source *src = wl_event_loop_add_fd(lp, ef, WL_EVENT_READABLE, NULL, NULL);
        printf("src=%p errno=%d\n", src, errno);
    }
    struct wl_display *d = wl_display_create();
    printf("display=%p errno=%d\n", d, errno);
    if (!d) {
        printf("wlsmoke: FAIL (no display)\n");
        return 1;
    }
    printf("display created, err=%d\n", wl_display_get_error(d));
    wl_display_destroy(d);
    printf("wlsmoke: PASS\n");
    return 0;
}
