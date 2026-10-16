/* The Metal transport between the guest and the VM host's executor (spec §5.6): a virtio-gpu context of capset
 * MN_CAPSET_DXMT, blobs of whole host pages, one command ring per guest thread in a ring blob, and control messages
 * (struct mn_ctrl) sent with EXECBUFFER. Shared by both sides (C11). */
#ifndef MN_PROTO_H
#define MN_PROTO_H
#include <stddef.h>
#include <stdint.h>

#define MN_CAPSET_DXMT 24 /* under 32: the guest driver builds its capset mask with `1 << id` on an int */
#define MN_PAGE 16384     /* the host page: every blob is a multiple of it */

/* A ring blob: struct mn_ring in its first 4 KiB, then MN_RING_BYTES of records, then the reply area. */
#define MN_RING_BYTES (1u << 20)
#define MN_REPLY_BYTES (64u << 10)
#define MN_RING_DATA 4096u
#define MN_RING_REPLY (MN_RING_DATA + MN_RING_BYTES)
#define MN_RING_BLOB_BYTES ((MN_RING_REPLY + MN_REPLY_BYTES + MN_PAGE - 1) / MN_PAGE * MN_PAGE)
#define MN_REC_MAX (MN_RING_BYTES / 4) /* the largest payload; larger data goes through a blob */
#define MN_MAX_RINGS 256          /* per context: the host refuses more */
#define MN_ATTACHED 0x41545441u   /* reply_len after the host attached the ring ("ATTA") */

struct mn_ring {
    _Alignas(64) _Atomic uint64_t head;     /* bytes the guest has published */
    _Alignas(64) _Atomic uint64_t tail;     /* bytes the host has consumed */
    _Alignas(64) _Atomic uint64_t done_seq; /* the last record the host finished */
    _Alignas(64) _Atomic uint32_t parked;   /* 1: the consumer sleeps, send MN_CTRL_WAKE; 2: it was sent */
    _Alignas(64) uint32_t reply_len;        /* the last synchronous record's reply, in the reply area */
    int32_t reply_status;
};

/* A record: then len payload bytes, padded to 8. Records are numbered from 1 per ring. */
struct mn_rec { uint32_t op, len; uint64_t seq; };
#define MN_REC_SYNC 0x80000000u /* in op: the guest waits for this record's reply */

struct mn_ctrl { uint32_t type, reserved; uint64_t ring_blob, arg; };
enum {
    MN_CTRL_RING_ATTACH = 1, /* ring_blob is now a thread's ring: the host sets reply_len to MN_ATTACHED */
    MN_CTRL_RING_DETACH,     /* and no longer: the host stops its consumer before the next control message */
    MN_CTRL_WAKE,            /* the ring's consumer is parked and has work */
    MN_CTRL_WAIT,            /* this message's fence completes once the ring's done_seq >= arg; wakes the ring */
    MN_CTRL_PEEK,            /* the probe's: host reads u64 MN_PROBE_MAGIC at blob ring_blob + arg, writes magic + 1 */
};

enum {
    MN_OP_WRAP = 0,         /* the rest of the record area is padding: continue at its start */
    MN_OP_NOP = 1,
    MN_OP_BLOB_RELEASE = 2, /* u64 blob id: the thread is done with it (sent synchronously before freeing it) */
    MN_OP_TEST_ECHO = 0x100, MN_OP_TEST_SET, MN_OP_TEST_GET, MN_OP_TEST_FILL, MN_OP_TEST_SUM, /* 4a's test ops */
    MN_OP_TEST_DELAY,  /* u64 ns: the host busy-waits that long (the fenced-wait bench) */
    MN_OP_TEST_ERRORS, /* replies u64: asynchronous records that failed on the host since this context began */
    MN_OP_TEST_HOLD,   /* struct mn_test_hold: the host keeps writing the blob's first byte for ns */
};
struct mn_test_fill { uint64_t blob_id, offset, len; uint32_t byte, reserved; };
struct mn_test_sum { uint64_t blob_id, offset, len; };
struct mn_test_hold { uint64_t blob_id, ns; };

/* Host statuses (negative errno values, the same on both sides). */
enum { MN_E2BIG = -7, MN_EINVAL = -22, MN_ENOSYS = -38 };

#define MN_PROBE_MAGIC 0x4d4e44584d540001ull

/* The x86-64 guest and the arm64 host must agree on every byte of these. */
_Static_assert(sizeof(struct mn_rec) == 16, "mn_rec");
_Static_assert(sizeof(struct mn_ctrl) == 24, "mn_ctrl");
_Static_assert(offsetof(struct mn_ring, head) == 0 && offsetof(struct mn_ring, tail) == 64 &&
               offsetof(struct mn_ring, done_seq) == 128 && offsetof(struct mn_ring, parked) == 192 &&
               offsetof(struct mn_ring, reply_len) == 256 && offsetof(struct mn_ring, reply_status) == 260 &&
               sizeof(struct mn_ring) == 320 && sizeof(struct mn_ring) <= MN_RING_DATA, "mn_ring");
_Static_assert(sizeof(struct mn_test_fill) == 32 && sizeof(struct mn_test_sum) == 24 &&
               sizeof(struct mn_test_hold) == 16, "test op payloads");

#endif
