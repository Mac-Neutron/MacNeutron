/* Winemetal's 153 Unix-call slots, described for forwarding (milestone 4b-1): each slot's params struct, whether the
 * guest waits for it, and the fields the marshaller translates. Slot numbers and the facts behind each entry are the
 * call catalog's (docs/research/2026-10-15-vm-route/winemetal-calls.md). Shared by the guest and the host. */
#ifndef MN_WM_CALLS_H
#define MN_WM_CALLS_H
#include <stdint.h>
#include "handles.h"

#define MN_WM_SLOTS 153
#define MN_WM_STATS MN_WM_SLOTS /* op MN_OP_WM + 153: struct mn_wm_stats */
#define MN_OP_WM 0x1000u        /* a call is op MN_OP_WM + slot */

/* NTSTATUS values the forwarder answers with. */
#define MN_STATUS_UNSUCCESSFUL 0xC0000001u
#define MN_STATUS_NOT_IMPLEMENTED 0xC0000002u
#define MN_STATUS_INVALID_HANDLE 0xC0000008u
#define MN_STATUS_INVALID_PARAMETER 0xC000000Du
#define MN_STATUS_BUFFER_TOO_SMALL 0xC0000023u
#define MN_STATUS_NOT_SUPPORTED 0xC00000BBu

enum mn_wm_field_kind {
    WMF_HANDLE_IN = 1, /* an obj_handle_t the host translates (0 stays 0) */
    WMF_HANDLE_OUT,    /* an obj_handle_t the host writes: the guest picks it and the host binds it */
    WMF_IN,            /* a pointer to size bytes (or count * size with WMF_ARRAY) the host reads */
    WMF_OUT,           /* ... the host writes, copied back */
    WMF_INOUT,         /* ... both */
    WMF_STR_IN,        /* a pointer to a NUL-terminated string */
    WMF_VALUE_HANDLE,  /* typed obj_handle_t, but a value: passed through */
    WMF_BLOB_PTR,      /* a pointer into blob memory both sides map (count bytes): the host maps it, never copies */
};
#define WMF_ARRAY 1

struct mn_wm_field {
    uint16_t offset;
    uint8_t kind;  /* enum mn_wm_field_kind */
    uint8_t hkind; /* handle fields: enum mn_hkind */
    uint8_t own;   /* WMF_HANDLE_OUT: enum mn_own */
    uint8_t flags; /* WMF_ARRAY */
    uint32_t size; /* region bytes, or per element with WMF_ARRAY */
    uint16_t count_offset; /* WMF_ARRAY: the element count, in the same struct as this field */
    uint8_t count_width;   /* its bytes: 1, 4 or 8 */
    uint8_t nested;        /* 1 + an index into mn_wm_nested: fields inside each element; 0 for none */
};
struct mn_wm_nested { uint16_t nfields; const struct mn_wm_field *fields; };
extern const struct mn_wm_nested mn_wm_nested[];

enum { MN_WM_QUEUED, MN_WM_SYNC, MN_WM_REFUSED };
enum mn_wm_special {
    MN_SP_NONE,
    MN_SP_RETAIN,    /* 0: the tables only */
    MN_SP_RELEASE,   /* 1 */
    MN_SP_BUFFER,    /* 18: memory over blobs (Task 5) */
    MN_SP_ENCODE,    /* 36-38: command lists (Task 6) */
    MN_SP_REGION,    /* 45, 145: texture data within bounds (Task 5) */
    MN_SP_UPDATE,    /* 107: buffer data within bounds (Task 5) */
    MN_SP_HEAP,      /* 138, 141, 142: emulated heaps (Task 5) */
    MN_SP_RESIDENCY, /* 133-135: residency sets with emulated heaps (Task 5) */
    MN_SP_LISTENER,  /* 108-110 (Task 7) */
    MN_SP_PATH,      /* 54, 112, 113, 115, 117, 119: host paths (Task 8) */
    MN_SP_SM50,      /* 74-88 (Task 8; 77 in Task 5) */
    MN_SP_ENUMERATE, /* 91 (Task 8) */
    MN_SP_CONSTANTS, /* 98 (Task 8) */
};

struct mn_wm_call {
    const char *name;
    uint32_t params_size;
    uint8_t mode;    /* MN_WM_QUEUED, MN_WM_SYNC, MN_WM_REFUSED */
    uint8_t special; /* enum mn_wm_special */
    uint16_t nfields;
    uint32_t refuse_status; /* MN_WM_REFUSED */
    const struct mn_wm_field *fields;
};
extern const struct mn_wm_call mn_wm_calls[MN_WM_SLOTS];

struct mn_wm_stats { uint64_t live, deferred, failures; };

/* A pixel format's block: bytes, width and height (1 x 1 for uncompressed); -1 for a format the forwarder doesn't
 * know. The bytes a texture region's data covers (slots 45 and 145), or UINT64_MAX if its pitches are too small. */
int mn_wm_format_block(uint32_t format, uint32_t *bytes, uint32_t *w, uint32_t *h);
uint64_t mn_wm_region_extent(uint32_t format, uint64_t width, uint64_t height, uint64_t depth, uint64_t bytes_per_row,
                             uint64_t bytes_per_image);

#endif
