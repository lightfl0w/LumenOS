#ifndef UAPI_DRM_H
#define UAPI_DRM_H

#include <stdint.h>

#define DRM_IOCTL_BASE 0x64u

#define DRM_IOCTL_VERSION 0xc0406400u
#define DRM_IOCTL_GET_MAGIC 0x80046402u
#define DRM_IOCTL_SET_VERSION 0xc0106407u
#define DRM_IOCTL_GET_CAP 0xc010640cu
#define DRM_IOCTL_SET_CLIENT_CAP 0x4010640du
#define DRM_IOCTL_AUTH_MAGIC 0xc0046403u
#define DRM_IOCTL_GET_CLIENT 0xc01c6414u
#define DRM_IOCTL_SET_MASTER 0x641eu
#define DRM_IOCTL_DROP_MASTER 0x641fu

#define DRM_IOCTL_MODE_GETRESOURCES 0xc04064a0u
#define DRM_IOCTL_MODE_GETCRTC 0xc06864a1u
#define DRM_IOCTL_MODE_SETCRTC 0xc06864a2u
#define DRM_IOCTL_MODE_GETENCODER 0xc01464a6u
#define DRM_IOCTL_MODE_GETCONNECTOR 0xc05064a7u
#define DRM_IOCTL_MODE_GETPROPERTY 0xc04064aau
#define DRM_IOCTL_MODE_GETFB 0xc01c64adu
#define DRM_IOCTL_MODE_ADDFB 0xc01c64aeu
#define DRM_IOCTL_MODE_RMFB 0xc00464afu
#define DRM_IOCTL_MODE_PAGE_FLIP 0xc01864b0u
#define DRM_IOCTL_MODE_CREATE_DUMB 0xc02064b2u
#define DRM_IOCTL_MODE_MAP_DUMB 0xc01064b3u
#define DRM_IOCTL_MODE_DESTROY_DUMB 0xc00464b4u
#define DRM_IOCTL_MODE_GETPLANERESOURCES 0xc01064b5u
#define DRM_IOCTL_MODE_GETPLANE 0xc02064b6u
#define DRM_IOCTL_MODE_OBJ_GETPROPERTIES 0xc02064b9u
#define DRM_IOCTL_MODE_ATOMIC 0xc03864bcu

#define DRM_CAP_DUMB_BUFFER 0x1
#define DRM_CAP_VBLANK_HIGH_CRTC 0x2
#define DRM_CAP_DUMB_PREFERRED_DEPTH 0x3
#define DRM_CAP_TIMESTAMP_MONOTONIC 0x6
#define DRM_CAP_ASYNC_PAGE_FLIP 0x7
#define DRM_CAP_CURSOR_WIDTH 0x8
#define DRM_CAP_CURSOR_HEIGHT 0x9
#define DRM_CAP_CRTC_IN_VBLANK_EVENT 0x12

#define DRM_CLIENT_CAP_UNIVERSAL_PLANES 2
#define DRM_CLIENT_CAP_ATOMIC 3

#define DRM_EVENT_VBLANK 0x01
#define DRM_EVENT_FLIP_COMPLETE 0x02

struct drm_version {
    int32_t version_major;
    int32_t version_minor;
    int32_t version_patchlevel;
    uint64_t name_len;
    char *name;
    uint64_t date_len;
    char *date;
    uint64_t desc_len;
    char *desc;
};

struct drm_auth {
    uint32_t magic;
};

struct drm_set_version {
    int32_t drm_di_major;
    int32_t drm_di_minor;
    int32_t drm_dd_major;
    int32_t drm_dd_minor;
};

struct drm_get_cap {
    uint64_t capability;
    uint64_t value;
};

struct drm_set_client_cap {
    uint64_t capability;
    uint64_t value;
};

struct drm_event {
    uint32_t type;
    uint32_t length;
};

struct drm_event_vblank {
    struct drm_event base;
    uint64_t user_data;
    uint32_t tv_sec;
    uint32_t tv_usec;
    uint32_t sequence;
    uint32_t crtc_id;
};

#define DRM_DISPLAY_MODE_LEN 32

struct drm_mode_modeinfo {
    uint32_t clock;
    uint16_t hdisplay;
    uint16_t hsync_start;
    uint16_t hsync_end;
    uint16_t htotal;
    uint16_t hskew;
    uint16_t vdisplay;
    uint16_t vsync_start;
    uint16_t vsync_end;
    uint16_t vtotal;
    uint16_t vscan;
    uint32_t vrefresh;
    uint32_t flags;
    uint32_t type;
    char name[DRM_DISPLAY_MODE_LEN];
};

struct drm_mode_card_res {
    uint64_t fb_id_ptr;
    uint64_t crtc_id_ptr;
    uint64_t connector_id_ptr;
    uint64_t encoder_id_ptr;
    uint32_t count_fbs;
    uint32_t count_crtcs;
    uint32_t count_connectors;
    uint32_t count_encoders;
    uint32_t min_width;
    uint32_t max_width;
    uint32_t min_height;
    uint32_t max_height;
};

struct drm_mode_crtc {
    uint64_t set_connectors_ptr;
    uint32_t count_connectors;
    uint32_t crtc_id;
    uint32_t fb_id;
    uint32_t x;
    uint32_t y;
    uint32_t gamma_size;
    uint32_t mode_valid;
    struct drm_mode_modeinfo mode;
};

struct drm_mode_get_encoder {
    uint32_t encoder_id;
    uint32_t encoder_type;
    uint32_t crtc_id;
    uint32_t possible_crtcs;
    uint32_t possible_clones;
};

struct drm_mode_get_connector {
    uint64_t encoders_ptr;
    uint64_t modes_ptr;
    uint64_t props_ptr;
    uint64_t prop_values_ptr;
    uint32_t count_modes;
    uint32_t count_props;
    uint32_t count_encoders;
    uint32_t encoder_id;
    uint32_t connector_id;
    uint32_t connector_type;
    uint32_t connector_type_id;
    uint32_t connection;
    uint32_t mm_width;
    uint32_t mm_height;
    uint32_t subpixel;
    uint32_t pad;
};

#define DRM_PROP_NAME_LEN 32

struct drm_mode_get_property {
    uint64_t values_ptr;
    uint64_t enum_blob_ptr;
    uint32_t count_values;
    uint32_t count_enum_blobs;
    uint32_t flags;
    uint32_t prop_id;
    char name[DRM_PROP_NAME_LEN];
};

struct drm_mode_fb_cmd {
    uint32_t fb_id;
    uint32_t width;
    uint32_t height;
    uint32_t pitch;
    uint32_t bpp;
    uint32_t depth;
    uint32_t handle;
};

struct drm_mode_crtc_page_flip {
    uint32_t crtc_id;
    uint32_t fb_id;
    uint32_t flags;
    uint32_t reserved;
    uint64_t user_data;
};

struct drm_mode_create_dumb {
    uint32_t height;
    uint32_t width;
    uint32_t bpp;
    uint32_t flags;
    uint32_t handle;
    uint32_t pitch;
    uint64_t size;
};

struct drm_mode_map_dumb {
    uint32_t handle;
    uint32_t pad;
    uint64_t offset;
};

struct drm_mode_destroy_dumb {
    uint32_t handle;
};

struct drm_mode_get_plane_res {
    uint64_t plane_id_ptr;
    uint32_t count_planes;
};

struct drm_mode_get_plane {
    uint32_t plane_id;
    uint32_t crtc_id;
    uint32_t fb_id;
    uint32_t possible_crtcs;
    uint32_t gamma_size;
    uint32_t count_format_types;
    uint64_t format_type_ptr;
};

struct drm_mode_obj_get_properties {
    uint64_t props_ptr;
    uint64_t prop_values_ptr;
    uint32_t count_props;
    uint32_t obj_id;
    uint32_t obj_type;
};

struct drm_mode_atomic {
    uint32_t flags;
    uint32_t count_objs;
    uint64_t objs_ptr;
    uint64_t count_props_ptr;
    uint64_t props_ptr;
    uint64_t prop_values_ptr;
    uint64_t reserved;
    uint64_t user_data;
};

#define DRM_MODE_PAGE_FLIP_EVENT (1u << 0)
#define DRM_MODE_PAGE_FLIP_ASYNC (1u << 1)

#define DRM_MODE_ATOMIC_TEST_ONLY (1u << 15)
#define DRM_MODE_ATOMIC_NONBLOCK (1u << 14)
#define DRM_MODE_ATOMIC_ALLOW_MODESET (1u << 13)

#define DRM_MODE_TYPE_PREFERRED (1u << 3)
#define DRM_MODE_TYPE_DRIVER (1u << 6)

#define DRM_MODE_FLAG_NHSYNC (1u << 1)
#define DRM_MODE_FLAG_NVSYNC (1u << 3)

#define DRM_MODE_CONNECTOR_VGA 1
#define DRM_MODE_CONNECTOR_Virtual 14

#define DRM_MODE_ENCODER_NONE 0
#define DRM_MODE_ENCODER_VIRTUAL 5

#define DRM_MODE_PROP_RANGE (1u << 1)
#define DRM_MODE_PROP_IMMUTABLE (1u << 2)
#define DRM_MODE_PROP_OBJECT (1u << 6)
#define DRM_MODE_PROP_ATOMIC 0x80000000u

#define DRM_MODE_OBJECT_CRTC 0xccccccccu
#define DRM_MODE_OBJECT_CONNECTOR 0xc0c0c0c0u
#define DRM_MODE_OBJECT_PLANE 0xeeeeeeeeu

#define DRM_FORMAT_XRGB8888 0x34325258u
#define DRM_FORMAT_ARGB8888 0x34325241u

#define DRM_MAJOR 226

#endif
