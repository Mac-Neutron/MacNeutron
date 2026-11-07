/* The forwarding client (spec §5.9): one connection per calling thread, a table-checksum handshake, one request and one
 * reply per call. Used by the guest's forwarder (vsock) and by the host-side test client (Unix socket). */
#ifndef MN_CLIENT_H
#define MN_CLIENT_H
#include <stdint.h>
#include "marshal.h"

#define MN_STATUS_NOT_IMPLEMENTED 0xc0000002u
#define MN_STATUS_INVALID_PARAMETER 0xc000000du
#define MN_STATUS_DISCONNECTED 0xc000020cu

extern const struct mn_call_desc mn_calls[];
extern const struct mn_call_desc mn_calls_wow64[]; /* the same calls with a 32-bit caller's params (gen.py) */
extern const unsigned int mn_ncalls;
uint32_t mn_handshake_mine(void); /* the handshake's value this guest sends (marshal.h's mn_handshake_value) */
/* Both tables' checksum, which guest and host exchange before their first call. */
uint32_t mn_tables_checksum(void);

typedef int (*mn_connect_fn)(void); /* a connected stream socket, or -1 */
void mn_client_init(mn_connect_fn connect, mn_reg_fn apply, void *ctx);
/* Runs call `index` with `params` on the host. Returns the call's status, or one of the MN_STATUS_ values above when it
 * can't be carried or the host is gone (logged; the next call reconnects). */
uint32_t mn_client_call(uint32_t index, void *params);
/* The same for a 32-bit caller's call (a WoW64 Unix call): its params' pointers are 4 bytes, valid in this process. */
uint32_t mn_client_call_wow64(uint32_t index, void *params);

/* Shared by client and host: whole-message I/O on a stream socket. */
int mn_read_full(int fd, void *buf, size_t n);
int mn_write_full(int fd, const void *buf, size_t n);
#endif
