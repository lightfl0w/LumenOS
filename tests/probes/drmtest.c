#include <drm.h>
#include <fcntl.h>
#include <pixman.h>
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>
#include <xf86drm.h>
#include <xf86drmMode.h>

static uint32_t fb[16 * 16];

int main(void) {
    pixman_image_t *img = pixman_image_create_bits(PIXMAN_x8r8g8b8, 16, 16, fb, 64);
    if (img == NULL) {
        printf("pixman create failed\n");
        return 1;
    }
    pixman_color_t c = {0xffff, 0, 0, 0xffff};
    pixman_image_t *solid = pixman_image_create_solid_fill(&c);
    pixman_image_composite32(PIXMAN_OP_SRC, solid, NULL, img, 0, 0, 0, 0, 0, 0, 16, 16);
    pixman_image_unref(solid);
    printf("pixman %s px=%08x\n", pixman_version_string(), fb[0]);
    pixman_image_unref(img);

    int fd = open("/dev/dri/card0", O_RDWR);
    if (fd < 0) {
        printf("open card0 failed\n");
        return 0;
    }
    drmVersionPtr ver = drmGetVersion(fd);
    if (ver != NULL) {
        printf("drm %s %d.%d.%d\n", ver->name, ver->version_major, ver->version_minor, ver->version_patchlevel);
        drmFreeVersion(ver);
    }
    drmModeResPtr res = drmModeGetResources(fd);
    if (res != NULL) {
        printf("res crtc=%d conn=%d enc=%d\n", res->count_crtcs, res->count_connectors, res->count_encoders);
        drmModeFreeResources(res);
    }
    close(fd);
    return 0;
}
