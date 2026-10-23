/* Steam forwarding wire format (spec §5.9). One request, one reply, on a stream connection owned by one guest thread.
 * Request: mn_req_hdr, the params struct's bytes, then each described pointer field in descriptor order as
 * u32 length (MN_NULL for a NULL pointer) + bytes.
 * Reply: mn_rep_hdr, the params struct's bytes as the host left them, then each copied-back field (not MN_IN_ONLY,
 * not NULL) as u32 length + bytes, then u32 registry-entry count and the entries (mn_reg_hdr + key + name + data).
 * Lengths count the bytes after the header. */
#ifndef MN_WIRE_H
#define MN_WIRE_H
#include <stdint.h>

#define MN_MAGIC 0x46534e4du /* "MNSF" */
#define MN_NULL 0xffffffffu
#define MN_MAX_MESSAGE (64u << 20)
#define MN_WOW64 0x80000000u /* in a request's index: a WoW64 call (mn_calls_wow64, the host's WoW64 functions) */

struct mn_req_hdr { uint32_t magic, index, len; };
struct mn_rep_hdr { uint32_t magic, status, len; };
/* A registry write the host recorded for the guest to replay: key relative to HKCU or absolute (\Registry\...). */
struct mn_reg_hdr { uint16_t kind, reserved; uint32_t type, key_len, name_len, data_len; };
enum { MN_REG_SET_VALUE = 1 };

#endif
