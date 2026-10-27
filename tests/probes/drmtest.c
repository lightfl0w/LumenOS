#include <drm.h>
#include <fcntl.h>
#include <pixman.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
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

    drmModePlaneResPtr pr = drmModeGetPlaneResources(fd);
    uint32_t plane_id = 0;
    if (pr != NULL) {
        printf("planes=%d\n", pr->count_planes);
        if (pr->count_planes > 0) {
            plane_id = pr->planes[0];
        }
        drmModeFreePlaneResources(pr);
    }

    drmModePlanePtr pl = plane_id ? drmModeGetPlane(fd, plane_id) : NULL;
    if (pl != NULL) {
        printf("plane %u formats=%d crtcs=%08x\n", pl->plane_id, pl->count_formats, pl->possible_crtcs);
        for (int i = 0; i < pl->count_formats; i++) {
            printf("  fmt %08x\n", pl->formats[i]);
        }
        drmModeFreePlane(pl);
    }

    struct drm_mode_create_dumb cd;
    memset(&cd, 0, sizeof(cd));
    cd.width = 32;
    cd.height = 32;
    cd.bpp = 32;
    if (ioctl(fd, DRM_IOCTL_MODE_CREATE_DUMB, &cd) == 0) {
        printf("dumb handle=%u pitch=%u size=%llu\n", cd.handle, cd.pitch, (unsigned long long)cd.size);
        struct drm_mode_map_dumb md;
        memset(&md, 0, sizeof(md));
        md.handle = cd.handle;
        if (ioctl(fd, DRM_IOCTL_MODE_MAP_DUMB, &md) == 0) {
            uint32_t *map = mmap(NULL, cd.size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, md.offset);
            if (map != MAP_FAILED) {
                for (uint32_t i = 0; i < 32 * 32; i++) {
                    map[i] = 0x0000ff00;
                }
                uint32_t fb_id = 0;
                if (drmModeAddFB(fd, 32, 32, 24, 32, cd.pitch, cd.handle, &fb_id) == 0) {
                    printf("addfb=%u\n", fb_id);
                    drmModeAtomicReq *req = drmModeAtomicAlloc();
                    if (req != NULL) {
                        drmModeAtomicAddProperty(req, plane_id, 0x101, fb_id);
                        drmModeAtomicAddProperty(req, plane_id, 0x100, 35);
                        drmModeAtomicAddProperty(req, plane_id, 0x102, 0);
                        drmModeAtomicAddProperty(req, plane_id, 0x103, 0);
                        drmModeAtomicAddProperty(req, plane_id, 0x104, 32 << 16);
                        drmModeAtomicAddProperty(req, plane_id, 0x105, 32 << 16);
                        drmModeAtomicAddProperty(req, plane_id, 0x106, 64);
                        drmModeAtomicAddProperty(req, plane_id, 0x107, 64);
                        drmModeAtomicAddProperty(req, plane_id, 0x108, 128);
                        drmModeAtomicAddProperty(req, plane_id, 0x109, 128);
                        int rc = drmModeAtomicCommit(fd, req, 0, NULL);
                        printf("atomic rc=%d\n", rc);
                        drmModeAtomicFree(req);
                    }
                }
                munmap(map, cd.size);
            }
        }
        struct drm_mode_destroy_dumb dd;
        memset(&dd, 0, sizeof(dd));
        dd.handle = cd.handle;
        ioctl(fd, DRM_IOCTL_MODE_DESTROY_DUMB, &dd);
    }

    close(fd);
    return 0;
}
