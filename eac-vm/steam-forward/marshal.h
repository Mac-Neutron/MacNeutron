/* Marshalling of lsteamclient's Unix-call params between the guest's forwarder and mn-steam-host (spec §5.9).
 * The params structs are packed (pshpack1), so their bytes travel as they are; the pointer fields a descriptor lists
 * travel as copies, and are swapped for host copies on the host and restored to the guest's pointers on the way back.
 * Every unpack checks lengths and never reads outside the message. */
#ifndef MN_MARSHAL_H
#define MN_MARSHAL_H
#include <stddef.h>
#include <stdint.h>
#include "wire.h"

enum mn_kind {
    MN_STR_IN = 1,   /* NUL-terminated string, copied in */
    MN_FIXED,        /* pointer to `size` bytes (a scalar, struct or array), copied in and back */
    MN_BUF,          /* pointer to (count at size_offset) x `size` bytes, copied in and back */
    MN_UNSUPPORTED,  /* this call can't be carried: it fails before anything is sent */
    MN_RET_BUF,      /* a returned struct u_buffer { u64 ptr, len }: the host's bytes come back as a guest copy */
};
enum { MN_IN_ONLY = 1,  /* const pointee: not copied back */
       MN_INNER = 2 };  /* MN_FIXED whose pointee holds a pointer (inner_ptr) to inner_size_field bytes, copied back */

struct mn_field {
    uint16_t offset;      /* of the pointer in the params struct */
    uint8_t kind, flags;
    uint32_t size;        /* MN_FIXED: bytes; MN_BUF: bytes per element */
    uint16_t size_offset; /* MN_BUF: offset of the count field in the params struct */
    uint8_t size_width;   /* MN_BUF: its width in bytes (1, 2, 4 or 8); counts are unsigned, negative as 0 */
    uint8_t reserved;
    uint16_t inner_ptr, inner_size; /* MN_INNER: offsets inside the pointee of the pointer and its int32 byte count */
};
struct mn_call_desc {
    const char *name;
    uint32_t params_size;
    uint16_t nfields;
    const struct mn_field *fields;
};

struct mn_buf { uint8_t *data; size_t len, cap; };
void mn_buf_free(struct mn_buf *b);

/* Allocations the host makes for one call, freed together. */
struct mn_arena { void **ptrs; size_t n, cap; };
void mn_arena_free(struct mn_arena *a);

/* A registry write recorded on the host, replayed by the guest. */
typedef void (*mn_reg_fn)(void *ctx, uint32_t type, const char *key, size_t key_len, const char *name,
                          size_t name_len, const void *data, size_t data_len);

enum { MN_E_UNSUPPORTED = -1, MN_E_FORMAT = -2, MN_E_TOO_BIG = -3, MN_E_MISMATCH = -4, MN_E_NOMEM = -5 };

/* Guest: the request for one call. */
int mn_pack_request(const struct mn_call_desc *d, uint32_t index, const void *params, struct mn_buf *out);
/* Host: the call's index and a host copy of its params (pointers into `arena`). `msg` excludes nothing: header first. */
int mn_unpack_request(const struct mn_call_desc *descs, uint32_t ndescs, const void *msg, size_t len,
                      uint32_t *index, void **host_params, struct mn_arena *arena);
/* Host: the reply, after the call ran on host_params; the registry writes the call made go along. */
struct mn_reg_log { struct mn_buf entries; uint32_t count; };
int mn_reg_log_add(struct mn_reg_log *log, uint32_t type, const char *key, size_t key_len, const char *name,
                   size_t name_len, const void *data, size_t data_len);
int mn_pack_reply(const struct mn_call_desc *d, uint32_t status, const void *host_params,
                  const struct mn_reg_log *log, struct mn_buf *out);
/* Guest: copies the results into the caller's params and buffers (keeping the caller's pointers), replays registry
 * writes through `apply`, and returns the call's status in *status. */
int mn_unpack_reply(const struct mn_call_desc *d, const void *msg, size_t len, void *params, uint32_t *status,
                    mn_reg_fn apply, void *ctx);

/* FNV-1a over every size and offset in the table: guest and host exchange it before their first call. */
uint32_t mn_table_checksum(const struct mn_call_desc *descs, unsigned n);

#endif
