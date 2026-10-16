/* The Metal transport between the guest and the VM host's executor (spec §5.6): a virtio-gpu context of capset
 * MN_CAPSET_DXMT, blobs of whole host pages, and control messages (struct mn_ctrl) sent with EXECBUFFER. */
#ifndef MN_PROTO_H
#define MN_PROTO_H
#include <stdint.h>

#define MN_CAPSET_DXMT 24 /* under 32: the guest driver builds its capset mask with `1 << id` on an int */
#define MN_PAGE 16384 /* the host page: every blob is a multiple of it */

struct mn_ctrl { uint32_t type, reserved; uint64_t ring_blob, arg; };
enum { MN_CTRL_PEEK = 5 }; /* the probe's: host reads u64 MN_PROBE_MAGIC at blob ring_blob + arg, writes magic + 1 */

#define MN_PROBE_MAGIC 0x4d4e44584d540001ull

#endif
