#include "uapi/drm.h"
#include "kernel/drm/drm.h"

#include "arch/time/pit.h"
#include "arch/x86_64/irq/interrupt/interrupt.h"
#include "drivers/char/serial/rtc.h"
#include "drivers/driver_ops.h"
#include "drivers/video/framebuffer/udi.h"
#include "fs/chardev.h"
#include "fs/devfs.h"
#include "fs/file.h"
#include "kernel/gui/gfx.h"
#include "kernel/sched/thread.h"
#include "kernel/sync/sync.h"
#include "kernel/syscall/mmap.h"
#include "lib/malloc/kmalloc.h"
#include "lib/printf/printf.h"
#include "lib/string/str.h"
#include "arch/x86_64/mmu.h"
#include "arch/x86_64/mm/paging.h"
#include "mm/pool.h"

#include <stdint.h>

#define DRM_NAME "lumen-drm"
#define DRM_DATE "20261025"
#define DRM_DESC "LumenOS DRM/KMS on UDI"

#define DRM_MAX_GEMS 16
#define DRM_MAX_FBS 16
#define DRM_EVENT_RING 16

#define DRM_OBJ_CONNECTOR 33
#define DRM_OBJ_ENCODER 34
#define DRM_OBJ_CRTC 35
#define DRM_OBJ_PLANE 36

#define DRM_PROP_CRTC_ID 0x100
#define DRM_PROP_FB_ID 0x101
#define DRM_PROP_SRC_X 0x102
#define DRM_PROP_SRC_Y 0x103
#define DRM_PROP_SRC_W 0x104
#define DRM_PROP_SRC_H 0x105
#define DRM_PROP_CRTC_X 0x106
#define DRM_PROP_CRTC_Y 0x107
#define DRM_PROP_CRTC_W 0x108
#define DRM_PROP_CRTC_H 0x109

struct drm_gem {
    uint8_t used;
    uint32_t handle;
    uint32_t w;
    uint32_t h;
    uint32_t pitch;
    uint64_t size;
    uint32_t pages;
    uint32_t phys;
};

struct drm_fb {
    uint8_t used;
    uint32_t id;
    struct drm_gem *gem;
};

struct drm_client {
    struct drm_event_vblank events[DRM_EVENT_RING];
    uint32_t count;
    uint32_t next;
    struct TASK *waiter;
};

static struct drm_gem drm_gems[DRM_MAX_GEMS];
static struct drm_fb drm_fbs[DRM_MAX_FBS];
static uint32_t drm_fb_seq;
static uint32_t drm_vblank_seq;

static uint32_t drm_w = 1024;
static uint32_t drm_h = 768;
static uint8_t drm_ready;
static uint32_t drm_crtc_fb;

static struct GUI_UDI_BUFFER drm_scanout;
static struct GFX_CANVAS drm_scanout_canvas;

static struct SCHED_LOCK drm_lock;

static struct drm_client *drm_master;

static void drm_fill_mode(struct drm_mode_modeinfo *m) {
    memset(m, 0, sizeof(*m));
    m->clock = drm_w * drm_h * 60 / 1000;
    m->hdisplay = (uint16_t)drm_w;
    m->htotal = (uint16_t)(drm_w + 160);
    m->vdisplay = (uint16_t)drm_h;
    m->vtotal = (uint16_t)(drm_h + 40);
    m->vrefresh = 60;
    m->flags = DRM_MODE_FLAG_NHSYNC | DRM_MODE_FLAG_NVSYNC;
    m->type = DRM_MODE_TYPE_PREFERRED | DRM_MODE_TYPE_DRIVER;
    uint32_t off = (uint32_t)u32_to_dec(drm_w, m->name) - (uint32_t)(uintptr_t)m->name;
    m->name[off] = 'x';
    u32_to_dec(drm_h, m->name + off + 1);
}

static struct drm_gem *drm_gem_by_handle(uint32_t handle) {
    for (int i = 0; i < DRM_MAX_GEMS; i++) {
        if (drm_gems[i].used && drm_gems[i].handle == handle) {
            return &drm_gems[i];
        }
    }
    return NULL;
}

static struct drm_fb *drm_fb_by_id(uint32_t id) {
    for (int i = 0; i < DRM_MAX_FBS; i++) {
        if (drm_fbs[i].used && drm_fbs[i].id == id) {
            return &drm_fbs[i];
        }
    }
    return NULL;
}

static struct drm_fb *drm_fb_new(struct drm_gem *gem) {
    for (int i = 0; i < DRM_MAX_FBS; i++) {
        struct drm_fb *fb = &drm_fbs[i];
        if (!fb->used) {
            fb->used = 1;
            fb->id = 0x1000 + ++drm_fb_seq;
            fb->gem = gem;
            return fb;
        }
    }
    return NULL;
}

static void drm_push_event(struct drm_client *c, uint32_t type, uint64_t user_data) {
    if (c->count >= DRM_EVENT_RING) {
        return;
    }
    struct drm_event_vblank *ev = &c->events[(c->next + c->count) % DRM_EVENT_RING];
    memset(ev, 0, sizeof(*ev));
    ev->base.type = type;
    ev->base.length = sizeof(*ev);
    ev->user_data = user_data;
    ev->tv_sec = (uint32_t)rtc_unix_time();
    ev->tv_usec = (tick % PIT_HZ) * (1000000u / PIT_HZ);
    ev->sequence = ++drm_vblank_seq;
    ev->crtc_id = DRM_OBJ_CRTC;
    c->count++;

    struct TASK *w = c->waiter;
    c->waiter = NULL;
    if (w != NULL) {
        thread_unblock(w);
    }
}

static void drm_scanout_gem(struct drm_gem *gem) {
    struct GFX_CANVAS src;
    src.pixels = (gfx_color *)(uintptr_t)VIRT_OF(gem->phys);
    src.pitch = (int)gem->pitch;
    src.w = (int)gem->w;
    src.h = (int)gem->h;
    src.bytes = (size_t)gem->size;

    gfx_present(&drm_scanout_canvas, 0, 0, &src, 0, 0, (int)gem->w, (int)gem->h);

    struct GUI_UDI_OPS *ops = udi_active();
    if (ops != NULL) {
        struct GFX_RECT full = {0, 0, (int)drm_w, (int)drm_h};
        ops->commit(drm_scanout.handle, &full, 1);
    }
}

static void drm_scanout_blit(struct drm_gem *gem, uint32_t sx, uint32_t sy, uint32_t sw,
                             uint32_t sh, int32_t dx, int32_t dy, uint32_t dw, uint32_t dh) {
    if (sw == 0 || sh == 0 || dw == 0 || dh == 0) {
        return;
    }
    if (dx == 0 && dy == 0 && sx == 0 && sy == 0 && sw == dw && sh == dh && sw == drm_w &&
        sh == drm_h) {
        drm_scanout_gem(gem);
        return;
    }
    struct GFX_CANVAS src;
    src.pixels = (gfx_color *)(uintptr_t)VIRT_OF(gem->phys);
    src.pitch = (int)gem->pitch;
    src.w = (int)gem->w;
    src.h = (int)gem->h;
    src.bytes = (size_t)gem->size;

    gfx_blit_scale(&drm_scanout_canvas, dx, dy, (int)dw, (int)dh, &src, (int)sx, (int)sy, (int)sw,
                   (int)sh);

    struct GUI_UDI_OPS *ops = udi_active();
    if (ops != NULL) {
        struct GFX_RECT full = {0, 0, (int)drm_w, (int)drm_h};
        ops->commit(drm_scanout.handle, &full, 1);
    }
}

static int drm_open(struct FILE *file, uint32_t devnum) {
    struct drm_client *c = kmalloc(sizeof(*c));
    if (c == NULL) {
        return -1;
    }
    memset(c, 0, sizeof(*c));
    file->dev_priv = c;
    return 0;
}

static void drm_release(struct FILE *file) {
    struct drm_client *c = (struct drm_client *)file->dev_priv;

    if (c != NULL && drm_master == c) {
        drm_master = NULL;
    }

    kfree(file->dev_priv);
    file->dev_priv = NULL;
}

static int32_t drm_read(struct FILE *file, void *buf, int32_t count) {
    struct drm_client *c = (struct drm_client *)file->dev_priv;
    if (c == NULL || count < (int32_t)sizeof(struct drm_event)) {
        return -22;
    }

    while (c->count == 0) {
        if (file->fd_nonblock) {
            return -11;
        }

        c->waiter = current;
        uint32_t bf = thread_block_prepare(TASK_BLOCKED);
        thread_block_commit(bf);
    }

    struct drm_event_vblank *ev = &c->events[c->next % DRM_EVENT_RING];
    int32_t n = (int32_t)sizeof(*ev);
    if (n > count) {
        n = count;
    }

    memcpy(buf, ev, (size_t)n);
    c->next++;
    c->count--;
    return n;
}

static uint32_t drm_poll(struct FILE *file) {
    struct drm_client *c = (struct drm_client *)file->dev_priv;
    uint32_t rv = CHARDEV_POLL_WRITABLE;
    if (c != NULL && c->count > 0) {
        rv |= CHARDEV_POLL_READABLE;
    }
    return rv;
}

static void drm_version_str(char *dst, uint64_t *dst_len, const char *src) {
    size_t len = strlen(src);
    if (dst == NULL) {
        *dst_len = len + 1;
        return;
    }
    size_t n = *dst_len < len ? (size_t)*dst_len : len;
    if (n != 0) {
        memcpy(dst, src, n);
    }
    *dst_len = n;
}

static int32_t drm_ioctl_version(struct drm_version *v) {
    v->version_major = 1;
    v->version_minor = 1;
    v->version_patchlevel = 0;

    drm_version_str(v->name, &v->name_len, DRM_NAME);
    drm_version_str(v->date, &v->date_len, DRM_DATE);
    drm_version_str(v->desc, &v->desc_len, DRM_DESC);
    return 0;
}

static int32_t drm_ioctl_get_cap(struct drm_get_cap *c) {
    switch (c->capability) {
    case DRM_CAP_DUMB_BUFFER:
        c->value = 1;
        return 0;
    case DRM_CAP_DUMB_PREFERRED_DEPTH:
        c->value = 32;
        return 0;
    case DRM_CAP_VBLANK_HIGH_CRTC:
    case DRM_CAP_TIMESTAMP_MONOTONIC:
    case DRM_CAP_CRTC_IN_VBLANK_EVENT:
        c->value = 1;
        return 0;
    case DRM_CAP_ASYNC_PAGE_FLIP:
        c->value = 0;
        return 0;
    case DRM_CAP_CURSOR_WIDTH:
    case DRM_CAP_CURSOR_HEIGHT:
        c->value = 64;
        return 0;
    default:
        c->value = 0;
        return 0;
    }
}

static int32_t drm_ioctl_get_resources(struct drm_mode_card_res *r) {
    uint32_t crtcs[1] = {DRM_OBJ_CRTC};
    uint32_t connectors[1] = {DRM_OBJ_CONNECTOR};
    uint32_t encoders[1] = {DRM_OBJ_ENCODER};

    if (r->crtc_id_ptr != 0) {
        memcpy((void *)(uintptr_t)r->crtc_id_ptr, crtcs, sizeof(crtcs));
    }
    if (r->connector_id_ptr != 0) {
        memcpy((void *)(uintptr_t)r->connector_id_ptr, connectors, sizeof(connectors));
    }
    if (r->encoder_id_ptr != 0) {
        memcpy((void *)(uintptr_t)r->encoder_id_ptr, encoders, sizeof(encoders));
    }

    r->count_crtcs = 1;
    r->count_connectors = 1;
    r->count_encoders = 1;
    r->count_fbs = 0;
    for (int i = 0; i < DRM_MAX_FBS; i++) {
        if (drm_fbs[i].used) {
            r->count_fbs++;
        }
    }
    r->min_width = 1;
    r->min_height = 1;
    r->max_width = drm_w;
    r->max_height = drm_h;
    return 0;
}

static int32_t drm_ioctl_get_crtc(struct drm_mode_crtc *c) {
    if (c->crtc_id != 0 && c->crtc_id != DRM_OBJ_CRTC) {
        return -22;
    }
    c->crtc_id = DRM_OBJ_CRTC;
    c->fb_id = drm_crtc_fb;
    c->x = 0;
    c->y = 0;
    c->gamma_size = 0;
    c->count_connectors = 1;
    if (drm_crtc_fb != 0) {
        c->mode_valid = 1;
        drm_fill_mode(&c->mode);
    } else {
        c->mode_valid = 0;
        memset(&c->mode, 0, sizeof(c->mode));
    }
    return 0;
}

static int32_t drm_ioctl_set_crtc(struct drm_mode_crtc *c) {
    if (c->crtc_id != DRM_OBJ_CRTC) {
        return -22;
    }
    if (!c->mode_valid) {
        drm_crtc_fb = 0;
        return 0;
    }
    struct drm_fb *fb = drm_fb_by_id(c->fb_id);
    if (fb == NULL || c->x != 0 || c->y != 0) {
        return -22;
    }
    struct drm_mode_modeinfo want;
    drm_fill_mode(&want);
    if (c->mode.hdisplay != want.hdisplay || c->mode.vdisplay != want.vdisplay) {
        return -22;
    }
    drm_crtc_fb = fb->id;
    drm_scanout_gem(fb->gem);
    return 0;
}

static int32_t drm_ioctl_get_encoder(struct drm_mode_get_encoder *e) {
    if (e->encoder_id != 0 && e->encoder_id != DRM_OBJ_ENCODER) {
        return -22;
    }
    e->encoder_id = DRM_OBJ_ENCODER;
    e->encoder_type = DRM_MODE_ENCODER_VIRTUAL;
    e->crtc_id = DRM_OBJ_CRTC;
    e->possible_crtcs = 1;
    e->possible_clones = 0;
    return 0;
}

static int32_t drm_ioctl_get_connector(struct drm_mode_get_connector *c) {
    if (c->connector_id != 0 && c->connector_id != DRM_OBJ_CONNECTOR) {
        return -22;
    }
    c->connector_id = DRM_OBJ_CONNECTOR;
    c->connector_type = DRM_MODE_CONNECTOR_Virtual;
    c->connector_type_id = 1;
    c->connection = 1;
    c->encoder_id = DRM_OBJ_ENCODER;
    c->count_encoders = 1;
    c->count_props = 0;
    c->mm_width = drm_w / 4;
    c->mm_height = drm_h / 4;
    c->subpixel = 0;

    if (c->encoders_ptr != 0) {
        uint32_t encoders[1] = {DRM_OBJ_ENCODER};
        memcpy((void *)(uintptr_t)c->encoders_ptr, encoders, sizeof(encoders));
    }

    struct drm_mode_modeinfo mode;
    drm_fill_mode(&mode);
    c->count_modes = (c->modes_ptr != 0 && c->count_modes >= 1) ? 1 : 1;
    if (c->modes_ptr != 0 && c->count_modes >= 1) {
        memcpy((void *)(uintptr_t)c->modes_ptr, &mode, sizeof(mode));
    }
    c->count_modes = 1;
    return 0;
}

static int32_t drm_ioctl_get_fb(struct drm_mode_fb_cmd *f) {
    struct drm_fb *fb = drm_fb_by_id(f->fb_id);
    if (fb == NULL) {
        return -22;
    }
    f->width = fb->gem->w;
    f->height = fb->gem->h;
    f->pitch = fb->gem->pitch;
    f->bpp = 32;
    f->depth = 24;
    f->handle = fb->gem->handle;
    return 0;
}

static int32_t drm_ioctl_add_fb(struct drm_mode_fb_cmd *f) {
    struct drm_gem *gem = drm_gem_by_handle(f->handle);
    if (gem == NULL || f->bpp != 32 || f->width != gem->w || f->height != gem->h) {
        return -22;
    }
    struct drm_fb *fb = drm_fb_new(gem);
    if (fb == NULL) {
        return -12;
    }
    f->fb_id = fb->id;
    return 0;
}

static int32_t drm_ioctl_rm_fb(uint32_t *id) {
    struct drm_fb *fb = drm_fb_by_id(*id);
    if (fb == NULL) {
        return -22;
    }
    if (drm_crtc_fb == fb->id) {
        drm_crtc_fb = 0;
    }
    fb->used = 0;
    return 0;
}

static int32_t drm_ioctl_page_flip(struct FILE *file, struct drm_mode_crtc_page_flip *p) {
    if (p->crtc_id != DRM_OBJ_CRTC || drm_crtc_fb == 0) {
        return -22;
    }
    struct drm_fb *fb = drm_fb_by_id(p->fb_id);
    if (fb == NULL) {
        return -22;
    }
    drm_crtc_fb = fb->id;
    drm_scanout_gem(fb->gem);
    if (p->flags & DRM_MODE_PAGE_FLIP_EVENT) {
        drm_push_event((struct drm_client *)file->dev_priv, DRM_EVENT_FLIP_COMPLETE,
                       p->user_data);
    }
    return 0;
}

static int32_t drm_ioctl_create_dumb(struct drm_mode_create_dumb *d) {
    if (d->bpp != 32 || d->width == 0 || d->height == 0) {
        return -22;
    }
    struct drm_gem *gem = NULL;
    for (int i = 0; i < DRM_MAX_GEMS; i++) {
        if (!drm_gems[i].used) {
            gem = &drm_gems[i];
            break;
        }
    }
    if (gem == NULL) {
        return -12;
    }

    uint64_t size = (uint64_t)d->width * d->height * 4;
    uint32_t pages = (uint32_t)((size + PAGE_SIZE - 1) / PAGE_SIZE);
    uint32_t phys = palloc_pages(&kernel_pool, pages);
    if (phys == 0) {
        return -12;
    }
    memset((void *)(uintptr_t)VIRT_OF(phys), 0, (size_t)pages * PAGE_SIZE);

    memset(gem, 0, sizeof(*gem));
    gem->used = 1;
    gem->handle = (uint32_t)(gem - drm_gems) + 1;
    gem->w = d->width;
    gem->h = d->height;
    gem->pitch = d->width * 4;
    gem->size = size;
    gem->pages = pages;
    gem->phys = phys;

    d->handle = gem->handle;
    d->pitch = gem->pitch;
    d->size = size;
    return 0;
}

static int32_t drm_ioctl_map_dumb(struct drm_mode_map_dumb *m) {
    struct drm_gem *gem = drm_gem_by_handle(m->handle);
    if (gem == NULL) {
        return -22;
    }
    m->offset = (uint64_t)gem->handle << 20;
    return 0;
}

static void drm_gem_free(struct drm_gem *gem) {
    for (uint32_t i = 0; i < gem->pages; i++) {
        pfree(&kernel_pool, gem->phys + i * PAGE_SIZE);
    }
    gem->used = 0;
}

static int32_t drm_ioctl_destroy_dumb(struct drm_mode_destroy_dumb *d) {
    struct drm_gem *gem = drm_gem_by_handle(d->handle);
    if (gem == NULL) {
        return -22;
    }
    for (int i = 0; i < DRM_MAX_FBS; i++) {
        if (drm_fbs[i].used && drm_fbs[i].gem == gem) {
            drm_fbs[i].used = 0;
            if (drm_crtc_fb == drm_fbs[i].id) {
                drm_crtc_fb = 0;
            }
        }
    }
    drm_gem_free(gem);
    return 0;
}

static int32_t drm_ioctl_get_plane_res(struct drm_mode_get_plane_res *p) {
    if (p->plane_id_ptr != 0) {
        uint32_t planes[1] = {DRM_OBJ_PLANE};
        memcpy((void *)(uintptr_t)p->plane_id_ptr, planes, sizeof(planes));
    }
    p->count_planes = 1;
    return 0;
}

static int32_t drm_ioctl_get_plane(struct drm_mode_get_plane *p) {
    if (p->plane_id != 0 && p->plane_id != DRM_OBJ_PLANE) {
        return -22;
    }
    p->plane_id = DRM_OBJ_PLANE;
    p->crtc_id = drm_crtc_fb ? DRM_OBJ_CRTC : 0;
    p->fb_id = drm_crtc_fb;
    p->possible_crtcs = 1;
    p->gamma_size = 0;
    if (p->count_format_types >= 2 && p->format_type_ptr != 0) {
        uint32_t formats[2] = {DRM_FORMAT_XRGB8888, DRM_FORMAT_ARGB8888};
        memcpy((void *)(uintptr_t)p->format_type_ptr, formats, sizeof(formats));
    }
    p->count_format_types = 2;
    return 0;
}

static const struct {
    uint32_t id;
    const char *name;
    uint32_t flags;
} drm_props[] = {
    {DRM_PROP_CRTC_ID, "CRTC_ID", DRM_MODE_PROP_OBJECT | DRM_MODE_PROP_ATOMIC},
    {DRM_PROP_FB_ID, "FB_ID", DRM_MODE_PROP_OBJECT | DRM_MODE_PROP_ATOMIC},
    {DRM_PROP_SRC_X, "SRC_X", DRM_MODE_PROP_RANGE | DRM_MODE_PROP_ATOMIC},
    {DRM_PROP_SRC_Y, "SRC_Y", DRM_MODE_PROP_RANGE | DRM_MODE_PROP_ATOMIC},
    {DRM_PROP_SRC_W, "SRC_W", DRM_MODE_PROP_RANGE | DRM_MODE_PROP_ATOMIC},
    {DRM_PROP_SRC_H, "SRC_H", DRM_MODE_PROP_RANGE | DRM_MODE_PROP_ATOMIC},
    {DRM_PROP_CRTC_X, "CRTC_X", DRM_MODE_PROP_RANGE | DRM_MODE_PROP_ATOMIC},
    {DRM_PROP_CRTC_Y, "CRTC_Y", DRM_MODE_PROP_RANGE | DRM_MODE_PROP_ATOMIC},
    {DRM_PROP_CRTC_W, "CRTC_W", DRM_MODE_PROP_RANGE | DRM_MODE_PROP_ATOMIC},
    {DRM_PROP_CRTC_H, "CRTC_H", DRM_MODE_PROP_RANGE | DRM_MODE_PROP_ATOMIC},
};

static int32_t drm_ioctl_get_property(struct drm_mode_get_property *p) {
    for (uint32_t i = 0; i < sizeof(drm_props) / sizeof(drm_props[0]); i++) {
        if (drm_props[i].id == p->prop_id) {
            p->flags = drm_props[i].flags;
            memset(p->name, 0, sizeof(p->name));
            strcpy(p->name, drm_props[i].name);
            p->count_values = 0;
            p->count_enum_blobs = 0;
            return 0;
        }
    }
    return -22;
}

static uint32_t drm_prop_value(uint32_t prop) {
    struct drm_fb *fb = drm_fb_by_id(drm_crtc_fb);
    switch (prop) {
    case DRM_PROP_CRTC_ID:
        return drm_crtc_fb ? DRM_OBJ_CRTC : 0;
    case DRM_PROP_FB_ID:
        return drm_crtc_fb;
    case DRM_PROP_SRC_X:
    case DRM_PROP_SRC_Y:
    case DRM_PROP_CRTC_X:
    case DRM_PROP_CRTC_Y:
        return 0;
    case DRM_PROP_SRC_W:
    case DRM_PROP_CRTC_W:
        return fb ? fb->gem->w : drm_w;
    case DRM_PROP_SRC_H:
    case DRM_PROP_CRTC_H:
        return fb ? fb->gem->h : drm_h;
    default:
        return 0;
    }
}

static int32_t drm_ioctl_obj_get_properties(struct drm_mode_obj_get_properties *o) {
    uint32_t count = 0;
    if (o->obj_id == DRM_OBJ_PLANE || o->obj_id == DRM_OBJ_CONNECTOR) {
        count = (o->obj_id == DRM_OBJ_PLANE)
                    ? (uint32_t)(sizeof(drm_props) / sizeof(drm_props[0]))
                    : 1;
    }
    if (o->count_props >= count && o->props_ptr != 0 && o->prop_values_ptr != 0 && count) {
        for (uint32_t i = 0; i < count; i++) {
            uint32_t prop = (o->obj_id == DRM_OBJ_PLANE) ? drm_props[i].id : DRM_PROP_CRTC_ID;
            ((uint32_t *)(uintptr_t)o->props_ptr)[i] = prop;
            ((uint64_t *)(uintptr_t)o->prop_values_ptr)[i] = drm_prop_value(prop);
        }
    }
    o->count_props = count;
    return 0;
}

static int32_t drm_atomic_commit(struct FILE *file, struct drm_mode_atomic *a) {
    uint32_t *objs = (uint32_t *)(uintptr_t)a->objs_ptr;
    uint32_t *counts = (uint32_t *)(uintptr_t)a->count_props_ptr;
    uint32_t *props = (uint32_t *)(uintptr_t)a->props_ptr;
    uint64_t *values = (uint64_t *)(uintptr_t)a->prop_values_ptr;
    uint32_t prop_idx = 0;

    uint32_t fb_val = 0;
    uint32_t crtc_val = 0;
    uint64_t src_x = 0;
    uint64_t src_y = 0;
    uint64_t src_w = 0;
    uint64_t src_h = 0;
    uint64_t crtc_x = 0;
    uint64_t crtc_y = 0;
    uint64_t crtc_w = 0;
    uint64_t crtc_h = 0;

    for (uint32_t o = 0; o < a->count_objs; o++) {
        for (uint32_t k = 0; k < counts[o]; k++) {
            uint32_t prop = props[prop_idx];
            uint64_t value = values[prop_idx];
            prop_idx++;
            if (objs[o] == DRM_OBJ_PLANE) {
                switch (prop) {
                case DRM_PROP_FB_ID:
                    fb_val = (uint32_t)value;
                    break;
                case DRM_PROP_CRTC_ID:
                    crtc_val = (uint32_t)value;
                    break;
                case DRM_PROP_SRC_X:
                    src_x = value;
                    break;
                case DRM_PROP_SRC_Y:
                    src_y = value;
                    break;
                case DRM_PROP_SRC_W:
                    src_w = value;
                    break;
                case DRM_PROP_SRC_H:
                    src_h = value;
                    break;
                case DRM_PROP_CRTC_X:
                    crtc_x = value;
                    break;
                case DRM_PROP_CRTC_Y:
                    crtc_y = value;
                    break;
                case DRM_PROP_CRTC_W:
                    crtc_w = value;
                    break;
                case DRM_PROP_CRTC_H:
                    crtc_h = value;
                    break;
                default:
                    break;
                }
            }
        }
    }

    if (fb_val == 0) {
        return (a->flags & DRM_MODE_ATOMIC_TEST_ONLY) ? 0 : 0;
    }
    struct drm_fb *fb = drm_fb_by_id(fb_val);
    if (fb == NULL || crtc_val != DRM_OBJ_CRTC) {
        return -22;
    }
    if (src_w == 0 || src_h == 0 || crtc_w == 0 || crtc_h == 0) {
        return -22;
    }

    uint32_t sw = (uint32_t)(src_w >> 16);
    uint32_t sh = (uint32_t)(src_h >> 16);
    uint32_t sx = (uint32_t)(src_x >> 16);
    uint32_t sy = (uint32_t)(src_y >> 16);
    if (sw == 0 || sh == 0) {
        sw = (uint32_t)src_w;
        sh = (uint32_t)src_h;
        sx = (uint32_t)src_x;
        sy = (uint32_t)src_y;
    }
    if (sx >= fb->gem->w || sy >= fb->gem->h) {
        return -22;
    }
    if (sw > fb->gem->w - sx) {
        sw = fb->gem->w - sx;
    }
    if (sh > fb->gem->h - sy) {
        sh = fb->gem->h - sy;
    }
    if (a->flags & DRM_MODE_ATOMIC_TEST_ONLY) {
        return 0;
    }

    drm_crtc_fb = fb->id;
    drm_scanout_blit(fb->gem, sx, sy, sw, sh, (int32_t)crtc_x, (int32_t)crtc_y, (uint32_t)crtc_w,
                     (uint32_t)crtc_h);
    if (a->flags & DRM_MODE_PAGE_FLIP_EVENT) {
        drm_push_event((struct drm_client *)file->dev_priv, DRM_EVENT_FLIP_COMPLETE,
                       a->user_data);
    }
    return 0;
}

static int32_t drm_ioctl_set_master(struct drm_client *c) {
    if (drm_master != NULL && drm_master != c) {
        return -16;
    }

    drm_master = c;
    return 0;
}

static int32_t drm_ioctl_drop_master(struct drm_client *c) {
    if (drm_master == c) {
        drm_master = NULL;
    }

    return 0;
}

static int32_t drm_ioctl(struct FILE *file, uint32_t cmd, uint64_t arg) {
    void *p = (void *)(uintptr_t)arg;

    lock_acquire(&drm_lock);
    int32_t rc;
    switch (cmd) {
    case DRM_IOCTL_VERSION:
        rc = drm_ioctl_version((struct drm_version *)p);
        break;
    case DRM_IOCTL_GET_MAGIC:
        *(uint32_t *)p = 1;
        rc = 0;
        break;
    case DRM_IOCTL_SET_VERSION:
        rc = 0;
        break;
    case DRM_IOCTL_GET_CAP:
        rc = drm_ioctl_get_cap((struct drm_get_cap *)p);
        break;
    case DRM_IOCTL_SET_CLIENT_CAP:
        rc = 0;
        break;
    case DRM_IOCTL_SET_MASTER:
        rc = drm_ioctl_set_master((struct drm_client *)file->dev_priv);
        break;
    case DRM_IOCTL_DROP_MASTER:
        rc = drm_ioctl_drop_master((struct drm_client *)file->dev_priv);
        break;
    case DRM_IOCTL_AUTH_MAGIC:
        rc = 0;
        break;
    case DRM_IOCTL_MODE_GETRESOURCES:
        rc = drm_ioctl_get_resources((struct drm_mode_card_res *)p);
        break;
    case DRM_IOCTL_MODE_GETCRTC:
        rc = drm_ioctl_get_crtc((struct drm_mode_crtc *)p);
        break;
    case DRM_IOCTL_MODE_SETCRTC:
        rc = drm_ioctl_set_crtc((struct drm_mode_crtc *)p);
        break;
    case DRM_IOCTL_MODE_GETENCODER:
        rc = drm_ioctl_get_encoder((struct drm_mode_get_encoder *)p);
        break;
    case DRM_IOCTL_MODE_GETCONNECTOR:
        rc = drm_ioctl_get_connector((struct drm_mode_get_connector *)p);
        break;
    case DRM_IOCTL_MODE_GETPROPERTY:
        rc = drm_ioctl_get_property((struct drm_mode_get_property *)p);
        break;
    case DRM_IOCTL_MODE_GETFB:
        rc = drm_ioctl_get_fb((struct drm_mode_fb_cmd *)p);
        break;
    case DRM_IOCTL_MODE_ADDFB:
        rc = drm_ioctl_add_fb((struct drm_mode_fb_cmd *)p);
        break;
    case DRM_IOCTL_MODE_RMFB:
        rc = drm_ioctl_rm_fb((uint32_t *)p);
        break;
    case DRM_IOCTL_MODE_PAGE_FLIP:
        rc = drm_ioctl_page_flip(file, (struct drm_mode_crtc_page_flip *)p);
        break;
    case DRM_IOCTL_MODE_CREATE_DUMB:
        rc = drm_ioctl_create_dumb((struct drm_mode_create_dumb *)p);
        break;
    case DRM_IOCTL_MODE_MAP_DUMB:
        rc = drm_ioctl_map_dumb((struct drm_mode_map_dumb *)p);
        break;
    case DRM_IOCTL_MODE_DESTROY_DUMB:
        rc = drm_ioctl_destroy_dumb((struct drm_mode_destroy_dumb *)p);
        break;
    case DRM_IOCTL_MODE_GETPLANERESOURCES:
        rc = drm_ioctl_get_plane_res((struct drm_mode_get_plane_res *)p);
        break;
    case DRM_IOCTL_MODE_GETPLANE:
        rc = drm_ioctl_get_plane((struct drm_mode_get_plane *)p);
        break;
    case DRM_IOCTL_MODE_OBJ_GETPROPERTIES:
        rc = drm_ioctl_obj_get_properties((struct drm_mode_obj_get_properties *)p);
        break;
    case DRM_IOCTL_MODE_ATOMIC:
        rc = drm_atomic_commit(file, (struct drm_mode_atomic *)p);
        break;
    default:
        rc = -25;
        break;
    }
    lock_release(&drm_lock);
    return rc;
}

static const struct CHARDEV_OPS drm_ops = {drm_open,  drm_release, drm_read,
                                            NULL,       drm_ioctl,   drm_poll};

const struct CHARDEV_OPS *drm_chardev_ops(void) {
    return &drm_ops;
}

int drm_fd_mmap(int32_t fd, uint32_t base, uint32_t pages, uint32_t prot, uint64_t offset) {
    if (fd < 0 || fd >= (int32_t)MAX_FILES_OPEN_PER_PROC) {
        return -1;
    }
    uint32_t gfd = fd_local2global((uint32_t)fd);
    if (gfd >= MAX_FILE_OPEN) {
        return -1;
    }
    struct FILE *f = file_get(gfd);
    if (f == NULL || f->devfs_node == 0 || f->dev_major != DRM_MAJOR) {
        return -1;
    }

    struct drm_gem *gem = drm_gem_by_handle((uint32_t)(offset >> 20));
    if (gem == NULL || pages > gem->pages) {
        return -1;
    }

    for (uint32_t i = 0; i < pages; i++) {
        uint64_t *pte = arch_pte_create(arch_current_pgd(), base + i * PAGE_SIZE);
        if (pte == NULL) {
            return -1;
        }
        *pte = pte_wx((uint64_t)(gem->phys + i * PAGE_SIZE) | PTE_P | PTE_U,
                      !!(prot & PROT_WRITE), !!(prot & PROT_EXEC));
        arch_tlb_flush(base + i * PAGE_SIZE);
    }
    return 0;
}

static int drm_drv_init(void) {
    if (drm_ready) {
        return 0;
    }
    if (udi_init(&drm_w, &drm_h, 32) != 0) {
        kprintf("[drm] no display backend\n");
        return -1;
    }
    struct GUI_UDI_OPS *ops = udi_active();
    if (ops == NULL || ops->alloc_buffer(drm_w, drm_h, 32, &drm_scanout) != 0) {
        return -1;
    }
    drm_scanout_canvas.pixels = (gfx_color *)drm_scanout.vmem;
    drm_scanout_canvas.pitch = (int)(drm_w * 4);
    drm_scanout_canvas.w = (int)drm_scanout.w;
    drm_scanout_canvas.h = (int)drm_scanout.h;
    drm_scanout_canvas.bytes = drm_scanout.size;

    lock_init(&drm_lock);
    chardev_register(DRM_MAJOR, &drm_ops);
    devfs_add_char("/dev/dri/card0", DRM_MAJOR, 0);
    drm_ready = 1;
    kprintf("[drm] card0 ready %ux%u\n", drm_w, drm_h);
    return 0;
}

DRIVER_REGISTER("drm", 40, drm_drv_init);
