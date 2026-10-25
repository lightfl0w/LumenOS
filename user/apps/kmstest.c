#include "uapi/drm.h"
#include "uapi/fs.h"
#include "user/libc/stdio.h"
#include "user/libc/stdlib.h"
#include "user/libc/syscall.h"
#include "lib/string/str.h"

static int g_fail;

static void expect(int cond, const char *msg) {
    if (cond) {
        printf("  [ok] %s\n", msg);
    } else {
        g_fail = 1;
        printf("  [FAIL] %s\n", msg);
    }
}

static int64_t lc_ioctl(int fd, uint32_t cmd, void *arg) {
    int64_t ret;
    __asm__ volatile("syscall"
                     : "=a"(ret)
                     : "a"(16L), "D"((int64_t)fd), "S"((int64_t)cmd), "d"((int64_t)arg)
                     : "rcx", "r11", "memory");
    return ret;
}

int main(void) {
    printf("kmstest: start\n");

    int fd = open("/dev/dri/card0", O_RDWR);
    expect(fd >= 0, "open /dev/dri/card0");
    if (fd < 0) {
        printf("kmstest: no card, abort\n");
        return 1;
    }

    struct drm_version ver;
    memset(&ver, 0, sizeof(ver));
    char name[32];
    ver.name = name;
    ver.name_len = sizeof(name);
    expect(lc_ioctl(fd, DRM_IOCTL_VERSION, &ver) == 0, "DRM_IOCTL_VERSION");
    printf("  driver: %s %d.%d.%d\n", name, ver.version_major, ver.version_minor,
           ver.version_patchlevel);

    struct drm_get_cap cap = {DRM_CAP_DUMB_BUFFER, 0};
    expect(lc_ioctl(fd, DRM_IOCTL_GET_CAP, &cap) == 0 && cap.value == 1, "GET_CAP DUMB_BUFFER");

    struct drm_mode_card_res res;
    memset(&res, 0, sizeof(res));
    uint32_t crtcs[1], connectors[1];
    res.crtc_id_ptr = (uint64_t)(uintptr_t)crtcs;
    res.connector_id_ptr = (uint64_t)(uintptr_t)connectors;
    expect(lc_ioctl(fd, DRM_IOCTL_MODE_GETRESOURCES, &res) == 0, "GETRESOURCES");
    printf("  crtc=%d connector=%d\n", crtcs[0], connectors[0]);

    struct drm_mode_get_connector conn;
    memset(&conn, 0, sizeof(conn));
    conn.connector_id = connectors[0];
    struct drm_mode_modeinfo modes[2];
    conn.modes_ptr = (uint64_t)(uintptr_t)modes;
    conn.count_modes = 2;
    expect(lc_ioctl(fd, DRM_IOCTL_MODE_GETCONNECTOR, &conn) == 0 && conn.count_modes == 1,
           "GETCONNECTOR");
    printf("  mode: %s conn=%d\n", modes[0].name, conn.connection);

    struct drm_mode_create_dumb dumb;
    memset(&dumb, 0, sizeof(dumb));
    dumb.width = modes[0].hdisplay;
    dumb.height = modes[0].vdisplay;
    dumb.bpp = 32;
    expect(lc_ioctl(fd, DRM_IOCTL_MODE_CREATE_DUMB, &dumb) == 0, "CREATE_DUMB");
    printf("  dumb handle=%d size=%d\n", dumb.handle, (uint32_t)dumb.size);

    struct drm_mode_map_dumb map;
    memset(&map, 0, sizeof(map));
    map.handle = dumb.handle;
    expect(lc_ioctl(fd, DRM_IOCTL_MODE_MAP_DUMB, &map) == 0, "MAP_DUMB");

    uint32_t *fbmem = mmap(0, (uint32_t)dumb.size, PROT_READ | PROT_WRITE, MAP_SHARED, fd,
                           (uint32_t)map.offset);
    expect(fbmem != MAP_FAILED, "mmap dumb buffer");
    for (uint32_t y = 0; y < dumb.height; y++) {
        uint8_t r = (uint8_t)(y * 255 / dumb.height);
        for (uint32_t x = 0; x < dumb.width; x++) {
            uint8_t g = (uint8_t)(x * 255 / dumb.width);
            fbmem[y * (dumb.pitch / 4) + x] = 0xFF000000u | ((uint32_t)r << 16) | (g << 8);
        }
    }
    printf("  gradient drawn\n");

    struct drm_mode_fb_cmd fbc;
    memset(&fbc, 0, sizeof(fbc));
    fbc.width = dumb.width;
    fbc.height = dumb.height;
    fbc.pitch = dumb.pitch;
    fbc.bpp = 32;
    fbc.depth = 24;
    fbc.handle = dumb.handle;
    expect(lc_ioctl(fd, DRM_IOCTL_MODE_ADDFB, &fbc) == 0 && fbc.fb_id != 0, "ADDFB");

    struct drm_mode_crtc crtc;
    memset(&crtc, 0, sizeof(crtc));
    crtc.crtc_id = crtcs[0];
    crtc.fb_id = fbc.fb_id;
    crtc.count_connectors = 1;
    crtc.mode = modes[0];
    crtc.mode_valid = 1;
    expect(lc_ioctl(fd, DRM_IOCTL_MODE_SETCRTC, &crtc) == 0, "SETCRTC");
    printf("  scanout active, fb=%d\n", fbc.fb_id);

    struct drm_mode_crtc_page_flip flip;
    memset(&flip, 0, sizeof(flip));
    flip.crtc_id = crtcs[0];
    flip.fb_id = fbc.fb_id;
    flip.flags = DRM_MODE_PAGE_FLIP_EVENT;
    flip.user_data = 0xABCD;
    expect(lc_ioctl(fd, DRM_IOCTL_MODE_PAGE_FLIP, &flip) == 0, "PAGE_FLIP");

    struct drm_event_vblank ev;
    memset(&ev, 0, sizeof(ev));
    int64_t n = read(fd, &ev, sizeof(ev));
    expect(n == (int64_t)sizeof(ev) && ev.base.type == DRM_EVENT_FLIP_COMPLETE,
           "read flip event");
    if (n == (int64_t)sizeof(ev)) {
        printf("  flip event seq=%d userdata=%x\n", ev.sequence,
               (uint32_t)ev.user_data);
    }

    struct drm_mode_get_plane_res pres;
    memset(&pres, 0, sizeof(pres));
    uint32_t planes[1];
    pres.plane_id_ptr = (uint64_t)(uintptr_t)planes;
    expect(lc_ioctl(fd, DRM_IOCTL_MODE_GETPLANERESOURCES, &pres) == 0 && pres.count_planes == 1,
           "GETPLANERESOURCES");

    struct drm_mode_obj_get_properties op;
    memset(&op, 0, sizeof(op));
    op.obj_id = planes[0];
    op.obj_type = DRM_MODE_OBJECT_PLANE;
    uint32_t prop_ids[10];
    uint64_t prop_vals[10];
    op.props_ptr = (uint64_t)(uintptr_t)prop_ids;
    op.prop_values_ptr = (uint64_t)(uintptr_t)prop_vals;
    op.count_props = 10;
    expect(lc_ioctl(fd, DRM_IOCTL_MODE_OBJ_GETPROPERTIES, &op) == 0 && op.count_props == 10,
           "OBJ_GETPROPERTIES(plane)");

    uint32_t objs[1] = {planes[0]};
    uint32_t counts[1] = {10};
    uint32_t atomic_props[10];
    uint64_t atomic_vals[10];
    for (int i = 0; i < 10; i++) {
        atomic_props[i] = prop_ids[i];
        atomic_vals[i] = prop_vals[i];
    }
    struct drm_mode_atomic atom;
    memset(&atom, 0, sizeof(atom));
    atom.flags = DRM_MODE_ATOMIC_TEST_ONLY;
    atom.count_objs = 1;
    atom.objs_ptr = (uint64_t)(uintptr_t)objs;
    atom.count_props_ptr = (uint64_t)(uintptr_t)counts;
    atom.props_ptr = (uint64_t)(uintptr_t)atomic_props;
    atom.prop_values_ptr = (uint64_t)(uintptr_t)atomic_vals;
    expect(lc_ioctl(fd, DRM_IOCTL_MODE_ATOMIC, &atom) == 0, "ATOMIC TEST_ONLY");

    atom.flags = DRM_MODE_ATOMIC_ALLOW_MODESET | DRM_MODE_PAGE_FLIP_EVENT;
    atom.user_data = 0x1234;
    expect(lc_ioctl(fd, DRM_IOCTL_MODE_ATOMIC, &atom) == 0, "ATOMIC commit");
    memset(&ev, 0, sizeof(ev));
    n = read(fd, &ev, sizeof(ev));
    expect(n == (int64_t)sizeof(ev) && ev.base.type == DRM_EVENT_FLIP_COMPLETE,
           "read atomic flip event");

    close(fd);
    printf("kmstest: %s\n", g_fail ? "FAIL" : "PASS");
    return g_fail;
}
